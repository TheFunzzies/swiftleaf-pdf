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
#include "Accessibility.h"

extern const pdf_write_options gPdfDefaultWriteOptions;

static HICON GetAppIcon() {
    return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
}

void FreeAccessReport(AccessReport& report) {
    for (auto& item : report.items) {
        str::Free(item.category);
        str::Free(item.title);
        str::Free(item.detail);
    }
    VecClear(report.items);
    str::Free(report.language);
    str::Free(report.docTitle);
}

void FreeFigureAltItems(Vec<FigureAltItem>& figures) {
    for (auto& f : figures) {
        str::Free(f.altText);
    }
    VecClear(figures);
}

static void AddReportItem(AccessReport& report, Str category, Str title, Str detail, AccessCheckStatus status,
                          int pageNo = 0) {
    AccessCheckItem item;
    item.category = str::Dup(category);
    item.title = str::Dup(title);
    item.detail = str::Dup(detail);
    item.status = status;
    item.pageNo = pageNo;
    VecAppend(report.items, item);

    if (status == AccessCheckStatus::Pass) {
        report.passCount++;
    } else if (status == AccessCheckStatus::Warning) {
        report.warnCount++;
    } else {
        report.failCount++;
    }
}

static void WalkTreeForTables(fz_context* ctx, pdf_obj* node, int& tableCount, int& tablesWithoutHeaders,
                              int& firstBadPage, int depth = 0) {
    if (!node || depth > 64) return;
    if (pdf_is_array(ctx, node)) {
        int n = pdf_array_len(ctx, node);
        for (int i = 0; i < n; i++) {
            WalkTreeForTables(ctx, pdf_array_get(ctx, node, i), tableCount, tablesWithoutHeaders, firstBadPage,
                              depth + 1);
        }
        return;
    }
    if (!pdf_is_dict(ctx, node)) return;

    pdf_obj* s = pdf_dict_get(ctx, node, PDF_NAME(S));
    const char* sName = s ? pdf_to_name(ctx, s) : nullptr;
    if (sName && str::Eq(Str(sName), StrL("Table"))) {
        tableCount++;
        bool hasTH = false;
        pdf_obj* kids = pdf_dict_get(ctx, node, PDF_NAME(K));
        if (kids) {
            Vec<pdf_obj*> stack;
            VecAppend(stack, kids);
            int remaining = 4096;
            while (len(stack) > 0 && remaining-- > 0) {
                pdf_obj* cur = VecPop(stack);
                if (pdf_is_array(ctx, cur)) {
                    int kLen = pdf_array_len(ctx, cur);
                    for (int j = 0; j < kLen; j++) {
                        VecAppend(stack, pdf_array_get(ctx, cur, j));
                    }
                } else if (pdf_is_dict(ctx, cur)) {
                    pdf_obj* curS = pdf_dict_get(ctx, cur, PDF_NAME(S));
                    const char* curSName = curS ? pdf_to_name(ctx, curS) : nullptr;
                    if (curSName && str::Eq(Str(curSName), StrL("TH"))) {
                        hasTH = true;
                        break;
                    }
                    pdf_obj* subKids = pdf_dict_get(ctx, cur, PDF_NAME(K));
                    if (subKids) VecAppend(stack, subKids);
                }
            }
        }
        if (!hasTH) {
            tablesWithoutHeaders++;
            if (firstBadPage == 0) {
                pdf_obj* pg = pdf_dict_gets(ctx, node, "Pg");
                if (pg) {
                    firstBadPage = 1;
                }
            }
        }
    }

    pdf_obj* k = pdf_dict_get(ctx, node, PDF_NAME(K));
    if (k) WalkTreeForTables(ctx, k, tableCount, tablesWithoutHeaders, firstBadPage, depth + 1);
}

static void WalkAcroFields(fz_context* ctx, pdf_obj* fields, int& fieldCount, int& missingTooltipCount,
                           Str& firstBadFieldName, int& firstBadPage) {
    if (!fields || !pdf_is_array(ctx, fields)) return;
    int n = pdf_array_len(ctx, fields);
    for (int i = 0; i < n; i++) {
        pdf_obj* f = pdf_array_get(ctx, fields, i);
        if (!pdf_is_dict(ctx, f)) continue;
        pdf_obj* kids = pdf_dict_get(ctx, f, PDF_NAME(Kids));
        if (kids && pdf_is_array(ctx, kids) && pdf_array_len(ctx, kids) > 0) {
            WalkAcroFields(ctx, kids, fieldCount, missingTooltipCount, firstBadFieldName, firstBadPage);
            continue;
        }
        fieldCount++;
        pdf_obj* tu = pdf_dict_get(ctx, f, PDF_NAME(TU));
        const char* tuStr = tu ? pdf_to_text_string(ctx, tu) : nullptr;
        if (!tuStr || !*tuStr) {
            missingTooltipCount++;
            if (len(firstBadFieldName) == 0) {
                pdf_obj* t = pdf_dict_get(ctx, f, PDF_NAME(T));
                const char* tStr = t ? pdf_to_text_string(ctx, t) : nullptr;
                firstBadFieldName = str::Dup(tStr && *tStr ? Str(tStr) : StrL("UnnamedField"));
                firstBadPage = 1;
            }
        }
    }
}

// Verify both directions of direct image OBJR links. MCID coverage remains unverified.
static bool HasFigureAlt(fz_context* ctx, pdf_obj* tree, pdf_obj* image, pdf_obj* parent, int depth = 0) {
    if (!tree || depth > 64) return false;
    if (pdf_is_array(ctx, tree)) {
        for (int i = 0; i < pdf_array_len(ctx, tree); i++) {
            if (HasFigureAlt(ctx, pdf_array_get(ctx, tree, i), image, parent, depth + 1)) return true;
        }
        return false;
    }
    if (!pdf_is_dict(ctx, tree)) return false;
    pdf_obj* kids = pdf_dict_get(ctx, tree, PDF_NAME(K));
    const char* alt = pdf_to_text_string(ctx, pdf_dict_get(ctx, tree, PDF_NAME(Alt)));
    if (pdf_dict_get(ctx, tree, PDF_NAME(S)) == PDF_NAME(Figure) && alt && *alt && parent &&
        pdf_objcmp(ctx, parent, tree) == 0) {
        int n = pdf_is_array(ctx, kids) ? pdf_array_len(ctx, kids) : 1;
        for (int i = 0; i < n; i++) {
            pdf_obj* ref = pdf_is_array(ctx, kids) ? pdf_array_get(ctx, kids, i) : kids;
            if (pdf_dict_get(ctx, ref, PDF_NAME(Type)) == PDF_NAME(OBJR) &&
                pdf_objcmp(ctx, pdf_dict_gets(ctx, ref, "Obj"), image) == 0)
                return true;
        }
    }
    return HasFigureAlt(ctx, kids, image, parent, depth + 1);
}

AccessReport RunAccessibilityCheck(fz_context* ctx, pdf_document* doc) {
    AccessReport report;
    if (!ctx || !doc) return report;

    pdf_obj* trailer = pdf_trailer(ctx, doc);
    pdf_obj* root = trailer ? pdf_dict_get(ctx, trailer, PDF_NAME(Root)) : nullptr;
    if (!root) {
        AddReportItem(report, StrL("Document"), StrL("Valid PDF"), StrL("Failed to read document catalog"),
                      AccessCheckStatus::Fail);
        return report;
    }

    // 1. Tagged PDF & Structure Tree Root
    pdf_obj* markInfo = pdf_dict_gets(ctx, root, "MarkInfo");
    bool marked = markInfo && pdf_to_bool(ctx, pdf_dict_gets(ctx, markInfo, "Marked"));
    pdf_obj* structTree = pdf_dict_get(ctx, root, PDF_NAME(StructTreeRoot));
    report.isTagged = marked && (structTree != nullptr);

    if (report.isTagged) {
        AddReportItem(report, StrL("Structure"), StrL("Tagged PDF"),
                      StrL("Tag markers present; content links and PDF/UA conformance require manual check"),
                      AccessCheckStatus::Warning);
    } else if (structTree != nullptr) {
        AddReportItem(report, StrL("Structure"), StrL("Tagged PDF"),
                      StrL("Structure tree present but MarkInfo Marked flag is missing or false"),
                      AccessCheckStatus::Warning);
    } else {
        AddReportItem(report, StrL("Structure"), StrL("Tagged PDF"),
                      StrL("Document is not tagged. Assistive tech may fail to read content logically"),
                      AccessCheckStatus::Fail);
    }

    // 2. Reading order / StructTreeRoot contents
    if (structTree) {
        pdf_obj* k = pdf_dict_get(ctx, structTree, PDF_NAME(K));
        int kLen = k ? (pdf_is_array(ctx, k) ? pdf_array_len(ctx, k) : 1) : 0;
        if (kLen > 0) {
            AddReportItem(
                report, StrL("Structure"), StrL("Logical Reading Order"),
                StrL("Manual check required: structure children do not verify reading order or content links"),
                AccessCheckStatus::Warning);
        } else {
            AddReportItem(report, StrL("Structure"), StrL("Logical Reading Order"),
                          StrL("Structure tree exists but contains no child elements"), AccessCheckStatus::Warning);
        }
    } else {
        AddReportItem(report, StrL("Structure"), StrL("Logical Reading Order"),
                      StrL("No structure tree present; reading order relies on raw content streams"),
                      AccessCheckStatus::Fail);
    }

    // 3. Document Language
    pdf_obj* langObj = pdf_dict_get(ctx, root, PDF_NAME(Lang));
    const char* langStr = langObj ? pdf_to_text_string(ctx, langObj) : nullptr;
    if (langStr && *langStr) {
        report.language = str::Dup(Str(langStr));
        AddReportItem(report, StrL("Metadata"), StrL("Document Language"),
                      fmt(Tr("Document natural language is specified as '%s' (/Lang)").s, report.language),
                      AccessCheckStatus::Pass);
    } else {
        AddReportItem(report, StrL("Metadata"), StrL("Document Language"),
                      StrL("Document language is not specified in document catalog (/Lang)"), AccessCheckStatus::Fail);
    }

    // 4. Document Title & DisplayDocTitle
    pdf_obj* info = pdf_dict_get(ctx, trailer, PDF_NAME(Info));
    pdf_obj* titleObj = info ? pdf_dict_get(ctx, info, PDF_NAME(Title)) : nullptr;
    const char* titleStr = titleObj ? pdf_to_text_string(ctx, titleObj) : nullptr;
    pdf_obj* vp = pdf_dict_gets(ctx, root, "ViewerPreferences");
    bool displayDocTitle = vp && pdf_to_bool(ctx, pdf_dict_gets(ctx, vp, "DisplayDocTitle"));
    report.displayDocTitle = displayDocTitle;

    if (titleStr && *titleStr) {
        report.docTitle = str::Dup(Str(titleStr));
        if (displayDocTitle) {
            AddReportItem(report, StrL("Metadata"), StrL("Document Title"),
                          fmt(Tr("Title '%s' is set and DisplayDocTitle is enabled").s, report.docTitle),
                          AccessCheckStatus::Pass);
        } else {
            AddReportItem(report, StrL("Metadata"), StrL("Document Title"),
                          fmt(Tr("Title '%s' is set, but DisplayDocTitle is disabled").s, report.docTitle),
                          AccessCheckStatus::Warning);
        }
    } else {
        AddReportItem(report, StrL("Metadata"), StrL("Document Title"),
                      StrL("Document title is missing in document properties / metadata"), AccessCheckStatus::Fail);
    }

    // 5. Table Headers
    if (!report.isTagged) {
        AddReportItem(report, StrL("Tables"), StrL("Table Headers"),
                      StrL("Document is not tagged; table headers cannot be verified"), AccessCheckStatus::Warning);
    } else {
        int tableCount = 0;
        int tablesWithoutHeaders = 0;
        int firstBadTablePage = 0;
        WalkTreeForTables(ctx, structTree, tableCount, tablesWithoutHeaders, firstBadTablePage);
        if (tableCount == 0) {
            AddReportItem(report, StrL("Tables"), StrL("Table Headers"),
                          StrL("No table roles found; manual check required for untagged tables"),
                          AccessCheckStatus::Warning);
        } else if (tablesWithoutHeaders == 0) {
            AddReportItem(
                report, StrL("Tables"), StrL("Table Headers"),
                fmt(Tr("%d table(s) contain TH roles; header scope and content associations require manual check").s,
                    tableCount),
                AccessCheckStatus::Warning);
        } else {
            AddReportItem(report, StrL("Tables"), StrL("Table Headers"),
                          fmt(Tr("%d table(s) lack header cells (/TH)").s, tablesWithoutHeaders),
                          AccessCheckStatus::Fail, firstBadTablePage);
        }
    }

    // 6. Form Fields Tooltips
    pdf_obj* acroForm = pdf_dict_get(ctx, root, PDF_NAME(AcroForm));
    pdf_obj* fields = acroForm ? pdf_dict_get(ctx, acroForm, PDF_NAME(Fields)) : nullptr;
    if (fields) {
        int fieldCount = 0;
        int missingTooltips = 0;
        Str firstBadFieldName{};
        int firstBadFieldPage = 0;
        WalkAcroFields(ctx, fields, fieldCount, missingTooltips, firstBadFieldName, firstBadFieldPage);
        if (fieldCount == 0) {
            AddReportItem(report, StrL("Forms"), StrL("Field Tooltips"), StrL("No interactive form fields found"),
                          AccessCheckStatus::Pass);
        } else if (missingTooltips == 0) {
            AddReportItem(report, StrL("Forms"), StrL("Field Tooltips"),
                          fmt(Tr("All %d form field(s) have accessible descriptions (/TU)").s, fieldCount),
                          AccessCheckStatus::Pass);
        } else {
            AddReportItem(report, StrL("Forms"), StrL("Field Tooltips"),
                          fmt(Tr("%d of %d field(s) lack descriptions (e.g. '%s')").s, missingTooltips, fieldCount,
                              firstBadFieldName),
                          AccessCheckStatus::Fail, firstBadFieldPage);
        }
        str::Free(firstBadFieldName);
    } else {
        AddReportItem(report, StrL("Forms"), StrL("Field Tooltips"), StrL("No interactive form fields found"),
                      AccessCheckStatus::Pass);
    }

    // 7. Scanned / image-only pages and Figures without alt text
    int numPages = pdf_count_pages(ctx, doc);
    int scannedPagesCount = 0;
    int firstScannedPage = 0;
    int totalImages = 0;
    int imagesWithoutAlt = 0;
    int firstImageWithoutAltPage = 0;

    for (int i = 0; i < numPages; i++) {
        pdf_page* page = pdf_load_page(ctx, doc, i);
        if (!page) continue;

        int pageImages = 0;
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, i);
        pdf_obj* res = pageObj ? pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Resources)) : nullptr;
        pdf_obj* xobj = res ? pdf_dict_get(ctx, res, PDF_NAME(XObject)) : nullptr;
        if (xobj && pdf_is_dict(ctx, xobj)) {
            int xLen = pdf_dict_len(ctx, xobj);
            for (int x = 0; x < xLen; x++) {
                pdf_obj* item = pdf_dict_get_val(ctx, xobj, x);
                if (pdf_is_dict(ctx, item) && pdf_dict_get(ctx, item, PDF_NAME(Subtype)) == PDF_NAME(Image)) {
                    pageImages++;
                    totalImages++;
                    pdf_obj* key = pdf_dict_gets(ctx, item, "StructParent");
                    pdf_obj* parents = pdf_dict_get(ctx, structTree, PDF_NAME(ParentTree));
                    pdf_obj* parent = pdf_is_int(ctx, key) && pdf_to_int(ctx, key) >= 0
                                          ? pdf_lookup_number(ctx, parents, pdf_to_int(ctx, key))
                                          : nullptr;
                    if (!HasFigureAlt(ctx, structTree, item, parent)) {
                        imagesWithoutAlt++;
                        if (firstImageWithoutAltPage == 0) firstImageWithoutAltPage = i + 1;
                    }
                }
            }
        }

        fz_stext_page* stext = fz_new_stext_page_from_page(ctx, (fz_page*)page, nullptr);
        int textChars = 0;
        if (stext) {
            for (fz_stext_block* block = stext->first_block; block; block = block->next) {
                if (block->type == FZ_STEXT_BLOCK_TEXT) {
                    for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                            if (ch->c > 32) textChars++;
                        }
                    }
                }
            }
            fz_drop_stext_page(ctx, stext);
        }

        if (textChars == 0 && pageImages > 0) {
            scannedPagesCount++;
            if (firstScannedPage == 0) firstScannedPage = i + 1;
        }

        fz_drop_page(ctx, (fz_page*)page);
    }

    if (scannedPagesCount == 0) {
        AddReportItem(report, StrL("Content"), StrL("Scanned Pages"),
                      StrL("No image-only pages detected; text completeness requires manual check"),
                      AccessCheckStatus::Pass);
    } else {
        AddReportItem(report, StrL("Content"), StrL("Scanned Pages"),
                      fmt(Tr("%d page(s) contain images but no searchable text (e.g. Page %d)").s, scannedPagesCount,
                          firstScannedPage),
                      AccessCheckStatus::Warning, firstScannedPage);
    }

    if (totalImages == 0) {
        AddReportItem(
            report, StrL("Figures"), StrL("Alternative Text"),
            StrL("No direct raster image resources found; vector, inline and nested figures require manual check"),
            AccessCheckStatus::Warning);
    } else if (imagesWithoutAlt == 0) {
        AddReportItem(report, StrL("Figures"), StrL("Alternative Text"),
                      fmt("%d direct image resource(s) have Figure Alt/OBJR links; MCIDs, ParentTree and coverage "
                          "require manual check",
                          totalImages),
                      AccessCheckStatus::Warning);
    } else {
        AddReportItem(
            report, StrL("Figures"), StrL("Alternative Text"),
            fmt("%d of %d image resources lack verified Figure Alt/OBJR links; MCID associations require manual check",
                imagesWithoutAlt, totalImages),
            AccessCheckStatus::Warning, firstImageWithoutAltPage);
    }

    return report;
}

AccessReport RunAccessibilityCheckFile(Str filePath) {
    AccessReport report;
    if (len(filePath) == 0) return report;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return report;
    fz_register_document_handlers(ctx);

    pdf_document* doc = nullptr;
    fz_var(doc);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(filePath));
        report = RunAccessibilityCheck(ctx, doc);
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        AddReportItem(report, StrL("Document"), StrL("Inspection failed"),
                      StrL("Unable to complete accessibility inspection; manual check required"),
                      AccessCheckStatus::Fail);
    }
    fz_drop_context(ctx);
    return report;
}

bool ExtractDocFigures(fz_context* ctx, pdf_document* doc, Vec<FigureAltItem>& outFigures) {
    if (!ctx || !doc) return false;
    int numPages = pdf_count_pages(ctx, doc);
    for (int p = 0; p < numPages; p++) {
        pdf_page* page = pdf_load_page(ctx, doc, p);
        if (!page) continue;
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, p);
        pdf_obj* res = pageObj ? pdf_dict_get(ctx, pageObj, PDF_NAME(Resources)) : nullptr;
        pdf_obj* xobj = res ? pdf_dict_get(ctx, res, PDF_NAME(XObject)) : nullptr;
        if (xobj && pdf_is_dict(ctx, xobj)) {
            int n = pdf_dict_len(ctx, xobj);
            for (int i = 0; i < n; i++) {
                pdf_obj* item = pdf_dict_get_val(ctx, xobj, i);
                if (pdf_is_dict(ctx, item) && pdf_dict_get(ctx, item, PDF_NAME(Subtype)) == PDF_NAME(Image)) {
                    FigureAltItem fig;
                    fig.pageNo = p + 1;
                    fig.objNum = pdf_to_num(ctx, item);
                    VecAppend(outFigures, fig);
                }
            }
        }
        fz_drop_page(ctx, (fz_page*)page);
    }
    return true;
}

struct TextLineSample {
    float size = 12.f;
    bool isBullet = false;
    int pageNo = 1;
    pdf_obj* pageObj = nullptr;
};

bool ApplyAccessibilityFixes(Str srcPath, Str destPath, const AccessibilityFixOptions& options) {
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

    Vec<TextLineSample> samples;

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        pdf_obj* trailer = pdf_trailer(ctx, doc);
        pdf_obj* root = trailer ? pdf_dict_get(ctx, trailer, PDF_NAME(Root)) : nullptr;
        if (!root) fz_throw(ctx, FZ_ERROR_GENERIC, "no catalog");

        // 1. Language
        if (len(options.language) > 0) {
            pdf_dict_put_text_string(ctx, root, PDF_NAME(Lang), CStrTemp(options.language));
        }

        // 2. Title in Info & ViewerPreferences
        if (len(options.title) > 0) {
            pdf_obj* info = pdf_dict_get(ctx, trailer, PDF_NAME(Info));
            if (!info) {
                info = pdf_new_dict(ctx, doc, 4);
                pdf_dict_put(ctx, trailer, PDF_NAME(Info), info);
            }
            pdf_dict_put_text_string(ctx, info, PDF_NAME(Title), CStrTemp(options.title));
        }

        if (options.setDisplayDocTitle) {
            pdf_obj* vp = pdf_dict_gets(ctx, root, "ViewerPreferences");
            if (!vp) {
                vp = pdf_new_dict(ctx, doc, 4);
                pdf_dict_puts(ctx, root, "ViewerPreferences", vp);
            }
            pdf_dict_puts(ctx, vp, "DisplayDocTitle", PDF_TRUE);
        }

        // 3. Auto-tag structure
        if (options.autoTagBasicStructure && !pdf_dict_get(ctx, root, PDF_NAME(StructTreeRoot))) {
            pdf_obj* markInfo = pdf_dict_gets(ctx, root, "MarkInfo");
            if (!markInfo) {
                markInfo = pdf_new_dict(ctx, doc, 2);
                pdf_dict_puts(ctx, root, "MarkInfo", markInfo);
            }
            pdf_dict_puts(ctx, markInfo, "Marked", PDF_FALSE);

            pdf_obj* structTree = pdf_new_dict(ctx, doc, 4);
            pdf_obj* structTreeRef = pdf_add_object(ctx, doc, structTree);
            pdf_dict_put(ctx, structTree, PDF_NAME(Type), PDF_NAME(StructTreeRoot));

            pdf_obj* docElem = pdf_new_dict(ctx, doc, 6);
            pdf_obj* docElemRef = pdf_add_object(ctx, doc, docElem);
            pdf_dict_put(ctx, docElem, PDF_NAME(Type), PDF_NAME(StructElem));
            pdf_dict_puts_drop(ctx, docElem, "S", pdf_new_name(ctx, "Document"));
            pdf_dict_put(ctx, docElem, PDF_NAME(P), structTreeRef);

            pdf_obj* kidsArray = pdf_new_array(ctx, doc, 32);

            int numPages = pdf_count_pages(ctx, doc);

            for (int p = 0; p < numPages; p++) {
                pdf_page* page = pdf_load_page(ctx, doc, p);
                if (!page) continue;
                pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, p);

                fz_stext_page* stext = fz_new_stext_page_from_page(ctx, (fz_page*)page, nullptr);
                if (stext) {
                    for (fz_stext_block* block = stext->first_block; block; block = block->next) {
                        if (block->type == FZ_STEXT_BLOCK_TEXT) {
                            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                                float maxSz = 0.f;
                                bool isBul = false;
                                int charIdx = 0;
                                for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                                    if (ch->size > maxSz) maxSz = ch->size;
                                    if (charIdx == 0) {
                                        if (ch->c == 0x2022 || ch->c == '-' || ch->c == '*') isBul = true;
                                    }
                                    charIdx++;
                                }
                                if (maxSz > 0.f) {
                                    VecAppend(samples, TextLineSample{maxSz, isBul, p + 1, pageObj});
                                }
                            }
                        }
                    }
                    fz_drop_stext_page(ctx, stext);
                }
                fz_drop_page(ctx, (fz_page*)page);
            }

            float baseSize = 12.f;
            if (len(samples) > 0) {
                float sum = 0.f;
                for (const auto& s : samples) sum += s.size;
                baseSize = sum / (float)len(samples);
            }

            for (const auto& s : samples) {
                pdf_obj* elem = pdf_new_dict(ctx, doc, 6);
                pdf_obj* elemRef = pdf_add_object(ctx, doc, elem);
                pdf_dict_put(ctx, elem, PDF_NAME(Type), PDF_NAME(StructElem));
                pdf_dict_put(ctx, elem, PDF_NAME(P), docElemRef);
                if (s.pageObj) pdf_dict_puts(ctx, elem, "Pg", s.pageObj);

                if (s.size >= 1.35f * baseSize) {
                    pdf_dict_puts_drop(ctx, elem, "S", pdf_new_name(ctx, "H1"));
                } else if (s.size >= 1.15f * baseSize) {
                    pdf_dict_puts_drop(ctx, elem, "S", pdf_new_name(ctx, "H2"));
                } else if (s.isBullet) {
                    pdf_dict_puts_drop(ctx, elem, "S", pdf_new_name(ctx, "LI"));
                } else {
                    pdf_dict_puts_drop(ctx, elem, "S", pdf_new_name(ctx, "P"));
                }
                pdf_array_push_drop(ctx, kidsArray, elemRef);
                pdf_drop_obj(ctx, elem);
            }

            // Figure tags
            for (const auto& fig : options.figures) {
                pdf_obj* figElem = pdf_new_dict(ctx, doc, 6);
                pdf_obj* figElemRef = pdf_add_object(ctx, doc, figElem);
                pdf_dict_put(ctx, figElem, PDF_NAME(Type), PDF_NAME(StructElem));
                pdf_dict_put(ctx, figElem, PDF_NAME(P), docElemRef);
                pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, fig.pageNo - 1);
                if (pageObj) pdf_dict_puts(ctx, figElem, "Pg", pageObj);
                if (fig.isDecorative) {
                    pdf_dict_puts_drop(ctx, figElem, "S", pdf_new_name(ctx, "Artifact"));
                } else {
                    pdf_dict_puts_drop(ctx, figElem, "S", pdf_new_name(ctx, "Figure"));
                    if (len(fig.altText) > 0) {
                        pdf_dict_put_text_string(ctx, figElem, PDF_NAME(Alt), CStrTemp(fig.altText));
                    }
                }
                pdf_array_push_drop(ctx, kidsArray, figElemRef);
                pdf_drop_obj(ctx, figElem);
            }

            if (pdf_array_len(ctx, kidsArray) == 0) {
                pdf_obj* pElem = pdf_new_dict(ctx, doc, 4);
                pdf_obj* pRef = pdf_add_object(ctx, doc, pElem);
                pdf_dict_put(ctx, pElem, PDF_NAME(Type), PDF_NAME(StructElem));
                pdf_dict_put(ctx, pElem, PDF_NAME(P), docElemRef);
                pdf_dict_puts_drop(ctx, pElem, "S", pdf_new_name(ctx, "P"));
                pdf_array_push_drop(ctx, kidsArray, pRef);
                pdf_drop_obj(ctx, pElem);
            }

            pdf_dict_put_drop(ctx, docElem, PDF_NAME(K), kidsArray);
            pdf_dict_put_drop(ctx, structTree, PDF_NAME(K), docElemRef);
            pdf_dict_put_drop(ctx, root, PDF_NAME(StructTreeRoot), structTreeRef);

            pdf_drop_obj(ctx, docElem);
            pdf_drop_obj(ctx, structTree);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
        VecReset(samples);
    }
    fz_catch(ctx) {
        ok = false;
    }
    fz_drop_context(ctx);
    return ok;
}

// Dialog: Accessibility Checker
struct AccessibilityCheckerDialog : WindowBase {
    MainWindow* win = nullptr;
    Str srcPath;
    AccessReport report;

    VBox* mainBox = nullptr;
    VirtListBox* listBox = nullptr;
    ListBoxModelStrings* model = nullptr;
    VirtText* summaryText = nullptr;
    VirtButton* gotoBtn = nullptr;
    VirtButton* fixBtn = nullptr;
    VirtButton* closeBtn = nullptr;

    ~AccessibilityCheckerDialog() override {
        FreeAccessReport(report);
        str::Free(srcPath);
    }

    bool Create(MainWindow* w) {
        win = w;
        WindowTab* tab = win ? win->CurrentTab() : nullptr;
        if (!tab || len(tab->filePath) == 0) return false;
        srcPath = str::Dup(tab->filePath);

        report = RunAccessibilityCheckFile(srcPath);

        PlatformFont* dialogFont = GetDefaultGuiFont();
        closeOnEsc = true;

        CreateCustomArgs cargs;
        cargs.owner = win ? win->hwndFrame : nullptr;
        cargs.title = Tr("Accessibility Checker");
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

        TempStr header = str::FormatTemp(Tr("Report for: %s").s, path::GetBaseNameTemp(srcPath));
        mainBox->AddChild(NewVirtText({.s = header, .font = font, .isRtl = IsUIRtl()}));

        TempStr sum = str::FormatTemp(Tr("Results: %d Passed, %d Warnings, %d Failed").s, report.passCount,
                                      report.warnCount, report.failCount);
        summaryText = NewVirtText({.s = sum, .font = font, .isRtl = IsUIRtl()});
        mainBox->AddChild(summaryText);

        listBox = new VirtListBox();
        listBox->dpi = GetDpi();
        listBox->font = font;
        listBox->idealSizeLines = 14;
        model = new ListBoxModelStrings();
        for (const auto& item : report.items) {
            const char* tag = item.status == AccessCheckStatus::Pass
                                  ? "[PASS] "
                                  : (item.status == AccessCheckStatus::Warning ? "[WARN] " : "[FAIL] ");
            TempStr line = str::FormatTemp("%s%s: %s", Str(tag), item.title, item.detail);
            if (item.pageNo > 0) {
                line = str::FormatTemp("%s (Page %d)", line, item.pageNo);
            }
            model->strings.Append(line);
        }
        listBox->SetModel(model);
        listBox->onDoubleClick =
            MkMethod0<AccessibilityCheckerDialog, &AccessibilityCheckerDialog::OnListDoubleClick>(this);
        mainBox->AddChild(listBox);

        auto* btnBox = new HBox();
        btnBox->alignMain = MainAxisAlign::MainEnd;
        btnBox->alignCross = CrossAxisAlign::CrossCenter;
        btnBox->gap = font->averageCharWidth;

        gotoBtn = NewThemedButton(hwnd, Tr("Go to Page"), font, false);
        gotoBtn->onClick =
            MkMethod1<AccessibilityCheckerDialog, VirtMouseEvent*, &AccessibilityCheckerDialog::OnGoToPage>(this);
        btnBox->AddChild(gotoBtn);

        fixBtn = NewThemedButton(hwnd, Tr("Fix Issues..."), font, false);
        fixBtn->onClick =
            MkMethod1<AccessibilityCheckerDialog, VirtMouseEvent*, &AccessibilityCheckerDialog::OnFixIssues>(this);
        btnBox->AddChild(fixBtn);

        closeBtn = NewThemedButton(hwnd, Tr("Close"), font, true);
        closeBtn->onClick =
            MkMethod1<AccessibilityCheckerDialog, VirtMouseEvent*, &AccessibilityCheckerDialog::OnCloseBtn>(this);
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
        return true;
    }

    void OnGoToPage(VirtMouseEvent*) {
        int idx = listBox->GetCurrentSelection();
        if (idx >= 0 && idx < len(report.items)) {
            int p = report.items[idx].pageNo;
            if (p > 0 && win && win->ctrl) {
                win->ctrl->GoToPage(p, false);
            }
        }
    }

    void OnListDoubleClick() { OnGoToPage(nullptr); }

    void OnFixIssues(VirtMouseEvent*) {
        ScheduleDelete();
        ShowAccessibilityFixesDialog(win);
    }

    void OnCloseBtn(VirtMouseEvent*) { ScheduleDelete(); }
};

void ShowAccessibilityCheckerDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new AccessibilityCheckerDialog();
    if (!dlg->Create(win)) delete dlg;
}

// Dialog: Accessibility Fixes
struct AccessibilityFixesDialog : WindowBase {
    MainWindow* win = nullptr;
    Str srcPath;
    Vec<FigureAltItem> figures;

    VBox* mainBox = nullptr;
    Edit* langEdit = nullptr;
    Edit* titleEdit = nullptr;
    Checkbox* dispDocTitleCheck = nullptr;
    Checkbox* autoTagCheck = nullptr;
    Edit* destEdit = nullptr;
    Edit* altEdit = nullptr;
    Checkbox* decorCheck = nullptr;

    VirtButton* browseBtn = nullptr;
    VirtButton* applyBtn = nullptr;
    VirtButton* cancelBtn = nullptr;

    ~AccessibilityFixesDialog() override {
        FreeFigureAltItems(figures);
        str::Free(srcPath);
    }

    bool Create(MainWindow* w) {
        win = w;
        WindowTab* tab = win ? win->CurrentTab() : nullptr;
        if (!tab || len(tab->filePath) == 0) return false;
        srcPath = str::Dup(tab->filePath);

        AccessReport rep = RunAccessibilityCheckFile(srcPath);

        fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        if (ctx) {
            fz_register_document_handlers(ctx);
            pdf_document* doc = nullptr;
            fz_try(ctx) {
                doc = pdf_open_document(ctx, CStrTemp(srcPath));
                ExtractDocFigures(ctx, doc, figures);
            }
            fz_always(ctx) {
                if (doc) pdf_drop_document(ctx, doc);
            }
            fz_catch(ctx) {}
            fz_drop_context(ctx);
        }

        PlatformFont* dialogFont = GetDefaultGuiFont();
        closeOnEsc = true;

        CreateCustomArgs cargs;
        cargs.owner = win ? win->hwndFrame : nullptr;
        cargs.title = Tr("Accessibility Fixes & Auto-Tag");
        cargs.font = dialogFont;
        cargs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        cargs.visible = false;
        cargs.icon = GetAppIcon();
        cargs.bgColor = DarkModeDialogBgColor();
        CreateCustom(cargs);
        if (!hwnd) {
            FreeAccessReport(rep);
            return false;
        }

        mainBox = new VBox();
        mainBox->alignMain = MainAxisAlign::MainStart;
        mainBox->alignCross = CrossAxisAlign::Stretch;

        // Language row
        mainBox->AddChild(NewVirtText({.s = Tr("Document Language (e.g. en-US):"), .font = font, .isRtl = IsUIRtl()}));

        Str defLang = len(rep.language) > 0 ? rep.language : StrL("en-US");
        Edit::CreateArgs eargs;
        eargs.parent = hwnd;
        eargs.font = font;
        eargs.withBorder = true;
        eargs.isRtl = IsUIRtl();
        eargs.text = defLang;
        langEdit = new Edit();
        langEdit->Create(eargs);
        mainBox->AddChild(langEdit);

        // Title row
        mainBox->AddChild(NewVirtText({.s = Tr("Document Title:"), .font = font, .isRtl = IsUIRtl()}));

        Str defTitle = len(rep.docTitle) > 0 ? rep.docTitle : path::GetBaseNameTemp(srcPath);
        eargs.text = defTitle;
        titleEdit = new Edit();
        titleEdit->Create(eargs);
        mainBox->AddChild(titleEdit);

        // Checkboxes
        Checkbox::CreateArgs cbargs;
        cbargs.parent = hwnd;
        cbargs.font = font;
        cbargs.text = Tr("Display document title in window titlebar (/DisplayDocTitle)");
        cbargs.initialState = Checkbox::State::Checked;
        dispDocTitleCheck = new Checkbox();
        dispDocTitleCheck->Create(cbargs);
        mainBox->AddChild(dispDocTitleCheck);

        cbargs.text = Tr("Generate draft roles only (not linked to content; existing tags preserved)");
        cbargs.initialState = Checkbox::State::Checked;
        autoTagCheck = new Checkbox();
        autoTagCheck->Create(cbargs);
        mainBox->AddChild(autoTagCheck);

        // Figures Alt Text
        if (len(figures) > 0) {
            mainBox->AddChild(NewVirtText({.s = fmt(Tr("Figures found: %d. Set alternate text:").s, len(figures)),
                                           .font = font,
                                           .isRtl = IsUIRtl()}));

            eargs.text = StrL("");
            altEdit = new Edit();
            altEdit->Create(eargs);
            mainBox->AddChild(altEdit);

            cbargs.text = Tr("Mark figure as decorative (/Artifact)");
            cbargs.initialState = Checkbox::State::Unchecked;
            decorCheck = new Checkbox();
            decorCheck->Create(cbargs);
            mainBox->AddChild(decorCheck);
        }

        // Destination Row
        mainBox->AddChild(NewVirtText({.s = Tr("Save fixed PDF to:"), .font = font, .isRtl = IsUIRtl()}));

        auto* destRow = new HBox();
        destRow->alignCross = CrossAxisAlign::CrossCenter;
        destRow->gap = font->averageCharWidth;

        Str stem = srcPath;
        if (str::EndsWithI(stem, StrL(".pdf"))) stem.len -= 4;
        TempStr defDest = fmt("%s_accessible.pdf", stem);
        eargs.text = defDest;
        destEdit = new Edit();
        destEdit->Create(eargs);
        destRow->AddChild(destEdit, 1);

        browseBtn = NewThemedButton(hwnd, Tr("Browse..."), font, false);
        browseBtn->onClick =
            MkMethod1<AccessibilityFixesDialog, VirtMouseEvent*, &AccessibilityFixesDialog::OnBrowse>(this);
        destRow->AddChild(browseBtn);
        mainBox->AddChild(destRow);

        // Buttons row
        auto* btnBox = new HBox();
        btnBox->alignMain = MainAxisAlign::MainEnd;
        btnBox->alignCross = CrossAxisAlign::CrossCenter;
        btnBox->gap = font->averageCharWidth;

        applyBtn = NewThemedButton(hwnd, Tr("Apply Fixes"), font, true);
        applyBtn->onClick =
            MkMethod1<AccessibilityFixesDialog, VirtMouseEvent*, &AccessibilityFixesDialog::OnApply>(this);
        btnBox->AddChild(applyBtn);

        cancelBtn = NewThemedButton(hwnd, Tr("Cancel"), font, false);
        cancelBtn->onClick =
            MkMethod1<AccessibilityFixesDialog, VirtMouseEvent*, &AccessibilityFixesDialog::OnCancel>(this);
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

        FreeAccessReport(rep);
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

    void OnApply(VirtMouseEvent*) {
        TempStr dest = destEdit->GetTextTemp();
        if (len(dest) == 0) return;
        if (path::IsSame(dest, srcPath)) {
            MessageBoxWarning(hwnd, Tr("Please choose a different destination file name."), Tr("Save Copy"));
            return;
        }

        AccessibilityFixOptions opts;
        TempStr lang = langEdit->GetTextTemp();
        opts.language = str::Dup(lang);
        TempStr title = titleEdit->GetTextTemp();
        opts.title = str::Dup(title);
        opts.setDisplayDocTitle = dispDocTitleCheck->IsChecked();
        opts.autoTagBasicStructure = autoTagCheck->IsChecked();

        if (len(figures) > 0 && altEdit) {
            TempStr alt = altEdit->GetTextTemp();
            bool isDec = decorCheck && decorCheck->IsChecked();
            for (auto& f : figures) {
                f.altText = str::Dup(alt);
                f.isDecorative = isDec;
            }
            opts.figures = figures;
        }

        bool ok = ApplyAccessibilityFixes(srcPath, dest, opts);
        str::Free(opts.language);
        str::Free(opts.title);

        if (!ok) {
            MessageBoxWarning(hwnd, Tr("Failed to apply accessibility fixes to PDF."), Tr("Error"));
            return;
        }

        ScheduleDelete();
        if (win) {
            LoadArgs largs(dest, win);
            StartLoadDocument(&largs);
        }
    }

    void OnCancel(VirtMouseEvent*) { ScheduleDelete(); }
};

void ShowAccessibilityFixesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new AccessibilityFixesDialog();
    if (!dlg->Create(win)) delete dlg;
}
