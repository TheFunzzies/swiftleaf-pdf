/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// Digitally signing a PDF with a PKCS#7 certificate. Kept out of
// EngineMupdf.cpp because PdfFilter / PdfPreview compile that file too and
// import mupdf through libsumatrapdf.def; signing is an app-only feature and
// dragging pdf_sign_signature & co. into those shell extensions would mean
// exporting them for no one.

#include "base/Base.h"
#include "base/File.h"
#include "base/ScopedWin.h"

// ng: Windows signs through wincrypt; Linux uses MuPDF's OpenSSL helper.
#if OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <wincrypt.h>
#endif

extern "C" {
#include <mupdf/pdf.h>
#if OS_WIN
#include "mupdf/pkcs7-windows.h"
#elif defined(SUMATRA_HAVE_OPENSSL)
#include "mupdf/helpers/pkcs7-openssl.h"
#endif
}

#include "gui/UIModels.h"

#include "Annotation.h"
#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "EngineMupdf.h"

#if OS_WIN || defined(SUMATRA_HAVE_OPENSSL)

// True when this widget is a signature field the document's author left for
// someone to sign, i.e. a signature field with no signature in it yet. Its
// field name (which may legitimately be empty) goes to fieldNameOut, so the
// Sign Document dialog can preselect that field (issue #5964).
bool IsUnsignedSignatureWidget(Annotation* widget, TempStr* fieldNameOut) {
    if (!AnnotationIsLive(widget) || widget->type != AnnotationType::Widget) {
        return false;
    }
    EngineMupdf* e = widget->engine;
    pdf_annot* a = widget->pdfannot;
    if (!e || !a) {
        return false;
    }
    fz_context* ctx = e->Ctx();
    ScopedRecursiveMutex scope(&e->docLock);
    bool res = false;
    fz_try(ctx) {
        if (pdf_widget_type(ctx, a) == PDF_WIDGET_TYPE_SIGNATURE && !pdf_widget_is_signed(ctx, a)) {
            res = true;
            if (fieldNameOut) {
                char* name = pdf_load_field_name(ctx, pdf_annot_obj(ctx, a));
                *fieldNameOut = name ? str::DupTemp(Str(name)) : StrL("");
                fz_free(ctx, name);
            }
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        res = false;
    }
    return res;
}

// Names of the signature fields that are present but not signed yet, plus the
// 1-based page each sits on. A PDF prepared for signing has such a field, and
// signing it (rather than adding another) is what the author intended.
void EngineMupdfGetUnsignedSignatureFields(EngineBase* engine, StrVec& names, Vec<int>& pageNos) {
    EngineMupdf* epdf = AsEngineMupdf(engine);
    if (!epdf || !epdf->pdfdoc) {
        return;
    }
    fz_context* ctx = epdf->BaseCtx();
    ScopedRecursiveMutex scope(&epdf->docLock);

    // finding the widgets means loading every page, which is O(pages). The
    // form's field tree says up front whether any signature field exists, so
    // documents without one (most of them) don't pay for it.
    int nSigFields = 0;
    fz_try(ctx) {
        nSigFields = pdf_count_signatures(ctx, epdf->pdfdoc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        return;
    }
    if (nSigFields == 0) {
        return;
    }

    pdf_page* page = nullptr;
    fz_var(page);
    fz_try(ctx) {
        int nPages = pdf_count_pages(ctx, epdf->pdfdoc);
        for (int pageIdx = 0; pageIdx < nPages; pageIdx++) {
            page = pdf_load_page(ctx, epdf->pdfdoc, pageIdx);
            for (pdf_annot* w = pdf_first_widget(ctx, page); w; w = pdf_next_widget(ctx, w)) {
                if (pdf_widget_type(ctx, w) != PDF_WIDGET_TYPE_SIGNATURE) {
                    continue;
                }
                if (pdf_widget_is_signed(ctx, w)) {
                    continue;
                }
                char* name = pdf_load_field_name(ctx, pdf_annot_obj(ctx, w));
                names.Append(name ? Str(name) : StrL(""));
                VecAppend(pageNos, pageIdx + 1);
                fz_free(ctx, name);
            }
            fz_drop_page(ctx, (fz_page*)page);
            page = nullptr;
        }
    }
    fz_always(ctx) {
        fz_drop_page(ctx, (fz_page*)page);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
}

// Finds the unsigned signature widget named name; returns a kept reference the
// caller drops. The page that owns it is left loaded and returned in pageOut
// (caller drops it). Dropping that page before signing unbinds the annot
// ("annotation not bound to any page") if nothing else is holding it.
static pdf_annot* FindUnsignedSignatureWidget(fz_context* ctx, pdf_document* doc, Str name, int* pageNoOut,
                                              pdf_page** pageOut) {
    pdf_page* page = nullptr;
    pdf_annot* found = nullptr;
    fz_var(page);
    fz_var(found);
    fz_try(ctx) {
        int nPages = pdf_count_pages(ctx, doc);
        for (int pageIdx = 0; !found && pageIdx < nPages; pageIdx++) {
            page = pdf_load_page(ctx, doc, pageIdx);
            for (pdf_annot* w = pdf_first_widget(ctx, page); w; w = pdf_next_widget(ctx, w)) {
                if (pdf_widget_type(ctx, w) != PDF_WIDGET_TYPE_SIGNATURE || pdf_widget_is_signed(ctx, w)) {
                    continue;
                }
                char* wName = pdf_load_field_name(ctx, pdf_annot_obj(ctx, w));
                bool isMatch = str::Eq(Str(wName), name);
                fz_free(ctx, wName);
                if (isMatch) {
                    found = pdf_keep_annot(ctx, w);
                    if (pageNoOut) {
                        *pageNoOut = pageIdx + 1;
                    }
                    if (pageOut) {
                        *pageOut = page;
                    }
                    page = nullptr;
                    break;
                }
            }
            fz_drop_page(ctx, (fz_page*)page);
            page = nullptr;
        }
    }
    fz_always(ctx) {
        fz_drop_page(ctx, (fz_page*)page);
    }
    fz_catch(ctx) {
        fz_rethrow(ctx);
    }
    return found;
}

// Signing changed what the page draws, so the cached display list (captured
// before the signature appearance existed) has to go. The annotation list is
// left alone: the document is re-loaded right after saving.
static void DropCachedPageRendering(EngineMupdf* e, int pageNo) {
    ScopedRecursiveMutex scope(&e->pagesLock);
    FzPageInfo* pageInfo = e->PageInfoByPageNo(pageNo);
    if (!pageInfo) {
        return;
    }
    pageInfo->elementsNeedRebuilding = true;
    ScopedMutex rl(&e->renderLock);
    if (pageInfo->displayList) {
        fz_drop_display_list(e->Ctx(), pageInfo->displayList);
        pageInfo->displayList = nullptr;
    }
}

// Signs the document with a PKCS#7 certificate from a .pfx / .p12 file, either
// filling in an existing (unsigned) signature field or creating a new one.
// The signature isn't complete until the document is saved: mupdf reserves
// space for it here and computes the digest over the saved bytes, so the caller
// must follow this with EngineMupdfSaveUpdated() (incremental, so earlier
// signatures stay valid).
bool EngineMupdfSignDocument(EngineBase* engine, const PdfSignArgs& args, Str* errOut) {
    EngineMupdf* epdf = AsEngineMupdf(engine);
    if (!epdf || !epdf->pdfdoc) {
        return false;
    }
    // signing changes a form field value, which can regenerate appearances and
    // run form JavaScript, so it must happen on the base context (see BaseCtx)
    fz_context* ctx = epdf->BaseCtx();
    ScopedRecursiveMutex scope(&epdf->docLock);

    pdf_pkcs7_signer* signer = nullptr;
    pdf_annot* widget = nullptr;
    pdf_page* page = nullptr;
    fz_image* graphic = nullptr;
    fz_buffer* imgBuf = nullptr;
    int pageNo = args.pageNo;
    bool ok = false;
    fz_var(signer);
    fz_var(widget);
    fz_var(page);
    fz_var(graphic);
    fz_var(imgBuf);
    fz_var(ok);
    fz_var(pageNo);

    static_assert(kPdfSignShowLabels == PDF_SIGNATURE_SHOW_LABELS);
    static_assert(kPdfSignShowDN == PDF_SIGNATURE_SHOW_DN);
    static_assert(kPdfSignShowDate == PDF_SIGNATURE_SHOW_DATE);
    static_assert(kPdfSignShowTextName == PDF_SIGNATURE_SHOW_TEXT_NAME);
    static_assert(kPdfSignShowGraphicName == PDF_SIGNATURE_SHOW_GRAPHIC_NAME);

    fz_try(ctx) {
#if OS_WIN
        if (args.certThumbprint) {
            signer = pkcs7_windows_read_store(ctx, CStrTemp(args.certThumbprint));
        } else {
            signer = pkcs7_windows_read_pfx(ctx, CStrTemp(args.certPath), CStrTemp(args.certPassword));
        }
        Str tsaUrl = args.tsaUrl;
        if (len(tsaUrl) == 0 && gSettings && len(gSettings->tsaUrl) > 0) {
            tsaUrl = gSettings->tsaUrl;
        }
        if (len(tsaUrl) > 0 && signer) {
            pkcs7_windows_set_tsa_url(signer, CStrTemp(tsaUrl));
        }
#else
        if (args.certThumbprint) {
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "the system certificate store is unavailable");
        }
        signer = pkcs7_openssl_read_pfx(ctx, CStrTemp(args.certPath), CStrTemp(args.certPassword));
#endif
        if (args.fieldName) {
            widget = FindUnsignedSignatureWidget(ctx, epdf->pdfdoc, args.fieldName, &pageNo, &page);
            if (!widget) {
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "signature field '%s' is gone", CStrTemp(args.fieldName));
            }
        } else {
            page = pdf_load_page(ctx, epdf->pdfdoc, pageNo - 1);
            // the name only has to be unique; mupdf doesn't check, so make it
            // unlikely to collide with fields the document already has
            TempStr name = fmt("SumatraSignature%d", pdf_count_signatures(ctx, epdf->pdfdoc) + 1);
            widget = pdf_create_signature_widget(ctx, page, CStrTemp(name));
            if (!args.rect.IsEmpty()) {
                pdf_set_annot_rect(ctx, widget, ToFzRect(args.rect));
            }
        }
        if (args.imagePath) {
            Str imgData = file::ReadFile(args.imagePath);
            if (len(imgData) == 0) {
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "could not read signature image");
            }
            imgBuf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)imgData.s, (size_t)len(imgData));
            str::Free(imgData);
            graphic = fz_new_image_from_buffer(ctx, imgBuf);
        }
        const char* reason = args.reason ? CStrTemp(args.reason) : nullptr;
        const char* location = args.location ? CStrTemp(args.location) : nullptr;
        // PDF_SIGNATURE_DEFAULT_APPEARANCE, minus the logo: that draws mupdf's
        // logo into the signature, which has no business in a user's document
        int appearance = args.appearanceFlags >= 0 ? args.appearanceFlags : kPdfSignDefaultAppearance;
        appearance &= ~PDF_SIGNATURE_SHOW_LOGO;
        pdf_sign_signature(ctx, widget, signer, appearance, graphic, reason, location);

        pdf_obj* wobj = pdf_annot_obj(ctx, widget);
        pdf_obj* v = pdf_dict_get(ctx, wobj, PDF_NAME(V));
        if (v && args.reason) {
            pdf_dict_put_string(ctx, v, PDF_NAME(Reason), CStrTemp(args.reason), len(args.reason));
        }
        if (v && args.location) {
            pdf_dict_put_string(ctx, v, PDF_NAME(Location), CStrTemp(args.location), len(args.location));
        }
        if (v && args.isCertify) {
            pdf_obj* refArr = pdf_dict_get(ctx, v, PDF_NAME(Reference));
            if (!refArr) {
                refArr = pdf_dict_put_array(ctx, v, PDF_NAME(Reference), 1);
            }
            pdf_obj* sigRef = pdf_array_push_dict(ctx, refArr, 3);
            pdf_dict_put(ctx, sigRef, PDF_NAME(Type), PDF_NAME(SigRef));
            pdf_dict_put(ctx, sigRef, PDF_NAME(TransformMethod), PDF_NAME(DocMDP));
            pdf_obj* tp = pdf_dict_put_dict(ctx, sigRef, PDF_NAME(TransformParams), 3);
            pdf_dict_put(ctx, tp, PDF_NAME(Type), PDF_NAME(TransformParams));
            int perm = (args.mdpPerm >= 1 && args.mdpPerm <= 3) ? args.mdpPerm : 2;
            pdf_dict_put_int(ctx, tp, PDF_NAME(P), perm);
            pdf_dict_put_name(ctx, tp, PDF_NAME(V), "1.2");

            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, epdf->pdfdoc), PDF_NAME(Root));
            if (root) {
                pdf_obj* perms = pdf_dict_get(ctx, root, PDF_NAME(Perms));
                if (!perms) {
                    perms = pdf_dict_put_dict(ctx, root, PDF_NAME(Perms), 1);
                }
                pdf_dict_put(ctx, perms, PDF_NAME(DocMDP), v);
            }
        }
        ok = true;
    }
    fz_always(ctx) {
        fz_drop_image(ctx, graphic);
        fz_drop_buffer(ctx, imgBuf);
        pdf_drop_annot(ctx, widget);
        fz_drop_page(ctx, (fz_page*)page);
        pdf_drop_signer(ctx, signer);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        if (errOut) {
            *errOut = str::Dup(Str(fz_caught_message(ctx)));
        }
        return false;
    }
    if (ok) {
        DropCachedPageRendering(epdf, pageNo);
        epdf->modifiedAnnotations = true;
    }
    return ok;
}

// Certificates in the current user's Personal store that have a private key
// and are currently valid. Labels are what the Sign Document drop-down shows;
// thumbprints are what EngineMupdfSignDocument wants in certThumbprint.
#if OS_WIN
struct SigFieldWalk {
    Vec<pdf_obj*> fields;
};
static void OnSigFieldArrive(fz_context* ctx, pdf_obj* node, void* arg, pdf_obj** values) {
    pdf_obj* ft = values && values[0] ? values[0] : pdf_dict_get_inheritable(ctx, node, PDF_NAME(FT));
    if (ft && pdf_name_eq(ctx, ft, PDF_NAME(Sig))) {
        VecAppend(((SigFieldWalk*)arg)->fields, pdf_keep_obj(ctx, node));
    }
}
static void CollectSignatureFields(fz_context* ctx, pdf_document* pdfdoc, Vec<pdf_obj*>& fields) {
    SigFieldWalk walk;
    pdf_obj* formFields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdfdoc), "Root/AcroForm/Fields");
    pdf_obj* ftName[2] = {PDF_NAME(FT), nullptr};
    pdf_obj* ftVal = nullptr;
    pdf_walk_tree(ctx, formFields, PDF_NAME(Kids), OnSigFieldArrive, nullptr, &walk, ftName, &ftVal);
    fields = walk.fields;
}
static int PageNoForSigField(fz_context* ctx, pdf_document* pdfdoc, pdf_obj* field) {
    pdf_obj* page = pdf_dict_get(ctx, field, PDF_NAME(P));
    if (!page) {
        return 0;
    }
    int pageNo = pdf_lookup_page_number(ctx, pdfdoc, page);
    return pageNo >= 0 ? pageNo + 1 : 0;
}

static TempStr FormatUnixTimeTemp(int64_t unixSecs) {
    if (unixSecs <= 0) return {};
    time_t t = (time_t)unixSecs;
    struct tm tm;
    gmtime_s(&tm, &t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M:%S UTC", &tm);
    return str::DupTemp(Str(buf));
}

static TempStr FormatPdfDateRawTemp(fz_context* ctx, pdf_obj* obj) {
    if (!obj) return {};
    const char* raw = nullptr;
    fz_try(ctx) {
        raw = pdf_to_str_buf(ctx, obj);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        raw = nullptr;
    }
    if (!raw || !raw[0]) return {};
    Str s = Str(raw);
    str::TrimPrefix(s, StrL("D:"));
    if (s.len < 14) return {};
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "%.4s/%.2s/%.2s %.2s:%.2s:%.2s", s.s, s.s + 4, s.s + 6, s.s + 8, s.s + 10, s.s + 12);
    if (n < 0) return {};
    if (s.len > 14) {
        Str tz = Str(s.s + 14, s.len - 14);
        if (tz.len > 0 && (tz.s[0] == '+' || tz.s[0] == '-' || tz.s[0] == 'Z')) {
            snprintf(buf + n, sizeof(buf) - (size_t)n, " %s", tz.s);
        }
    }
    return str::DupTemp(Str(buf));
}

bool EngineMupdfGetSignaturesInfo(EngineBase* engine, Vec<SignatureItemInfo>& outSigs) {
    EngineMupdf* epdf = AsEngineMupdf(engine);
    if (!epdf || !epdf->pdfdoc) return false;
    fz_context* ctx = epdf->Ctx();
    ScopedRecursiveMutex scope(&epdf->docLock);

    pdf_pkcs7_verifier* verifier = nullptr;
    Vec<pdf_obj*> fields;
    fz_var(verifier);

    fz_try(ctx) {
        verifier = pkcs7_windows_new_verifier(ctx);
        CollectSignatureFields(ctx, epdf->pdfdoc, fields);

        for (pdf_obj* field : fields) {
            if (!pdf_signature_is_signed(ctx, epdf->pdfdoc, field)) {
                continue;
            }
            SignatureItemInfo sig;
            char* fName = pdf_load_field_name(ctx, field);
            if (fName && fName[0]) {
                sig.fieldName = str::Dup(StrL(fName));
            } else {
                sig.fieldName = str::Dup(StrL("Signature"));
            }
            fz_free(ctx, fName);
            sig.pageNo = PageNoForSigField(ctx, epdf->pdfdoc, field);

            pdf_obj* v = pdf_dict_get(ctx, field, PDF_NAME(V));
            if (!v) v = field;

            // Check DocMDP
            pdf_obj* ref = pdf_dict_get(ctx, v, PDF_NAME(Reference));
            if (ref) {
                int nRef = pdf_array_len(ctx, ref);
                for (int ri = 0; ri < nRef; ri++) {
                    pdf_obj* sr = pdf_array_get(ctx, ref, ri);
                    pdf_obj* tm = pdf_dict_get(ctx, sr, PDF_NAME(TransformMethod));
                    if (pdf_name_eq(ctx, tm, PDF_NAME(DocMDP))) {
                        sig.isCertified = true;
                        pdf_obj* tp = pdf_dict_get(ctx, sr, PDF_NAME(TransformParams));
                        int p = pdf_dict_get_int(ctx, tp, PDF_NAME(P));
                        sig.mdpPerm = (p > 0) ? p : 2;
                        break;
                    }
                }
            }
            if (!sig.isCertified) {
                pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, epdf->pdfdoc), PDF_NAME(Root));
                pdf_obj* perms = root ? pdf_dict_get(ctx, root, PDF_NAME(Perms)) : nullptr;
                if (perms && pdf_dict_get(ctx, perms, PDF_NAME(DocMDP))) {
                    sig.isCertified = true;
                    sig.mdpPerm = 2;
                }
            }

            // Reason and Location
            pdf_obj* rObj = pdf_dict_get(ctx, v, PDF_NAME(Reason));
            if (rObj) {
                const char* rStr = pdf_to_text_string(ctx, rObj);
                if (rStr && rStr[0]) sig.reason = str::Dup(StrL(rStr));
            }
            pdf_obj* lObj = pdf_dict_get(ctx, v, PDF_NAME(Location));
            if (lObj) {
                const char* lStr = pdf_to_text_string(ctx, lObj);
                if (lStr && lStr[0]) sig.location = str::Dup(StrL(lStr));
            }

            // Date
            pdf_obj* mObj = pdf_dict_get(ctx, v, PDF_NAME(M));
            TempStr sDate = FormatPdfDateRawTemp(ctx, mObj);
            if (len(sDate) > 0) {
                sig.date = str::Dup(sDate);
            }

            pkcs7_windows_sig_info info{};
            char* contents = nullptr;
            size_t contentsLen = pdf_signature_contents(ctx, epdf->pdfdoc, field, &contents);
            if (contentsLen > 0) {
                pkcs7_windows_inspect(ctx, (unsigned char*)contents, contentsLen, &info);
            }
            fz_free(ctx, contents);

            if (info.signer_cn && info.signer_cn[0]) {
                sig.signerName = str::Dup(StrL(info.signer_cn));
            } else {
                sig.signerName = str::Dup(StrL("Unknown Signer"));
            }
            if (info.issuer_cn && info.issuer_cn[0]) {
                sig.issuerName = str::Dup(StrL(info.issuer_cn));
            }
            if (len(sig.date) == 0 && info.gen_time_unix > 0) {
                sig.date = str::Dup(FormatUnixTimeTemp(info.gen_time_unix));
            }
            if (info.cert_der && info.cert_der_len > 0) {
                sig.certDer = str::Dup(Str((const char*)info.cert_der, (size_t)info.cert_der_len));
            }

            // Verification
            pdf_signature_error certErr = PDF_SIGNATURE_ERROR_UNKNOWN;
            pdf_signature_error digErr = PDF_SIGNATURE_ERROR_UNKNOWN;
            int edits = 0;
            if (verifier) {
                certErr = pdf_check_certificate(ctx, verifier, epdf->pdfdoc, field);
                digErr = pdf_check_digest(ctx, verifier, epdf->pdfdoc, field);
                edits = pdf_signature_incremental_change_since_signing(ctx, epdf->pdfdoc, field);
            }

            int64_t nowUnix = (int64_t)time(nullptr);
            bool isExpired = (info.not_after_unix > 0 && nowUnix > info.not_after_unix);

            bool isRevoked = false;
            if (info.cert_der && info.cert_der_len > 0) {
                PCCERT_CONTEXT pCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                                   info.cert_der, (DWORD)info.cert_der_len);
                if (pCert) {
                    CERT_CHAIN_PARA chainPara{};
                    chainPara.cbSize = sizeof(chainPara);
                    PCCERT_CHAIN_CONTEXT pChain = nullptr;
                    if (CertGetCertificateChain(nullptr, pCert, nullptr, nullptr, &chainPara,
                                                CERT_CHAIN_REVOCATION_CHECK_CHAIN, nullptr, &pChain)) {
                        if (pChain->TrustStatus.dwErrorStatus & CERT_TRUST_IS_REVOKED) {
                            isRevoked = true;
                        }
                        CertFreeCertificateChain(pChain);
                    }
                    CertFreeCertificateContext(pCert);
                }
            }

            if (digErr != PDF_SIGNATURE_ERROR_OKAY || edits > 0) {
                sig.status = SignatureStatus::ModifiedAfterSigning;
                sig.statusDesc = str::Dup(StrL("Document has been modified or corrupted after signing"));
            } else if (isRevoked) {
                sig.status = SignatureStatus::Revoked;
                sig.statusDesc = str::Dup(StrL("Certificate was revoked by issuer"));
            } else if (isExpired) {
                sig.status = SignatureStatus::Expired;
                sig.statusDesc = str::Dup(StrL("Certificate has expired"));
            } else if (certErr != PDF_SIGNATURE_ERROR_OKAY) {
                sig.status = SignatureStatus::UnknownSigner;
                sig.statusDesc = str::Dup(StrL("Signer certificate is untrusted or self-signed"));
            } else {
                sig.status = SignatureStatus::Valid;
                sig.statusDesc = str::Dup(StrL("Signature is valid and document has not been modified"));
            }

            pkcs7_windows_sig_info_free(ctx, &info);
            VecAppend(outSigs, sig);
        }
    }
    fz_always(ctx) {
        for (pdf_obj* field : fields) {
            pdf_drop_obj(ctx, field);
        }
        if (verifier) {
            pdf_drop_verifier(ctx, verifier);
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        return false;
    }
    return true;
}

bool EngineMupdfClearSignature(EngineBase* engine, Str fieldName) {
    EngineMupdf* epdf = AsEngineMupdf(engine);
    if (!epdf || !epdf->pdfdoc) return false;
    fz_context* ctx = epdf->BaseCtx();
    ScopedRecursiveMutex scope(&epdf->docLock);

    bool ok = false;
    int pageNo = 1;
    Vec<pdf_obj*> fields;
    fz_try(ctx) {
        CollectSignatureFields(ctx, epdf->pdfdoc, fields);
        pdf_obj* target = nullptr;
        for (pdf_obj* field : fields) {
            if (!pdf_signature_is_signed(ctx, epdf->pdfdoc, field)) {
                continue;
            }
            char* name = pdf_load_field_name(ctx, field);
            bool match = (len(fieldName) == 0 || (name && str::Eq(fieldName, StrL(name))));
            fz_free(ctx, name);
            if (match) {
                target = field;
                pageNo = PageNoForSigField(ctx, epdf->pdfdoc, field);
                break;
            }
        }
        if (target) {
            pdf_obj* v = pdf_dict_get(ctx, target, PDF_NAME(V));
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, epdf->pdfdoc), PDF_NAME(Root));
            if (root) {
                pdf_obj* perms = pdf_dict_get(ctx, root, PDF_NAME(Perms));
                if (perms) {
                    pdf_obj* mdp = pdf_dict_get(ctx, perms, PDF_NAME(DocMDP));
                    if (mdp && v && (mdp == v || (pdf_to_num(ctx, mdp) > 0 && pdf_to_num(ctx, mdp) == pdf_to_num(ctx, v)))) {
                        pdf_dict_del(ctx, perms, PDF_NAME(DocMDP));
                    }
                }
            }
            pdf_dict_del(ctx, target, PDF_NAME(V));
            pdf_dict_del(ctx, target, PDF_NAME(AP));
            int ff = pdf_to_int(ctx, pdf_dict_get(ctx, target, PDF_NAME(Ff)));
            pdf_dict_put_int(ctx, target, PDF_NAME(Ff), ff & ~PDF_FIELD_IS_READ_ONLY);
            ok = true;
        }
    }
    fz_always(ctx) {
        for (pdf_obj* field : fields) {
            pdf_drop_obj(ctx, field);
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        return false;
    }
    if (ok) {
        DropCachedPageRendering(epdf, pageNo);
        epdf->modifiedAnnotations = true;
    }
    return ok;
}

void ListWindowsSigningCertificates(StrVec& thumbprints, StrVec& labels) {
    thumbprints.Reset();
    labels.Reset();
    HCERTSTORE hStore = CertOpenSystemStoreW(0, L"MY");
    if (!hStore) {
        return;
    }
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(hStore, cert)) != nullptr) {
        DWORD provSz = 0;
        if (!CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &provSz)) {
            continue;
        }
        if (cert->pCertInfo) {
            if (CompareFileTime(&cert->pCertInfo->NotBefore, &now) > 0) {
                continue;
            }
            if (CompareFileTime(&cert->pCertInfo->NotAfter, &now) < 0) {
                continue;
            }
            BYTE ku[2]{};
            if (CertGetIntendedKeyUsage(X509_ASN_ENCODING, cert->pCertInfo, ku, sizeof(ku))) {
                if ((ku[0] & CERT_DIGITAL_SIGNATURE_KEY_USAGE) == 0) {
                    continue;
                }
            }
        }
        BYTE hash[20];
        DWORD hashLen = sizeof(hash);
        if (!CertGetCertificateContextProperty(cert, CERT_HASH_PROP_ID, hash, &hashLen) || hashLen != 20) {
            continue;
        }
        char hex[41];
        for (DWORD i = 0; i < 20; i++) {
            snprintf(hex + (i * 2), 3, "%02X", hash[i]);
        }
        WCHAR nameW[256]{};
        CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nameW, dimof(nameW));
        TempStr name = ToUtf8Temp(WStr(nameW));
        if (len(name) == 0) {
            name = StrL("(unnamed)");
        }
        SYSTEMTIME st{};
        if (cert->pCertInfo) {
            FileTimeToSystemTime(&cert->pCertInfo->NotAfter, &st);
        }
        TempStr label = st.wYear ? fmt("%s (expires %04d-%02d-%02d)", name, st.wYear, st.wMonth, st.wDay) : name;
        thumbprints.Append(Str(hex, 40));
        labels.Append(label);
    }
    CertCloseStore(hStore, 0);
}
#else
void ListWindowsSigningCertificates(StrVec&, StrVec&) {}
#endif

#else

bool IsUnsignedSignatureWidget(Annotation*, TempStr* fieldNameOut) {
    if (fieldNameOut) {
        *fieldNameOut = {};
    }
    return false;
}

void EngineMupdfGetUnsignedSignatureFields(EngineBase*, StrVec&, Vec<int>&) {}

bool EngineMupdfSignDocument(EngineBase*, const PdfSignArgs&, Str* errOut) {
    if (errOut) {
        *errOut = str::Dup(StrL("signing a PDF is not available on this platform"));
    }
    return false;
}

void ListWindowsSigningCertificates(StrVec&, StrVec&) {}
bool EngineMupdfGetSignaturesInfo(EngineBase*, Vec<SignatureItemInfo>&) { return false; }
bool EngineMupdfClearSignature(EngineBase*, Str) { return false; }

#endif
