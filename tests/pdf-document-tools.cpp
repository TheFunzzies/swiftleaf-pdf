/* Copyright 2026 the SwiftLeaf PDF authors. License: GPLv3 */
#include "base/Base.h"
extern "C" {
#include "mupdf/pdf.h"
}
#include "merge-types.inc"

void log(Str s) { fprintf(stderr, "%.*s", s.len, s.s); }
void _uploadDebugReport(Str a, Str b, bool) {
    fprintf(stderr, "Invariant: %.*s %.*s\n", a.len, a.s, b.len, b.s);
    exit(2);
}
#pragma comment(lib, "winspool.lib")
const pdf_write_options gPdfDefaultWriteOptions = {};
#include "merge-under-test.inc"

static void Check(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static pdf_obj* Root(fz_context* ctx, pdf_document* doc) {
    return pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
}

static void Fixture(fz_context* ctx, const char* path) {
    pdf_document* doc = pdf_create_document(ctx);
    pdf_obj* res = pdf_new_dict(ctx, doc, 2);
    pdf_obj* fonts = pdf_dict_put_dict(ctx, res, PDF_NAME(Font), 1);
    pdf_obj* font = pdf_dict_put_dict(ctx, fonts, PDF_NAME(Helv), 3);
    pdf_dict_put(ctx, font, PDF_NAME(Type), PDF_NAME(Font));
    pdf_dict_put(ctx, font, PDF_NAME(Subtype), PDF_NAME(Type1));
    pdf_dict_put_name(ctx, font, PDF_NAME(BaseFont), "Helvetica");
    for (int i = 0; i < 2; i++) {
        const char* text = "BT /Helv 12 Tf 20 120 Td (ID-123 ID-456 public) Tj 0 -20 Td 3 Tr (HIDDEN) Tj ET";
        fz_buffer* buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)text, strlen(text));
        pdf_obj* page = pdf_add_page(ctx, doc, fz_make_rect(0, 0, 200, 200), 0, res, buf);
        pdf_insert_page(ctx, doc, -1, page);
        pdf_drop_obj(ctx, page);
        fz_drop_buffer(ctx, buf);
    }
    pdf_obj* root = Root(ctx, doc);
    pdf_obj* p0 = pdf_lookup_page_obj(ctx, doc, 0);
    pdf_obj* p1 = pdf_lookup_page_obj(ctx, doc, 1);
    pdf_obj* annots = pdf_dict_put_array(ctx, p0, PDF_NAME(Annots), 4);
    pdf_obj* form = pdf_dict_put_dict(ctx, root, PDF_NAME(AcroForm), 3);
    pdf_dict_put(ctx, form, PDF_NAME(DR), res);
    pdf_dict_put_text_string(ctx, form, PDF_NAME(DA), "/Helv 12 Tf 0 g");
    pdf_obj* fields = pdf_dict_put_array(ctx, form, PDF_NAME(Fields), 1);
    pdf_obj* widget = pdf_new_dict(ctx, doc, 8);
    pdf_dict_put(ctx, widget, PDF_NAME(Type), PDF_NAME(Annot));
    pdf_dict_put(ctx, widget, PDF_NAME(Subtype), PDF_NAME(Widget));
    pdf_dict_put(ctx, widget, PDF_NAME(FT), PDF_NAME(Tx));
    pdf_dict_put_text_string(ctx, widget, PDF_NAME(T), "same");
    pdf_dict_put_text_string(ctx, widget, PDF_NAME(V), "FORM-SECRET");
    pdf_dict_put_rect(ctx, widget, PDF_NAME(Rect), fz_make_rect(20, 20, 100, 40));
    pdf_dict_put(ctx, widget, PDF_NAME(P), p0);
    pdf_obj* ref = pdf_add_object(ctx, doc, widget);
    pdf_array_push(ctx, annots, ref);
    pdf_array_push(ctx, fields, ref);
    pdf_drop_obj(ctx, ref);
    pdf_drop_obj(ctx, widget);
    pdf_obj* comment = pdf_array_push_dict(ctx, annots, 3);
    pdf_dict_put(ctx, comment, PDF_NAME(Subtype), PDF_NAME(Text));
    pdf_dict_put_rect(ctx, comment, PDF_NAME(Rect), fz_make_rect(5, 5, 15, 15));
    pdf_dict_put_text_string(ctx, comment, PDF_NAME(Contents), "COMMENT-SECRET");
    pdf_obj* link = pdf_array_push_dict(ctx, annots, 3);
    pdf_dict_put(ctx, link, PDF_NAME(Subtype), PDF_NAME(Link));
    pdf_dict_put_rect(ctx, link, PDF_NAME(Rect), fz_make_rect(10, 50, 90, 65));
    pdf_obj* dest = pdf_dict_put_array(ctx, link, PDF_NAME(Dest), 2);
    pdf_array_push(ctx, dest, p1);
    pdf_array_push(ctx, dest, PDF_NAME(Fit));
    pdf_obj* outlines = pdf_new_dict(ctx, doc, 3);
    pdf_obj* outlineRef = pdf_add_object(ctx, doc, outlines);
    pdf_obj* item = pdf_new_dict(ctx, doc, 3);
    pdf_dict_put_text_string(ctx, item, PDF_NAME(Title), "Target");
    pdf_dict_put(ctx, item, PDF_NAME(Dest), dest);
    pdf_dict_put(ctx, item, PDF_NAME(Parent), outlineRef);
    pdf_obj* itemRef = pdf_add_object(ctx, doc, item);
    pdf_dict_put(ctx, outlines, PDF_NAME(First), itemRef);
    pdf_dict_put(ctx, outlines, PDF_NAME(Last), itemRef);
    pdf_dict_put_int(ctx, outlines, PDF_NAME(Count), 1);
    pdf_dict_put(ctx, root, PDF_NAME(Outlines), outlineRef);
    pdf_drop_obj(ctx, itemRef);
    pdf_drop_obj(ctx, item);
    pdf_drop_obj(ctx, outlineRef);
    pdf_drop_obj(ctx, outlines);
    fz_buffer* data = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)"ATTACHMENT-SECRET", 17);
    pdf_obj* spec = pdf_add_embedded_file(ctx, doc, "note.txt", "text/plain", data, -1, -1, 0);
    pdf_obj* names = pdf_dict_put_dict(ctx, root, PDF_NAME(Names), 1);
    pdf_obj* tree = pdf_dict_put_dict(ctx, names, PDF_NAME(EmbeddedFiles), 1);
    pdf_obj* entries = pdf_dict_put_array(ctx, tree, PDF_NAME(Names), 2);
    pdf_array_push_text_string(ctx, entries, "note.txt");
    pdf_array_push(ctx, entries, spec);
    pdf_drop_obj(ctx, spec);
    fz_drop_buffer(ctx, data);
    pdf_obj* action = pdf_dict_put_dict(ctx, root, PDF_NAME(OpenAction), 2);
    pdf_dict_put(ctx, action, PDF_NAME(S), PDF_NAME(JavaScript));
    pdf_dict_put_text_string(ctx, action, PDF_NAME(JS), "JS-SECRET");
    pdf_obj* info = pdf_dict_put_dict(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info), 1);
    pdf_dict_put_text_string(ctx, info, PDF_NAME(Author), "AUTHOR-SECRET");
    pdf_write_options opts = pdf_default_write_options;
    pdf_save_document(ctx, doc, path, &opts);
    pdf_drop_obj(ctx, res);
    pdf_drop_document(ctx, doc);
}

static bool HasText(fz_context* ctx, pdf_document* doc, const char* needle) {
    bool found = false;
    for (int i = 0; i < pdf_count_pages(ctx, doc); i++) {
        fz_page* page = (fz_page*)pdf_load_page(ctx, doc, i);
        fz_stext_page* text = fz_new_stext_page_from_page(ctx, page, nullptr);
        fz_buffer* buf = fz_new_buffer_from_stext_page(ctx, text);
        found |= strstr(fz_string_from_buffer(ctx, buf), needle) != nullptr;
        fz_drop_buffer(ctx, buf);
        fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, page);
    }
    return found;
}

int main() {
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    fz_register_document_handlers(ctx);
    fz_try(ctx) {
        Fixture(ctx, "tests/tmp/pdf-document-tools/source.pdf");
        Vec<PdfMergeSource> sources;
        VecAppend(sources, PdfMergeSource{StrL("tests/tmp/pdf-document-tools/source.pdf"), {}});
        VecAppend(sources, sources[0]);
        Vec<PdfMergePage> pages;
        VecAppend(pages, PdfMergePage{0, 1});
        VecAppend(pages, PdfMergePage{1, 2});
        VecAppend(pages, PdfMergePage{1, 1});
        VecAppend(pages, PdfMergePage{0, 2});
        Check(EngineMupdfMergePdfs(sources, pages, StrL("tests/tmp/pdf-document-tools/merged.pdf")), "merge");
        pdf_document* doc = pdf_open_document(ctx, "tests/tmp/pdf-document-tools/merged.pdf");
        pdf_obj* root = Root(ctx, doc);
        pdf_obj* fields = pdf_dict_getp(ctx, root, "AcroForm/Fields");
        Check(pdf_array_len(ctx, fields) == 2, "saved AcroForm roots survive");
        Check(strcmp(pdf_dict_get_text_string(ctx, pdf_array_get(ctx, fields, 0), PDF_NAME(T)),
                     pdf_dict_get_text_string(ctx, pdf_array_get(ctx, fields, 1), PDF_NAME(T))) != 0, "unique field names");
        pdf_obj* annots = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, doc, 2), PDF_NAME(Annots));
        Check(pdf_array_len(ctx, annots) == 3, "widget, comment and link survive import");
        pdf_obj* target = pdf_array_get(ctx, pdf_dict_get(ctx, pdf_array_get(ctx, annots, 2), PDF_NAME(Dest)), 0);
        Check(pdf_to_num(ctx, target) == pdf_to_num(ctx, pdf_lookup_page_obj(ctx, doc, 1)), "internal link remapped");
        pdf_obj* bookmarks = pdf_dict_get(ctx, root, PDF_NAME(Outlines));
        Check(pdf_dict_get(ctx, pdf_dict_get(ctx, bookmarks, PDF_NAME(First)), PDF_NAME(Next)) != nullptr, "both outlines survive");
        pdf_obj* files = pdf_load_name_tree(ctx, doc, PDF_NAME(EmbeddedFiles));
        Check(pdf_dict_len(ctx, files) == 2, "both attachments survive collision");
        pdf_drop_obj(ctx, files);
        pdf_drop_document(ctx, doc);
        PdfSanitizeOptions opts;
        Str error;
        Check(EngineMupdfSanitizePdf(sources[0], StrL("tests/tmp/pdf-document-tools/clean.pdf"), opts, &error), "sanitize");
        doc = pdf_open_document(ctx, "tests/tmp/pdf-document-tools/clean.pdf");
        root = Root(ctx, doc);
        Check(!pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info)), "metadata removed");
        Check(!pdf_dict_get(ctx, root, PDF_NAME(AcroForm)), "form data removed");
        Check(!pdf_dict_getp(ctx, root, "Names/EmbeddedFiles"), "attachments removed");
        Check(!pdf_dict_get(ctx, root, PDF_NAME(OpenAction)), "JavaScript removed");
        Check(HasText(ctx, doc, "ID-123"), "sanitation leaves visible text by default");
        pdf_drop_document(ctx, doc);
        int count = 0;
        Check(EngineMupdfRedactMatches(sources[0], StrL("tests/tmp/pdf-document-tools/redacted.pdf"), StrL("ID-[0-9]+"),
                                      PdfSearchMode::Regex, &count, &error), "redact regex");
        Check(count == 4, "all repeated identifiers matched");
        doc = pdf_open_document(ctx, "tests/tmp/pdf-document-tools/redacted.pdf");
        Check(!HasText(ctx, doc, "ID-"), "redacted text absent after reopen");
        Check(HasText(ctx, doc, "public"), "unmatched content kept");
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fprintf(stderr, "FAIL: %s\n", fz_caught_message(ctx));
        return 1;
    }
    fz_drop_context(ctx);
    puts("pdf-document-tools: PASS");
    return 0;
}
