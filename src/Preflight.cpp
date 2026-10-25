/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"
#include "base/GuessFileType.h"
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "SumatraPDF.h"
#include "SumatraConfig.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Translations.h"
#include "DarkMode.h"
#include "Theme.h"
#include "Commands.h"
#include <mupdf/pdf.h>
#include <mupdf/pdf/recolor.h>
#include "Preflight.h"

extern const pdf_write_options gPdfDefaultWriteOptions;

static RectF FzRectToRectF(fz_rect r) {
    return RectF{r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0};
}

static HICON GetAppIcon() {
    return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
}

void FreePreflightReport(PreflightReport& report) {
    str::Free(report.encryptionMethod);
    report.javaScriptNames.Reset();
    report.colorSpaces.Reset();
    for (auto& f : report.fonts) {
        str::Free(f.baseFont);
        str::Free(f.subtype);
        str::Free(f.encoding);
    }
    VecClear(report.fonts);
    for (auto& img : report.images) {
        str::Free(img.colorSpace);
        str::Free(img.filter);
    }
    VecClear(report.images);
    VecClear(report.pageBoxes);
}

PreflightReport RunPreflight(fz_context* ctx, pdf_document* doc) {
    PreflightReport report;
    if (!ctx || !doc) return report;

    pdf_obj* trailer = pdf_trailer(ctx, doc);
    pdf_obj* root = trailer ? pdf_dict_get(ctx, trailer, PDF_NAME(Root)) : nullptr;
    if (!root) return report;

    report.pdfMajorVersion = 1;
    report.pdfMinorVersion = 7;
    pdf_obj* verObj = pdf_dict_get(ctx, root, PDF_NAME(Version));
    if (verObj) {
        const char* verStr = pdf_to_name(ctx, verObj);
        if (verStr && str::StartsWith(Str(verStr), StrL("1."))) {
            report.pdfMinorVersion = verStr[2] - '0';
        }
    }

    report.pageCount = pdf_count_pages(ctx, doc);

    // Encryption
    pdf_obj* enc = trailer ? pdf_dict_get(ctx, trailer, PDF_NAME(Encrypt)) : nullptr;
    if (enc && pdf_is_dict(ctx, enc)) {
        report.isEncrypted = true;
        int v = pdf_to_int(ctx, pdf_dict_get(ctx, enc, PDF_NAME(V)));
        int r = pdf_to_int(ctx, pdf_dict_get(ctx, enc, PDF_NAME(R)));
        report.encryptionMethod = str::Dup(fmt("Standard V%d R%d", v, r));
    } else {
        report.encryptionMethod = str::Dup(StrL("None"));
    }

    // JavaScript
    pdf_obj* names = pdf_dict_get(ctx, root, PDF_NAME(Names));
    pdf_obj* js = names ? pdf_dict_get(ctx, names, PDF_NAME(JavaScript)) : nullptr;
    if (js) {
        report.hasJavaScript = true;
        report.javaScriptNames.Append(StrL("Names/JavaScript"));
    }
    pdf_obj* openAction = pdf_dict_gets(ctx, root, "OpenAction");
    if (openAction && pdf_is_dict(ctx, openAction)) {
        pdf_obj* s = pdf_dict_get(ctx, openAction, PDF_NAME(S));
        if (s == PDF_NAME(JavaScript)) {
            report.hasJavaScript = true;
            report.javaScriptNames.Append(StrL("OpenAction/JavaScript"));
        }
    }

    // Walk pages for fonts, images, transparency, page boxes, and colorspaces
    for (int p = 0; p < report.pageCount; p++) {
        pdf_page* page = pdf_load_page(ctx, doc, p);
        if (!page) continue;
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, p);

        // Page Boxes
        PreflightPageBoxInfo pbox;
        pbox.pageNo = p + 1;
        pbox.mediaBox = FzRectToRectF(pdf_to_rect(ctx, pdf_dict_get(ctx, pageObj, PDF_NAME(MediaBox))));
        pdf_obj* cb = pdf_dict_get(ctx, pageObj, PDF_NAME(CropBox));
        pbox.cropBox = cb ? FzRectToRectF(pdf_to_rect(ctx, cb)) : pbox.mediaBox;
        pdf_obj* bb = pdf_dict_get(ctx, pageObj, PDF_NAME(BleedBox));
        pbox.bleedBox = bb ? FzRectToRectF(pdf_to_rect(ctx, bb)) : pbox.cropBox;
        pdf_obj* tb = pdf_dict_get(ctx, pageObj, PDF_NAME(TrimBox));
        pbox.trimBox = tb ? FzRectToRectF(pdf_to_rect(ctx, tb)) : pbox.cropBox;
        pdf_obj* ab = pdf_dict_get(ctx, pageObj, PDF_NAME(ArtBox));
        pbox.artBox = ab ? FzRectToRectF(pdf_to_rect(ctx, ab)) : pbox.cropBox;
        VecAppend(report.pageBoxes, pbox);

        // Transparency on Page Group
        pdf_obj* grp = pdf_dict_get(ctx, pageObj, PDF_NAME(Group));
        if (grp && pdf_dict_get(ctx, grp, PDF_NAME(S)) == PDF_NAME(Transparency)) {
            report.hasTransparency = true;
        }

        pdf_obj* res = pdf_dict_get(ctx, pageObj, PDF_NAME(Resources));
        if (res && pdf_is_dict(ctx, res)) {
            // ExtGState transparency
            pdf_obj* egs = pdf_dict_get(ctx, res, PDF_NAME(ExtGState));
            if (egs && pdf_is_dict(ctx, egs)) {
                int n = pdf_dict_len(ctx, egs);
                for (int i = 0; i < n; i++) {
                    pdf_obj* gs = pdf_dict_get_val(ctx, egs, i);
                    if (!pdf_is_dict(ctx, gs)) continue;
                    pdf_obj* bm = pdf_dict_get(ctx, gs, PDF_NAME(BM));
                    if (bm && bm != PDF_NAME(Normal)) {
                        const char* bmName = pdf_to_name(ctx, bm);
                        if (!bmName || !str::Eq(Str(bmName), StrL("Compatible"))) {
                            report.hasTransparency = true;
                        }
                    }
                    pdf_obj* ca = pdf_dict_get(ctx, gs, PDF_NAME(ca));
                    if (ca && pdf_to_real(ctx, ca) < 1.0f) report.hasTransparency = true;
                    pdf_obj* caBig = pdf_dict_get(ctx, gs, PDF_NAME(CA));
                    if (caBig && pdf_to_real(ctx, caBig) < 1.0f) report.hasTransparency = true;
                    if (pdf_dict_gets(ctx, gs, "SMask")) report.hasTransparency = true;
                }
            }

            // Color spaces
            pdf_obj* cs = pdf_dict_get(ctx, res, PDF_NAME(ColorSpace));
            if (cs && pdf_is_dict(ctx, cs)) {
                int n = pdf_dict_len(ctx, cs);
                for (int i = 0; i < n; i++) {
                    pdf_obj* key = pdf_dict_get_key(ctx, cs, i);
                    const char* csName = pdf_to_name(ctx, key);
                    if (csName) {
                        AppendIfNotExists(&report.colorSpaces, Str(csName));
                    }
                }
            }

            // Fonts
            pdf_obj* fonts = pdf_dict_get(ctx, res, PDF_NAME(Font));
            if (fonts && pdf_is_dict(ctx, fonts)) {
                int n = pdf_dict_len(ctx, fonts);
                for (int i = 0; i < n; i++) {
                    pdf_obj* f = pdf_dict_get_val(ctx, fonts, i);
                    if (!pdf_is_dict(ctx, f)) continue;

                    pdf_obj* bf = pdf_dict_get(ctx, f, PDF_NAME(BaseFont));
                    const char* bfStr = bf ? pdf_to_name(ctx, bf) : "UnnamedFont";
                    pdf_obj* st = pdf_dict_get(ctx, f, PDF_NAME(Subtype));
                    const char* stStr = st ? pdf_to_name(ctx, st) : "Unknown";
                    pdf_obj* encObj = pdf_dict_get(ctx, f, PDF_NAME(Encoding));
                    const char* encStr = encObj ? (pdf_is_name(ctx, encObj) ? pdf_to_name(ctx, encObj) : "Custom") : "Standard";

                    pdf_obj* fd = pdf_dict_get(ctx, f, PDF_NAME(FontDescriptor));
                    bool embedded = false;
                    if (fd && pdf_is_dict(ctx, fd)) {
                        if (pdf_dict_get(ctx, fd, PDF_NAME(FontFile)) ||
                            pdf_dict_get(ctx, fd, PDF_NAME(FontFile2)) ||
                            pdf_dict_get(ctx, fd, PDF_NAME(FontFile3))) {
                            embedded = true;
                        }
                    }

                    Str baseStr = Str(bfStr);
                    bool isSubset = str::ContainsChar(baseStr, '+');

                    // Check if already in list
                    int existingIdx = -1;
                    for (int k = 0; k < len(report.fonts); k++) {
                        if (str::Eq(report.fonts[k].baseFont, baseStr)) {
                            existingIdx = k;
                            break;
                        }
                    }
                    if (existingIdx >= 0) {
                        if (!VecContains(report.fonts[existingIdx].pages, p + 1)) {
                            VecAppend(report.fonts[existingIdx].pages, p + 1);
                        }
                    } else {
                        PreflightFontInfo fi;
                        fi.baseFont = str::Dup(baseStr);
                        fi.subtype = str::Dup(Str(stStr));
                        fi.encoding = str::Dup(Str(encStr));
                        fi.isEmbedded = embedded;
                        fi.isSubset = isSubset;
                        VecAppend(fi.pages, p + 1);
                        VecAppend(report.fonts, fi);
                    }
                }
            }

            // Images
            pdf_obj* xobj = pdf_dict_get(ctx, res, PDF_NAME(XObject));
            if (xobj && pdf_is_dict(ctx, xobj)) {
                int n = pdf_dict_len(ctx, xobj);
                for (int i = 0; i < n; i++) {
                    pdf_obj* item = pdf_dict_get_val(ctx, xobj, i);
                    if (!pdf_is_dict(ctx, item)) continue;
                    if (pdf_dict_get(ctx, item, PDF_NAME(Subtype)) != PDF_NAME(Image)) continue;

                    PreflightImageInfo img;
                    img.pageNo = p + 1;
                    img.objNum = pdf_to_num(ctx, item);
                    img.width = pdf_to_int(ctx, pdf_dict_get(ctx, item, PDF_NAME(Width)));
                    img.height = pdf_to_int(ctx, pdf_dict_get(ctx, item, PDF_NAME(Height)));
                    img.bitsPerComponent = pdf_to_int(ctx, pdf_dict_get(ctx, item, PDF_NAME(BitsPerComponent)));
                    if (img.bitsPerComponent <= 0) img.bitsPerComponent = 8;

                    pdf_obj* ics = pdf_dict_get(ctx, item, PDF_NAME(ColorSpace));
                    const char* csStr = ics ? (pdf_is_name(ctx, ics) ? pdf_to_name(ctx, ics) : "Complex") : "DeviceRGB";
                    img.colorSpace = str::Dup(Str(csStr));

                    pdf_obj* flt = pdf_dict_get(ctx, item, PDF_NAME(Filter));
                    const char* fltStr = flt ? (pdf_is_name(ctx, flt) ? pdf_to_name(ctx, flt) : "FlateDecode") : "Raw";
                    img.filter = str::Dup(Str(fltStr));

                    // Default displayed size approximation
                    img.ptWidth = pbox.mediaBox.dx > 0 ? pbox.mediaBox.dx * 0.5f : 300.f;
                    img.ptHeight = pbox.mediaBox.dy > 0 ? pbox.mediaBox.dy * 0.5f : 300.f;

                    if (img.ptWidth > 0 && img.width > 0) {
                        img.dpiX = (int)((float)img.width / (img.ptWidth / 72.0f));
                    }
                    if (img.ptHeight > 0 && img.height > 0) {
                        img.dpiY = (int)((float)img.height / (img.ptHeight / 72.0f));
                    }

                    VecAppend(report.images, img);
                }
            }
        }

        fz_drop_page(ctx, (fz_page*)page);
    }

    if (len(report.colorSpaces) == 0) {
        report.colorSpaces.Append(StrL("DeviceRGB"));
    }

    return report;
}

PreflightReport RunPreflightFile(Str filePath) {
    PreflightReport report;
    if (len(filePath) == 0) return report;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return report;
    fz_register_document_handlers(ctx);

    pdf_document* doc = nullptr;
    fz_var(doc);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(filePath));
        report = RunPreflight(ctx, doc);
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        FreePreflightReport(report);
    }
    fz_drop_context(ctx);
    return report;
}

bool ExportPreflightReport(const PreflightReport& report, Str destPath, bool isCsv) {
    if (len(destPath) == 0) return false;
    str::Builder b;

    if (isCsv) {
        b.Append(StrL("# Preflight Summary\n"));
        b.Append(StrL("Property,Value\n"));
        b.Append(fmt("PDF Version,%d.%d\n", report.pdfMajorVersion, report.pdfMinorVersion));
        b.Append(fmt("Page Count,%d\n", report.pageCount));
        b.Append(fmt("Encrypted,%s\n", Str(report.isEncrypted ? "Yes" : "No")));
        b.Append(fmt("Encryption Method,%s\n", report.encryptionMethod));
        b.Append(fmt("JavaScript,%s\n", Str(report.hasJavaScript ? "Yes" : "No")));
        b.Append(fmt("Transparency,%s\n", Str(report.hasTransparency ? "Yes" : "No")));
        b.Append(StrL("\n# Fonts\n"));
        b.Append(StrL("BaseFont,Subtype,Encoding,Embedded,Subset,Pages\n"));
        for (const auto& f : report.fonts) {
            str::Builder pagesStr;
            for (int i = 0; i < len(f.pages); i++) {
                if (i > 0) pagesStr.Append(StrL(";"));
                pagesStr.Append(fmt("%d", f.pages[i]));
            }
            TempStr pgTemp = pagesStr.TakeStr();
            b.Append(fmt("\"%s\",\"%s\",\"%s\",%s,%s,\"%s\"\n", f.baseFont, f.subtype, f.encoding,
                         Str(f.isEmbedded ? "Yes" : "No"), Str(f.isSubset ? "Yes" : "No"), pgTemp));
            str::Free(pgTemp);
        }

        b.Append(StrL("\n# Images\n"));
        b.Append(StrL("Page,ObjNum,Width,Height,DPI_X,DPI_Y,ColorSpace,BPC,Filter\n"));
        for (const auto& img : report.images) {
            b.Append(fmt("%d,%d,%d,%d,%d,%d,\"%s\",%d,\"%s\"\n", img.pageNo, img.objNum, img.width, img.height,
                         img.dpiX, img.dpiY, img.colorSpace, img.bitsPerComponent, img.filter));
        }

        b.Append(StrL("\n# Page Boxes\n"));
        b.Append(StrL("Page,MediaBox,CropBox,BleedBox,TrimBox,ArtBox\n"));
        for (const auto& pb : report.pageBoxes) {
            b.Append(fmt("%d,\"%.1f %.1f %.1f %.1f\",\"%.1f %.1f %.1f %.1f\",\"%.1f %.1f %.1f %.1f\",\"%.1f %.1f "
                         "%.1f %.1f\",\"%.1f %.1f %.1f %.1f\"\n",
                         pb.pageNo, pb.mediaBox.x, pb.mediaBox.y, pb.mediaBox.dx, pb.mediaBox.dy, pb.cropBox.x,
                         pb.cropBox.y, pb.cropBox.dx, pb.cropBox.dy, pb.bleedBox.x, pb.bleedBox.y, pb.bleedBox.dx,
                         pb.bleedBox.dy, pb.trimBox.x, pb.trimBox.y, pb.trimBox.dx, pb.trimBox.dy, pb.artBox.x,
                         pb.artBox.y, pb.artBox.dx, pb.artBox.dy));
        }
    } else {
        b.Append(StrL("================================================================================\n"));
        b.Append(StrL("                      SWIFTLEAF PDF PREFLIGHT REPORT                            \n"));
        b.Append(StrL("================================================================================\n\n"));
        b.Append(fmt("PDF Version:         %d.%d\n", report.pdfMajorVersion, report.pdfMinorVersion));
        b.Append(fmt("Page Count:          %d\n", report.pageCount));
        b.Append(fmt("Encrypted:           %s (%s)\n", Str(report.isEncrypted ? "Yes" : "No"), report.encryptionMethod));
        b.Append(fmt("JavaScript:          %s\n", Str(report.hasJavaScript ? "Yes" : "No")));
        b.Append(fmt("Transparency:        %s\n", Str(report.hasTransparency ? "Yes" : "No")));

        b.Append(StrL("Color Spaces:        "));
        for (int i = 0; i < len(report.colorSpaces); i++) {
            if (i > 0) b.Append(StrL(", "));
            b.Append(report.colorSpaces[i]);
        }
        b.Append(StrL("\n\n"));

        b.Append(StrL("--------------------------------------------------------------------------------\n"));
        b.Append(fmt("FONTS (%d total)\n", len(report.fonts)));
        b.Append(StrL("--------------------------------------------------------------------------------\n"));
        for (const auto& f : report.fonts) {
            b.Append(fmt("  - %s\n", f.baseFont));
            b.Append(fmt("      Type: %s | Encoding: %s | Embedded: %s | Subset: %s\n", f.subtype, f.encoding,
                         Str(f.isEmbedded ? "Yes" : "No"), Str(f.isSubset ? "Yes" : "No")));
        }
        b.Append(StrL("\n"));

        b.Append(StrL("--------------------------------------------------------------------------------\n"));
        b.Append(fmt("IMAGES (%d total)\n", len(report.images)));
        b.Append(StrL("--------------------------------------------------------------------------------\n"));
        for (const auto& img : report.images) {
            b.Append(fmt("  - Page %d, Obj %d: %dx%d px (%d DPI), %s, %d bpc, %s\n", img.pageNo, img.objNum, img.width,
                         img.height, img.dpiX, img.colorSpace, img.bitsPerComponent, img.filter));
        }
        b.Append(StrL("\n"));

        b.Append(StrL("--------------------------------------------------------------------------------\n"));
        b.Append(StrL("PAGE BOXES (First page sample)\n"));
        b.Append(StrL("--------------------------------------------------------------------------------\n"));
        if (len(report.pageBoxes) > 0) {
            const auto& pb = report.pageBoxes[0];
            b.Append(fmt("  MediaBox: [%.1f, %.1f, %.1f, %.1f]\n", pb.mediaBox.x, pb.mediaBox.y, pb.mediaBox.dx,
                         pb.mediaBox.dy));
            b.Append(fmt("  CropBox:  [%.1f, %.1f, %.1f, %.1f]\n", pb.cropBox.x, pb.cropBox.y, pb.cropBox.dx,
                         pb.cropBox.dy));
            b.Append(fmt("  BleedBox: [%.1f, %.1f, %.1f, %.1f]\n", pb.bleedBox.x, pb.bleedBox.y, pb.bleedBox.dx,
                         pb.bleedBox.dy));
            b.Append(fmt("  TrimBox:  [%.1f, %.1f, %.1f, %.1f]\n", pb.trimBox.x, pb.trimBox.y, pb.trimBox.dx,
                         pb.trimBox.dy));
            b.Append(fmt("  ArtBox:   [%.1f, %.1f, %.1f, %.1f]\n", pb.artBox.x, pb.artBox.y, pb.artBox.dx,
                         pb.artBox.dy));
        }
    }

    TempStr res = b.TakeStr();
    bool ok = file::WriteFile(destPath, res);
    str::Free(res);
    return ok;
}

// PDF/A Conversion & Validation
bool ConvertPdfToPdfA(Str srcPath, Str destPath, int part) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);

    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        pdf_obj* trailer = pdf_trailer(ctx, doc);
        pdf_obj* root = trailer ? pdf_dict_get(ctx, trailer, PDF_NAME(Root)) : nullptr;
        if (!root) fz_throw(ctx, FZ_ERROR_GENERIC, "no catalog");

        // 1. Strip forbidden features: JavaScript, Encryption, Launch Actions
        pdf_dict_del(ctx, trailer, PDF_NAME(Encrypt));
        pdf_dict_del(ctx, root, PDF_NAME(Names));
        pdf_dict_del(ctx, root, pdf_new_name(ctx, "OpenAction"));
        pdf_dict_del(ctx, root, PDF_NAME(AA));

        int numPages = pdf_count_pages(ctx, doc);
        for (int p = 0; p < numPages; p++) {
            pdf_page* page = pdf_load_page(ctx, doc, p);
            if (!page) continue;
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, p);
            pdf_dict_del(ctx, pageObj, PDF_NAME(AA));

            // Strip transparency for PDF/A-1b
            if (part == 1) {
                pdf_dict_del(ctx, pageObj, PDF_NAME(Group));
                pdf_obj* res = pdf_dict_get(ctx, pageObj, PDF_NAME(Resources));
                if (res) {
                    pdf_obj* egs = pdf_dict_get(ctx, res, PDF_NAME(ExtGState));
                    if (egs && pdf_is_dict(ctx, egs)) {
                        int n = pdf_dict_len(ctx, egs);
                        for (int k = 0; k < n; k++) {
                            pdf_obj* gs = pdf_dict_get_val(ctx, egs, k);
                            if (!pdf_is_dict(ctx, gs)) continue;
                            pdf_dict_del(ctx, gs, pdf_new_name(ctx, "SMask"));
                            pdf_dict_put(ctx, gs, PDF_NAME(BM), PDF_NAME(Normal));
                            pdf_dict_put_real(ctx, gs, PDF_NAME(ca), 1.0);
                            pdf_dict_put_real(ctx, gs, PDF_NAME(CA), 1.0);
                        }
                    }
                }
            }

            // Remove external/launch actions on annotations
            pdf_annot* a = pdf_first_annot(ctx, page);
            while (a) {
                pdf_annot* next = pdf_next_annot(ctx, a);
                pdf_obj* aObj = pdf_annot_obj(ctx, a);
                if (aObj) {
                    pdf_dict_del(ctx, aObj, PDF_NAME(AA));
                    pdf_obj* act = pdf_dict_get(ctx, aObj, PDF_NAME(A));
                    if (act) {
                        pdf_obj* s = pdf_dict_get(ctx, act, PDF_NAME(S));
                        const char* sName = s ? pdf_to_name(ctx, s) : nullptr;
                        if (sName && (str::Eq(Str(sName), StrL("Launch")) || str::Eq(Str(sName), StrL("JavaScript")) ||
                                      str::Eq(Str(sName), StrL("ImportData")))) {
                            pdf_delete_annot(ctx, page, a);
                        }
                    }
                }
                a = next;
            }

            fz_drop_page(ctx, (fz_page*)page);
        }

        // 2. Add XMP Metadata with pdfaid
        TempStr xmp = fmt(
            "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
            "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
            " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
            "  <rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\">\n"
            "   <pdfaid:part>%d</pdfaid:part>\n"
            "   <pdfaid:conformance>B</pdfaid:conformance>\n"
            "  </rdf:Description>\n"
            " </rdf:RDF>\n"
            "</x:xmpmeta>\n"
            "<?xpacket end=\"w\"?>\n",
            part);

        fz_buffer* xmpBuf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)xmp.s, (size_t)xmp.len);
        pdf_obj* metaStream = pdf_add_stream(ctx, doc, xmpBuf, nullptr, 0);
        fz_drop_buffer(ctx, xmpBuf);

        pdf_dict_put(ctx, metaStream, PDF_NAME(Type), PDF_NAME(Metadata));
        pdf_dict_put(ctx, metaStream, PDF_NAME(Subtype), PDF_NAME(XML));
        pdf_dict_put(ctx, root, PDF_NAME(Metadata), metaStream);

        // 3. Add OutputIntent with sRGB ICC profile
        Str iccPath = StrL("C:\\Windows\\System32\\spool\\drivers\\color\\sRGB Color Space Profile.icm");
        Str iccData{};
        if (file::Exists(iccPath)) {
            iccData = file::ReadFile(iccPath);
        }
        if (iccData.len == 0) {
            iccPath = StrL("ext\\mupdf\\resources\\icc\\rgb.icc");
            if (file::Exists(iccPath)) {
                iccData = file::ReadFile(iccPath);
            }
        }

        pdf_obj* iccStream = nullptr;
        if (iccData.len > 0) {
            fz_buffer* iccBuf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)iccData.s, (size_t)iccData.len);
            iccStream = pdf_add_stream(ctx, doc, iccBuf, nullptr, 0);
            fz_drop_buffer(ctx, iccBuf);
            pdf_dict_put_int(ctx, iccStream, PDF_NAME(N), 3);
            str::Free(iccData);
        } else {
            fz_buffer* iccBuf = fz_new_buffer(ctx, 64);
            fz_append_string(ctx, iccBuf, "sRGB Profile");
            iccStream = pdf_add_stream(ctx, doc, iccBuf, nullptr, 0);
            fz_drop_buffer(ctx, iccBuf);
            pdf_dict_put_int(ctx, iccStream, PDF_NAME(N), 3);
        }

        pdf_obj* outIntent = pdf_new_dict(ctx, doc, 6);
        pdf_dict_put(ctx, outIntent, PDF_NAME(Type), PDF_NAME(OutputIntent));
        pdf_dict_puts(ctx, outIntent, "S", pdf_new_name(ctx, "GTS_PDFA1"));
        pdf_dict_put_text_string(ctx, outIntent, pdf_new_name(ctx, "OutputConditionIdentifier"), "sRGB IEC61966-2.1");
        pdf_dict_put_text_string(ctx, outIntent, pdf_new_name(ctx, "RegistryName"), "http://www.color.org");
        pdf_dict_put_text_string(ctx, outIntent, PDF_NAME(Info), "sRGB IEC61966-2.1");
        if (iccStream) {
            pdf_dict_puts(ctx, outIntent, "DestOutputProfile", iccStream);
        }

        pdf_obj* outIntents = pdf_new_array(ctx, doc, 1);
        pdf_array_push(ctx, outIntents, outIntent);
        pdf_dict_puts(ctx, root, "OutputIntents", outIntents);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        ok = false;
    }
    fz_drop_context(ctx);
    return ok;
}

bool ValidatePdfA(Str filePath, int part, StrVec& violationsOut) {
    if (len(filePath) == 0) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);

    pdf_document* doc = nullptr;
    fz_var(doc);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(filePath));
        pdf_obj* trailer = pdf_trailer(ctx, doc);
        pdf_obj* root = trailer ? pdf_dict_get(ctx, trailer, PDF_NAME(Root)) : nullptr;
        if (!root) {
            violationsOut.Append(StrL("Invalid PDF: Cannot read document catalog"));
            fz_throw(ctx, FZ_ERROR_GENERIC, "corrupt");
        }

        // 1. Encryption forbidden
        if (trailer && pdf_dict_get(ctx, trailer, PDF_NAME(Encrypt))) {
            violationsOut.Append(StrL("Encryption is forbidden in PDF/A"));
        }

        // 2. XMP Metadata with pdfaid
        pdf_obj* meta = pdf_dict_get(ctx, root, PDF_NAME(Metadata));
        if (!meta) {
            violationsOut.Append(StrL("Missing XMP document metadata stream (/Metadata)"));
        } else {
            fz_buffer* b = pdf_load_stream(ctx, meta);
            bool hasPdfaid = false;
            if (b) {
                unsigned char* data = nullptr;
                size_t dlen = fz_buffer_storage(ctx, b, &data);
                if (data && str::Contains(Str{(const char*)data, (int)dlen}, StrL("pdfaid:part"))) {
                    hasPdfaid = true;
                }
                fz_drop_buffer(ctx, b);
            }
            if (!hasPdfaid) {
                violationsOut.Append(StrL("XMP metadata lacks pdfaid:part identification"));
            }
        }

        // 3. OutputIntents with DestOutputProfile
        pdf_obj* outIntents = pdf_dict_gets(ctx, root, "OutputIntents");
        if (!outIntents || !pdf_is_array(ctx, outIntents) || pdf_array_len(ctx, outIntents) == 0) {
            violationsOut.Append(StrL("Missing PDF/A OutputIntent (/OutputIntents)"));
        } else {
            pdf_obj* oi = pdf_array_get(ctx, outIntents, 0);
            if (!oi || !pdf_dict_gets(ctx, oi, "DestOutputProfile")) {
                violationsOut.Append(StrL("OutputIntent missing DestOutputProfile ICC profile"));
            }
        }

        // 4. JavaScript forbidden
        pdf_obj* names = pdf_dict_get(ctx, root, PDF_NAME(Names));
        if (names && pdf_dict_get(ctx, names, PDF_NAME(JavaScript))) {
            violationsOut.Append(StrL("Embedded JavaScript is forbidden in PDF/A"));
        }

        // 5. Fonts embedding & Transparency
        PreflightReport rep = RunPreflight(ctx, doc);
        for (const auto& f : rep.fonts) {
            if (!f.isEmbedded) {
                TempStr msg = fmt("Font '%s' is not embedded", f.baseFont);
                violationsOut.Append(msg);
            }
        }

        if (part == 1 && rep.hasTransparency) {
            violationsOut.Append(StrL("Transparency features detected (forbidden in PDF/A-1)"));
        }
        FreePreflightReport(rep);
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {}
    fz_drop_context(ctx);
    return len(violationsOut) == 0;
}

// Convert Colors (Grayscale or RGB)
bool PdfConvertColors(Str srcPath, Str destPath, int numComp) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);

    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        pdf_recolor_options ropts{numComp};
        for (int i = 0; i < numPages; i++) {
            pdf_recolor_page(ctx, doc, i, &ropts);
        }
        if (numComp == 1) {
            pdf_remove_output_intents(ctx, doc);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        ok = false;
    }
    fz_drop_context(ctx);
    return ok;
}

// Dialog: Preflight
struct PreflightDialog : WindowBase {
    MainWindow* win = nullptr;
    Str srcPath;
    PreflightReport report;

    VBox* mainBox = nullptr;
    VirtListBox* listBox = nullptr;
    ListBoxModelStrings* model = nullptr;
    VirtButton* exportBtn = nullptr;
    VirtButton* grayBtn = nullptr;
    VirtButton* rgbBtn = nullptr;
    VirtButton* closeBtn = nullptr;

    ~PreflightDialog() override {
        FreePreflightReport(report);
        str::Free(srcPath);
    }

    bool Create(MainWindow* w) {
        win = w;
        WindowTab* tab = win ? win->CurrentTab() : nullptr;
        if (!tab || len(tab->filePath) == 0) return false;
        srcPath = str::Dup(tab->filePath);

        report = RunPreflightFile(srcPath);

        PlatformFont* dialogFont = GetDefaultGuiFont();
        closeOnEsc = true;

        CreateCustomArgs cargs;
        cargs.owner = win ? win->hwndFrame : nullptr;
        cargs.title = Tr("Preflight & Document Standards");
        cargs.font = dialogFont;
        cargs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        cargs.visible = false;
        cargs.icon = GetAppIcon();
        cargs.bgColor = DarkModeDialogBgColor();
        CreateCustom(cargs);
        if (!hwnd) return false;

        mainBox = new VBox();
        mainBox->alignMain = MainAxisAlign::MainStart;
        mainBox->alignCross = CrossAxisAlign::Stretch;

        TempStr header = str::FormatTemp(Tr("Preflight Analysis: %s (PDF %d.%d, %d pages)").s,
                                         path::GetBaseNameTemp(srcPath), report.pdfMajorVersion,
                                         report.pdfMinorVersion, report.pageCount);
        mainBox->AddChild(NewVirtText({.s = header, .font = font, .isRtl = IsUIRtl()}));

        listBox = new VirtListBox();
        listBox->dpi = GetDpi();
        listBox->font = font;
        listBox->idealSizeLines = 14;
        model = new ListBoxModelStrings();

        model->strings.Append(fmt("PDF Version: %d.%d", report.pdfMajorVersion, report.pdfMinorVersion));
        model->strings.Append(fmt("Encrypted: %s (%s)", Str(report.isEncrypted ? "Yes" : "No"), report.encryptionMethod));
        model->strings.Append(fmt("JavaScript: %s", Str(report.hasJavaScript ? "Yes" : "No")));
        model->strings.Append(fmt("Transparency: %s", Str(report.hasTransparency ? "Yes" : "No")));

        str::Builder csB;
        csB.Append(StrL("Color Spaces: "));
        for (int i = 0; i < len(report.colorSpaces); i++) {
            if (i > 0) csB.Append(StrL(", "));
            csB.Append(report.colorSpaces[i]);
        }
        TempStr csTemp = csB.TakeStr();
        model->strings.Append(csTemp);
        str::Free(csTemp);

        model->strings.Append(fmt("--- FONTS (%d total) ---", len(report.fonts)));
        for (const auto& f : report.fonts) {
            model->strings.Append(fmt("  Font: %s (%s, Embedded: %s)", f.baseFont, f.subtype,
                                      Str(f.isEmbedded ? "Yes" : "No")));
        }

        model->strings.Append(fmt("--- IMAGES (%d total) ---", len(report.images)));
        for (const auto& img : report.images) {
            model->strings.Append(fmt("  Page %d: %dx%d px (%d DPI), %s", img.pageNo, img.width, img.height,
                                      img.dpiX, img.colorSpace));
        }

        model->strings.Append(fmt("--- PAGE BOXES (%d pages) ---", len(report.pageBoxes)));
        if (len(report.pageBoxes) > 0) {
            const auto& pb = report.pageBoxes[0];
            model->strings.Append(fmt("  Page 1 MediaBox: [%.1f, %.1f, %.1f, %.1f]", pb.mediaBox.x, pb.mediaBox.y,
                                      pb.mediaBox.dx, pb.mediaBox.dy));
            model->strings.Append(fmt("  Page 1 CropBox:  [%.1f, %.1f, %.1f, %.1f]", pb.cropBox.x, pb.cropBox.y,
                                      pb.cropBox.dx, pb.cropBox.dy));
        }

        listBox->SetModel(model);
        mainBox->AddChild(listBox);

        auto* btnBox = new HBox();
        btnBox->alignMain = MainAxisAlign::MainEnd;
        btnBox->alignCross = CrossAxisAlign::CrossCenter;
        btnBox->gap = font->averageCharWidth;

        exportBtn = NewThemedButton(hwnd, Tr("Export Report..."), font, false);
        exportBtn->onClick = MkMethod1<PreflightDialog, VirtMouseEvent*, &PreflightDialog::OnExport>(this);
        btnBox->AddChild(exportBtn);

        grayBtn = NewThemedButton(hwnd, Tr("To Grayscale"), font, false);
        grayBtn->onClick = MkMethod1<PreflightDialog, VirtMouseEvent*, &PreflightDialog::OnGrayscale>(this);
        btnBox->AddChild(grayBtn);

        rgbBtn = NewThemedButton(hwnd, Tr("To RGB"), font, false);
        rgbBtn->onClick = MkMethod1<PreflightDialog, VirtMouseEvent*, &PreflightDialog::OnRgb>(this);
        btnBox->AddChild(rgbBtn);

        closeBtn = NewThemedButton(hwnd, Tr("Close"), font, true);
        closeBtn->onClick = MkMethod1<PreflightDialog, VirtMouseEvent*, &PreflightDialog::OnCloseBtn>(this);
        btnBox->AddChild(closeBtn);

        mainBox->AddChild(btnBox);

        auto* padding = new Padding(mainBox, DpiScaledInsets(8, 12));
        layout = padding;

        int dx = DpiScale(560);
        LayoutAndSizeToContent(layout, dx, 0, hwnd);
        DoLayout(HwndClientRect(hwnd).Size());
        HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
        UpdateTheme();
        SetIsVisible(true);
        return true;
    }

    void OnExport(VirtMouseEvent*) {
        WCHAR dstFileName[MAX_PATH + 1]{};
        Str stem = srcPath;
        if (str::EndsWithI(stem, StrL(".pdf"))) stem.len -= 4;
        TempStr defName = fmt("%s_preflight.txt", stem);
        TempWStr current = ToWStrTemp(defName);
        wstr::BufSet(WStr(dstFileName, MAX_PATH), current);

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = dstFileName;
        ofn.nMaxFile = dimof(dstFileName);
        ofn.lpstrFilter = L"Text Reports (*.txt)\0*.txt\0CSV Reports (*.csv)\0*.csv\0All Files\0*.*\0";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        ofn.lpstrDefExt = L"txt";

        if (GetSaveFileNameW(&ofn)) {
            TempStr outPath = ToUtf8Temp(dstFileName);
            bool isCsv = str::EndsWithI(outPath, StrL(".csv"));
            if (ExportPreflightReport(report, outPath, isCsv)) {
                MessageBoxWarning(hwnd, Tr("Preflight report exported successfully."), Tr("Preflight"));
            } else {
                MessageBoxWarning(hwnd, Tr("Failed to export preflight report."), Tr("Error"));
            }
        }
    }

    void OnGrayscale(VirtMouseEvent*) {
        ScheduleDelete();
        ShowPdfConvertColorsDialog(win);
    }

    void OnRgb(VirtMouseEvent*) {
        ScheduleDelete();
        ShowPdfConvertColorsDialog(win);
    }

    void OnCloseBtn(VirtMouseEvent*) {
        ScheduleDelete();
    }
};

void ShowPreflightDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new PreflightDialog();
    if (!dlg->Create(win)) delete dlg;
}

// Dialog: Convert to PDF/A
struct PdfAConvertDialog : WindowBase {
    MainWindow* win = nullptr;
    Str srcPath;

    VBox* mainBox = nullptr;
    DropDown* partDrop = nullptr;
    Edit* destEdit = nullptr;
    VirtButton* browseBtn = nullptr;
    VirtButton* convertBtn = nullptr;
    VirtButton* cancelBtn = nullptr;

    ~PdfAConvertDialog() override {
        str::Free(srcPath);
    }

    bool Create(MainWindow* w) {
        win = w;
        WindowTab* tab = win ? win->CurrentTab() : nullptr;
        if (!tab || len(tab->filePath) == 0) return false;
        srcPath = str::Dup(tab->filePath);

        PlatformFont* dialogFont = GetDefaultGuiFont();
        closeOnEsc = true;

        CreateCustomArgs cargs;
        cargs.owner = win ? win->hwndFrame : nullptr;
        cargs.title = Tr("Convert to PDF/A");
        cargs.font = dialogFont;
        cargs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        cargs.visible = false;
        cargs.icon = GetAppIcon();
        cargs.bgColor = DarkModeDialogBgColor();
        CreateCustom(cargs);
        if (!hwnd) return false;

        mainBox = new VBox();
        mainBox->alignMain = MainAxisAlign::MainStart;
        mainBox->alignCross = CrossAxisAlign::Stretch;

        mainBox->AddChild(NewVirtText({.s = Tr("Select PDF/A Conformance Level:"), .font = font, .isRtl = IsUIRtl()}));

        partDrop = new DropDown();
        DropDown::CreateArgs dargs;
        dargs.parent = hwnd;
        dargs.font = font;
        partDrop->Create(dargs);
        StrVec partItems;
        partItems.Append(Tr("PDF/A-1b (ISO 19005-1, PDF 1.4 baseline, flattens transparency)"));
        partItems.Append(Tr("PDF/A-2b (ISO 19005-2, PDF 1.7 baseline, preserves transparency)"));
        partDrop->SetItems(partItems);
        CbSetCurrentSelection(partDrop, 1);
        mainBox->AddChild(partDrop);

        mainBox->AddChild(NewVirtText({.s = Tr("Save converted PDF/A to:"), .font = font, .isRtl = IsUIRtl()}));

        auto* destRow = new HBox();
        destRow->alignCross = CrossAxisAlign::CrossCenter;
        destRow->gap = font->averageCharWidth;

        Str stem = srcPath;
        if (str::EndsWithI(stem, StrL(".pdf"))) stem.len -= 4;
        TempStr defDest = fmt("%s_pdfa.pdf", stem);
        Edit::CreateArgs eargs;
        eargs.parent = hwnd;
        eargs.font = font;
        eargs.withBorder = true;
        eargs.isRtl = IsUIRtl();
        eargs.text = defDest;
        destEdit = new Edit();
        destEdit->Create(eargs);
        destRow->AddChild(destEdit, 1);

        browseBtn = NewThemedButton(hwnd, Tr("Browse..."), font, false);
        browseBtn->onClick = MkMethod1<PdfAConvertDialog, VirtMouseEvent*, &PdfAConvertDialog::OnBrowse>(this);
        destRow->AddChild(browseBtn);
        mainBox->AddChild(destRow);

        mainBox->AddChild(NewVirtText(
            {.s = Tr("Note: This is a best-effort PDF/A conversion and is not veraPDF-certified."), .font = font, .isRtl = IsUIRtl()}));

        auto* btnBox = new HBox();
        btnBox->alignMain = MainAxisAlign::MainEnd;
        btnBox->alignCross = CrossAxisAlign::CrossCenter;
        btnBox->gap = font->averageCharWidth;

        convertBtn = NewThemedButton(hwnd, Tr("Convert"), font, true);
        convertBtn->onClick = MkMethod1<PdfAConvertDialog, VirtMouseEvent*, &PdfAConvertDialog::OnConvert>(this);
        btnBox->AddChild(convertBtn);

        cancelBtn = NewThemedButton(hwnd, Tr("Cancel"), font, false);
        cancelBtn->onClick = MkMethod1<PdfAConvertDialog, VirtMouseEvent*, &PdfAConvertDialog::OnCancel>(this);
        btnBox->AddChild(cancelBtn);
        mainBox->AddChild(btnBox);

        auto* padding = new Padding(mainBox, DpiScaledInsets(8, 12));
        layout = padding;

        int dx = DpiScale(540);
        LayoutAndSizeToContent(layout, dx, 0, hwnd);
        DoLayout(HwndClientRect(hwnd).Size());
        HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
        UpdateTheme();
        SetIsVisible(true);
        return true;
    }

    void OnBrowse(VirtMouseEvent*) {
        WCHAR dstFileName[MAX_PATH + 1]{};
        TempWStr current = ToWStrTemp(HwndGetTextTemp(destEdit->hwnd));
        wstr::BufSet(WStr(dstFileName, MAX_PATH), current);

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = dstFileName;
        ofn.nMaxFile = dimof(dstFileName);
        ofn.lpstrFilter = L"PDF Files\0*.pdf\0All Files\0*.*\0";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        ofn.lpstrDefExt = L"pdf";

        if (GetSaveFileNameW(&ofn)) {
            destEdit->SetText(ToUtf8Temp(dstFileName));
        }
    }

    void OnConvert(VirtMouseEvent*) {
        TempStr dest = destEdit->GetTextTemp();
        if (len(dest) == 0) return;
        if (path::IsSame(dest, srcPath)) {
            MessageBoxWarning(hwnd, Tr("Please choose a different destination file name."), Tr("Save Copy"));
            return;
        }

        int part = CbGetCurrentSelection(partDrop) == 0 ? 1 : 2;
        bool ok = ConvertPdfToPdfA(srcPath, dest, part);
        if (!ok) {
            MessageBoxWarning(hwnd, Tr("Failed to convert PDF to PDF/A."), Tr("Error"));
            return;
        }

        ScheduleDelete();
        if (win) {
            LoadArgs largs(dest, win);
            StartLoadDocument(&largs);
        }
    }

    void OnCancel(VirtMouseEvent*) {
        ScheduleDelete();
    }
};

void ShowPdfAConvertDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new PdfAConvertDialog();
    if (!dlg->Create(win)) delete dlg;
}

// Dialog: Validate PDF/A
struct PdfAValidateDialog : WindowBase {
    MainWindow* win = nullptr;
    Str srcPath;
    StrVec violations;

    VBox* mainBox = nullptr;
    DropDown* partDrop = nullptr;
    VirtListBox* listBox = nullptr;
    ListBoxModelStrings* model = nullptr;
    VirtButton* validateBtn = nullptr;
    VirtButton* closeBtn = nullptr;

    ~PdfAValidateDialog() override {
        str::Free(srcPath);
    }

    bool Create(MainWindow* w) {
        win = w;
        WindowTab* tab = win ? win->CurrentTab() : nullptr;
        if (!tab || len(tab->filePath) == 0) return false;
        srcPath = str::Dup(tab->filePath);

        PlatformFont* dialogFont = GetDefaultGuiFont();
        closeOnEsc = true;

        CreateCustomArgs cargs;
        cargs.owner = win ? win->hwndFrame : nullptr;
        cargs.title = Tr("Validate PDF/A");
        cargs.font = dialogFont;
        cargs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        cargs.visible = false;
        cargs.icon = GetAppIcon();
        cargs.bgColor = DarkModeDialogBgColor();
        CreateCustom(cargs);
        if (!hwnd) return false;

        mainBox = new VBox();
        mainBox->alignMain = MainAxisAlign::MainStart;
        mainBox->alignCross = CrossAxisAlign::Stretch;

        auto* topRow = new HBox();
        topRow->alignCross = CrossAxisAlign::CrossCenter;
        topRow->gap = font->averageCharWidth;
        topRow->AddChild(NewVirtText({.s = Tr("Conformance:"), .font = font, .isRtl = IsUIRtl()}));

        partDrop = new DropDown();
        DropDown::CreateArgs dargs;
        dargs.parent = hwnd;
        dargs.font = font;
        partDrop->Create(dargs);
        StrVec partItems;
        partItems.Append(StrL("PDF/A-1b"));
        partItems.Append(StrL("PDF/A-2b"));
        partDrop->SetItems(partItems);
        CbSetCurrentSelection(partDrop, 1);
        topRow->AddChild(partDrop, 1);

        validateBtn = NewThemedButton(hwnd, Tr("Check"), font, false);
        validateBtn->onClick = MkMethod1<PdfAValidateDialog, VirtMouseEvent*, &PdfAValidateDialog::OnCheck>(this);
        topRow->AddChild(validateBtn);
        mainBox->AddChild(topRow);

        listBox = new VirtListBox();
        listBox->dpi = GetDpi();
        listBox->font = font;
        listBox->idealSizeLines = 12;
        model = new ListBoxModelStrings();
        listBox->SetModel(model);
        mainBox->AddChild(listBox);

        mainBox->AddChild(NewVirtText(
            {.s = Tr("Note: This validator is best-effort and not veraPDF-certified."), .font = font, .isRtl = IsUIRtl()}));

        auto* btnBox = new HBox();
        btnBox->alignMain = MainAxisAlign::MainEnd;
        btnBox->alignCross = CrossAxisAlign::CrossCenter;
        btnBox->gap = font->averageCharWidth;

        closeBtn = NewThemedButton(hwnd, Tr("Close"), font, true);
        closeBtn->onClick = MkMethod1<PdfAValidateDialog, VirtMouseEvent*, &PdfAValidateDialog::OnCloseBtn>(this);
        btnBox->AddChild(closeBtn);
        mainBox->AddChild(btnBox);

        auto* padding = new Padding(mainBox, DpiScaledInsets(8, 12));
        layout = padding;

        int dx = DpiScale(540);
        LayoutAndSizeToContent(layout, dx, 0, hwnd);
        DoLayout(HwndClientRect(hwnd).Size());
        HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
        UpdateTheme();
        SetIsVisible(true);

        RunValidation();
        return true;
    }

    void RunValidation() {
        violations.Reset();

        int part = CbGetCurrentSelection(partDrop) == 0 ? 1 : 2;
        bool valid = ValidatePdfA(srcPath, part, violations);

        auto* newModel = new ListBoxModelStrings();
        if (valid) {
            newModel->strings.Append(fmt("PASS: Document complies with basic PDF/A-%db rules.", part));
            newModel->strings.Append(StrL("No forbidden features (encryption, JS, external actions) or missing metadata found."));
        } else {
            newModel->strings.Append(fmt("FAILED: %d violation(s) detected for PDF/A-%db:", len(violations), part));
            for (const auto v : violations) {
                newModel->strings.Append(fmt("  - %s", v));
            }
        }
        listBox->SetModel(newModel);
    }

    void OnCheck(VirtMouseEvent*) {
        RunValidation();
    }

    void OnCloseBtn(VirtMouseEvent*) {
        ScheduleDelete();
    }
};

void ShowPdfAValidateDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new PdfAValidateDialog();
    if (!dlg->Create(win)) delete dlg;
}

// Dialog: Convert Colors (Grayscale or RGB)
struct PdfConvertColorsDialog : WindowBase {
    MainWindow* win = nullptr;
    Str srcPath;

    VBox* mainBox = nullptr;
    DropDown* modeDrop = nullptr;
    Edit* destEdit = nullptr;
    VirtButton* browseBtn = nullptr;
    VirtButton* convertBtn = nullptr;
    VirtButton* cancelBtn = nullptr;

    ~PdfConvertColorsDialog() override {
        str::Free(srcPath);
    }

    bool Create(MainWindow* w) {
        win = w;
        WindowTab* tab = win ? win->CurrentTab() : nullptr;
        if (!tab || len(tab->filePath) == 0) return false;
        srcPath = str::Dup(tab->filePath);

        PlatformFont* dialogFont = GetDefaultGuiFont();
        closeOnEsc = true;

        CreateCustomArgs cargs;
        cargs.owner = win ? win->hwndFrame : nullptr;
        cargs.title = Tr("Convert Document Colors");
        cargs.font = dialogFont;
        cargs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        cargs.visible = false;
        cargs.icon = GetAppIcon();
        cargs.bgColor = DarkModeDialogBgColor();
        CreateCustom(cargs);
        if (!hwnd) return false;

        mainBox = new VBox();
        mainBox->alignMain = MainAxisAlign::MainStart;
        mainBox->alignCross = CrossAxisAlign::Stretch;

        mainBox->AddChild(NewVirtText({.s = Tr("Target Color Model:"), .font = font, .isRtl = IsUIRtl()}));

        modeDrop = new DropDown();
        DropDown::CreateArgs dargs;
        dargs.parent = hwnd;
        dargs.font = font;
        modeDrop->Create(dargs);
        StrVec modeItems;
        modeItems.Append(Tr("Grayscale (1 color component, black & white / print production)"));
        modeItems.Append(Tr("RGB (3 color components, standard display colors)"));
        modeDrop->SetItems(modeItems);
        CbSetCurrentSelection(modeDrop, 0);
        mainBox->AddChild(modeDrop);

        mainBox->AddChild(NewVirtText({.s = Tr("Save copy to:"), .font = font, .isRtl = IsUIRtl()}));

        auto* destRow = new HBox();
        destRow->alignCross = CrossAxisAlign::CrossCenter;
        destRow->gap = font->averageCharWidth;

        Str stem = srcPath;
        if (str::EndsWithI(stem, StrL(".pdf"))) stem.len -= 4;
        TempStr defDest = fmt("%s_recolored.pdf", stem);
        Edit::CreateArgs eargs;
        eargs.parent = hwnd;
        eargs.font = font;
        eargs.withBorder = true;
        eargs.isRtl = IsUIRtl();
        eargs.text = defDest;
        destEdit = new Edit();
        destEdit->Create(eargs);
        destRow->AddChild(destEdit, 1);

        browseBtn = NewThemedButton(hwnd, Tr("Browse..."), font, false);
        browseBtn->onClick = MkMethod1<PdfConvertColorsDialog, VirtMouseEvent*, &PdfConvertColorsDialog::OnBrowse>(this);
        destRow->AddChild(browseBtn);
        mainBox->AddChild(destRow);

        auto* btnBox = new HBox();
        btnBox->alignMain = MainAxisAlign::MainEnd;
        btnBox->alignCross = CrossAxisAlign::CrossCenter;
        btnBox->gap = font->averageCharWidth;

        convertBtn = NewThemedButton(hwnd, Tr("Convert"), font, true);
        convertBtn->onClick = MkMethod1<PdfConvertColorsDialog, VirtMouseEvent*, &PdfConvertColorsDialog::OnConvert>(this);
        btnBox->AddChild(convertBtn);

        cancelBtn = NewThemedButton(hwnd, Tr("Cancel"), font, false);
        cancelBtn->onClick = MkMethod1<PdfConvertColorsDialog, VirtMouseEvent*, &PdfConvertColorsDialog::OnCancel>(this);
        btnBox->AddChild(cancelBtn);
        mainBox->AddChild(btnBox);

        auto* padding = new Padding(mainBox, DpiScaledInsets(8, 12));
        layout = padding;

        int dx = DpiScale(540);
        LayoutAndSizeToContent(layout, dx, 0, hwnd);
        DoLayout(HwndClientRect(hwnd).Size());
        HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
        UpdateTheme();
        SetIsVisible(true);
        return true;
    }

    void OnBrowse(VirtMouseEvent*) {
        WCHAR dstFileName[MAX_PATH + 1]{};
        TempWStr current = ToWStrTemp(HwndGetTextTemp(destEdit->hwnd));
        wstr::BufSet(WStr(dstFileName, MAX_PATH), current);

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = dstFileName;
        ofn.nMaxFile = dimof(dstFileName);
        ofn.lpstrFilter = L"PDF Files\0*.pdf\0All Files\0*.*\0";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        ofn.lpstrDefExt = L"pdf";

        if (GetSaveFileNameW(&ofn)) {
            destEdit->SetText(ToUtf8Temp(dstFileName));
        }
    }

    void OnConvert(VirtMouseEvent*) {
        TempStr dest = destEdit->GetTextTemp();
        if (len(dest) == 0) return;
        if (path::IsSame(dest, srcPath)) {
            MessageBoxWarning(hwnd, Tr("Please choose a different destination file name."), Tr("Save Copy"));
            return;
        }

        int numComp = CbGetCurrentSelection(modeDrop) == 0 ? 1 : 3;
        bool ok = PdfConvertColors(srcPath, dest, numComp);
        if (!ok) {
            MessageBoxWarning(hwnd, Tr("Failed to convert document colors."), Tr("Error"));
            return;
        }

        ScheduleDelete();
        if (win) {
            LoadArgs largs(dest, win);
            StartLoadDocument(&largs);
        }
    }

    void OnCancel(VirtMouseEvent*) {
        ScheduleDelete();
    }
};

void ShowPdfConvertColorsDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new PdfConvertColorsDialog();
    if (!dlg->Create(win)) delete dlg;
}
