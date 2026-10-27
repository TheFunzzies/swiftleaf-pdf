// Run with tests/signature-crypto.ps1 after a debug build.
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"
#include "../src/shared/mupdf/pkcs7-windows.h"
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void check_revision(fz_context* ctx, pdf_pkcs7_signer* signer, int permission, int change) {
    pdf_document* doc = NULL;
    pdf_page* page = NULL;
    pdf_annot* first = NULL;
    pdf_annot* second = NULL;
    pdf_pkcs7_verifier* verifier = NULL;
    fz_var(doc);
    fz_var(page);
    fz_var(first);
    fz_var(second);
    fz_var(verifier);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, "docs/test/swiftleaf-sample.pdf");
        page = pdf_load_page(ctx, doc, 0);
        first = pdf_create_signature_widget(ctx, page, "Certifier");
        second = pdf_create_signature_widget(ctx, page, "Approver");
        pdf_signature_set_value(ctx, doc, pdf_annot_obj(ctx, first), signer, (int64_t)time(NULL));
        pdf_obj* value = pdf_dict_get(ctx, pdf_annot_obj(ctx, first), PDF_NAME(V));
        pdf_obj* refs = pdf_dict_put_array(ctx, value, PDF_NAME(Reference), 1);
        pdf_obj* ref = pdf_array_push_dict(ctx, refs, 3);
        pdf_dict_put(ctx, ref, PDF_NAME(Type), PDF_NAME(SigRef));
        pdf_dict_put(ctx, ref, PDF_NAME(TransformMethod), PDF_NAME(DocMDP));
        pdf_obj* params = pdf_dict_put_dict(ctx, ref, PDF_NAME(TransformParams), 2);
        pdf_dict_put_int(ctx, params, PDF_NAME(P), permission);
        pdf_dict_put_name(ctx, params, PDF_NAME(V), "1.2");
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* perms = pdf_dict_put_dict(ctx, root, PDF_NAME(Perms), 1);
        pdf_dict_put(ctx, perms, PDF_NAME(DocMDP), value);
        pdf_write_options opts = pdf_default_write_options;
        pdf_save_document(ctx, doc, "tmp/signature-certified.pdf", &opts);
        pdf_drop_annot(ctx, second);
        second = NULL;
        pdf_drop_annot(ctx, first);
        first = NULL;
        fz_drop_page(ctx, (fz_page*)page);
        page = NULL;
        pdf_drop_document(ctx, doc);
        doc = NULL;

        doc = pdf_open_document(ctx, "tmp/signature-certified.pdf");
        page = pdf_load_page(ctx, doc, 0);
        first = pdf_keep_annot(ctx, pdf_first_widget(ctx, page));
        if (change == 0) {
            pdf_annot* approver = pdf_next_widget(ctx, first);
            pdf_signature_set_value(ctx, doc, pdf_annot_obj(ctx, approver), signer, (int64_t)time(NULL));
        } else if (change == 1) {
            second = pdf_create_annot(ctx, page, PDF_ANNOT_TEXT);
            pdf_set_annot_contents(ctx, second, "permitted only by P=3");
        } else {
            pdf_obj* page_obj = pdf_lookup_page_obj(ctx, doc, 0);
            pdf_dict_put_rect(ctx, page_obj, PDF_NAME(MediaBox), fz_make_rect(0, 0, 100, 100));
        }
        opts.do_incremental = 1;
        pdf_save_document(ctx, doc, "tmp/signature-certified.pdf", &opts);
        pdf_drop_annot(ctx, second);
        second = NULL;
        pdf_drop_annot(ctx, first);
        first = NULL;
        fz_drop_page(ctx, (fz_page*)page);
        page = NULL;
        pdf_drop_document(ctx, doc);
        doc = NULL;

        doc = pdf_open_document(ctx, "tmp/signature-certified.pdf");
        page = pdf_load_page(ctx, doc, 0);
        pdf_obj* field = pdf_annot_obj(ctx, pdf_first_widget(ctx, page));
        verifier = pkcs7_windows_new_verifier(ctx);
        if (pdf_check_digest(ctx, verifier, doc, field) != PDF_SIGNATURE_ERROR_OKAY)
            fz_throw(ctx, FZ_ERROR_GENERIC, "incremental change damaged signed bytes");
        int invalid = pdf_validate_signature(ctx, doc, field);
        int permitted = change == 0 ? permission >= 2 : change == 1 ? permission == 3 : 0;
        printf("DocMDP P=%d change=%d invalid=%d\n", permission, change, invalid);
        if ((invalid == 0) != permitted) fz_throw(ctx, FZ_ERROR_GENERIC, "DocMDP change classification failed");
    }
    fz_always(ctx) {
        pdf_drop_verifier(ctx, verifier);
        pdf_drop_annot(ctx, second);
        pdf_drop_annot(ctx, first);
        fz_drop_page(ctx, (fz_page*)page);
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_rethrow(ctx);
    }
}

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    pdf_pkcs7_signer* signer = NULL;
    fz_stream* in = NULL;
    unsigned char* cms = NULL;
    int result = 1;
    fz_var(signer);
    fz_var(in);
    fz_var(cms);
    fz_var(result);
    fz_try(ctx) {
        signer = pkcs7_windows_read_pfx(ctx, argv[1], argv[2]);
        pkcs7_windows_set_tsa_url(signer, strcmp(argv[3], "none") ? argv[3] : "");
        size_t capacity = signer->max_digest_size(ctx, signer);
        cms = fz_malloc(ctx, capacity);
        in = fz_open_memory(ctx, (const unsigned char*)"signature regression\n", 21);
        int size = signer->create_digest(ctx, signer, in, cms, capacity);
        fz_seek(ctx, in, 0, SEEK_SET);
        if (size <= 0 || pkcs7_windows_check_digest(ctx, in, (char*)cms, size) != PDF_SIGNATURE_ERROR_OKAY)
            fz_throw(ctx, FZ_ERROR_GENERIC, "bad detached signature");
        FILE* out = fopen(argv[4], "wb");
        if (!out) fz_throw(ctx, FZ_ERROR_GENERIC, "cannot write test CMS");
        fwrite(cms, 1, size, out);
        fclose(out);
        HCRYPTMSG msg = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
        DWORD cb = 0;
        if (!msg || !CryptMsgUpdate(msg, cms, size, TRUE) ||
            !CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &cb))
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot read CMS");
        PCMSG_SIGNER_INFO si = LocalAlloc(LPTR, cb);
        CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, si, &cb);
        int found = 0;
        for (DWORD i = 0; i < si->UnauthAttrs.cAttr; i++) {
            CRYPT_ATTRIBUTE* attr = &si->UnauthAttrs.rgAttr[i];
            if (strcmp(attr->pszObjId, szOID_RFC3161_counterSign)) continue;
            PCRYPT_TIMESTAMP_CONTEXT ts = NULL;
            if (attr->cValue != 1 || !CryptVerifyTimeStampSignature(attr->rgValue[0].pbData, attr->rgValue[0].cbData,
                                                                    si->EncryptedHash.pbData, si->EncryptedHash.cbData,
                                                                    NULL, &ts, NULL, NULL))
                fz_throw(ctx, FZ_ERROR_GENERIC, "timestamp does not cover signature value");
            CryptMemFree(ts);
            found++;
            out = fopen("tmp/signature-token.der", "wb");
            if (out) {
                fwrite(attr->rgValue[0].pbData, 1, attr->rgValue[0].cbData, out);
                fclose(out);
            }
        }
        LocalFree(si);
        CryptMsgClose(msg);
        if (strcmp(argv[3], "none") && found != 1) fz_throw(ctx, FZ_ERROR_GENERIC, "requested timestamp missing");
        if (!strcmp(argv[3], "none")) {
            for (int p = 1; p <= 3; p++) {
                for (int change = 0; change < 3; change++) check_revision(ctx, signer, p, change);
            }
        }
        printf("PASS detached CMS; RFC3161 tokens=%d\n", found);
        result = 0;
    }
    fz_always(ctx) {
        fz_drop_stream(ctx, in);
        pdf_drop_signer(ctx, signer);
        fz_free(ctx, cms);
    }
    fz_catch(ctx) {
        fprintf(stderr, "%s\n", fz_caught_message(ctx));
    }
    fz_drop_context(ctx);
    return result;
}
