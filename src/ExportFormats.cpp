/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Zip.h"
#include "base/Pixmap.h"
#include "base/Win.h"
#include <richedit.h>
#include <shlwapi.h>
#include <mupdf/pdf.h>
#include "MarkdownToc.h"
#include "ExportFormats.h"

static const Str kRelStart = StrL(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
static const Str kTypesStart = StrL(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types "
    "xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" "
    "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" "
    "ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/>");
static const Str kWordNs = StrL(
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
static const Str kSlideNs = StrL(
    "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
static const Str kSheetNs = StrL(
    "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");

static void Xml(str::Builder& out, Str s) {
    for (int charIndex = 0; charIndex < len(s); charIndex++) {
        char c = s.s[charIndex];
        switch (c) {
            case '&':
                out.Append(StrL("&amp;"));
                break;
            case '<':
                out.Append(StrL("&lt;"));
                break;
            case '>':
                out.Append(StrL("&gt;"));
                break;
            case '"':
                out.Append(StrL("&quot;"));
                break;
            default:
                if ((u8)c >= 32 || c == '\t' || c == '\n' || c == '\r') out.AppendChar(c);
                break;
        }
    }
}

static void Json(str::Builder& out, Str s) {
    out.AppendChar('"');
    for (int charIndex = 0; charIndex < len(s); charIndex++) {
        char c = s.s[charIndex];
        if (c == '"' || c == '\\') {
            out.AppendChar('\\');
            out.AppendChar(c);
        } else if ((u8)c < 32)
            out.Append(fmt("\\u%04x", (int)(u8)c));
        else
            out.AppendChar(c);
    }
    out.AppendChar('"');
}

static void Rune(str::Builder& out, int c) {
    char utf8[8];
    if (c < 0 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) c = 0xfffd;
    int n = fz_runetochar(utf8, c);
    out.Append(Str(utf8, n));
}

static bool Cancelled(ConversionProgress& p) {
    return InterlockedCompareExchange(&p.cancelled, 0, 0) != 0;
}

static void CheckCancel(fz_context* ctx, ConversionProgress& p) {
    if (Cancelled(p)) fz_throw(ctx, FZ_ERROR_ABORT, "Conversion cancelled.");
}

static bool Fail(ConversionProgress& p, Str message) {
    str::Free(p.error);
    p.error = str::Dup(message);
    return false;
}

static bool PageRange(Str spec, int count, Vec<int>& pages) {
    if (!len(spec) || str::EqI(spec, StrL("all"))) {
        for (int i = 1; i <= count; i++) VecAppend(pages, i);
        return count > 0;
    }
    const char* s = CStrTemp(spec);
    while (*s) {
        while (*s == ' ') s++;
        if (*s < '0' || *s > '9') return false;
        char* end;
        long first = strtol(s, &end, 10);
        s = end;
        while (*s == ' ') s++;
        long last = first;
        if (*s == '-') {
            s++;
            while (*s == ' ') s++;
            if (*s < '0' || *s > '9') return false;
            last = strtol(s, &end, 10);
            s = end;
        }
        if (first < 1 || last < first || last > count) return false;
        for (int i = (int)first; i <= last; i++)
            if (!VecContains(pages, i)) VecAppend(pages, i);
        while (*s == ' ') s++;
        if (!*s) break;
        if (*s++ != ',' || !*s) return false;
    }
    return len(pages) > 0;
}

struct ExportPage {
    fz_context* ctx;
    fz_page* page = nullptr;
    fz_stext_page* text = nullptr;
    fz_link* links = nullptr;
    Vec<float> xs, ys;
    Vec<fz_stext_block*> blocks;
    int number = 0;
    float bodySize = 12;
    explicit ExportPage(fz_context* c) : ctx(c) {}
    ~ExportPage() {
        fz_drop_link(ctx, links);
        fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, page);
    }
};

struct ExportDoc {
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    fz_document* doc = nullptr;
    Vec<ExportPage*> pages;
    ~ExportDoc() {
        for (auto* p : pages) delete p;
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
};

static int FloatCmp(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y ? 1 : 0;
}
static void Axis(Vec<float>& axis, float v) {
    for (float p : axis)
        if (fabsf(p - v) < 2) return;
    VecAppend(axis, v);
}
static void CollectBlocks(ExportPage& p, fz_stext_block* block) {
    for (; block; block = block->next) {
        if (block->type == FZ_STEXT_BLOCK_STRUCT) {
            if (block->u.s.down) CollectBlocks(p, block->u.s.down->first_block);
        } else
            VecAppend(p.blocks, block);
    }
}
static void FindGrid(ExportPage& p) {
    int counts[100]{};
    for (auto* b : p.blocks) {
        if (b->type == FZ_STEXT_BLOCK_TEXT) {
            for (auto* l = b->u.t.first_line; l; l = l->next)
                for (auto* c = l->first_char; c; c = c->next) {
                    int size = (int)c->size;
                    if (size > 0 && size < 100) counts[size]++;
                }
        }
        if (b->type != FZ_STEXT_BLOCK_VECTOR) continue;
        fz_rect r = b->bbox;
        float w = r.x1 - r.x0, h = r.y1 - r.y0;
        if (w > 15 && h < 3)
            Axis(p.ys, (r.y0 + r.y1) / 2);
        else if (h > 15 && w < 3)
            Axis(p.xs, (r.x0 + r.x1) / 2);
        else if (w > 15 && h > 8 && (b->u.v.flags & FZ_STEXT_VECTOR_IS_RECTANGLE)) {
            Axis(p.xs, r.x0);
            Axis(p.xs, r.x1);
            Axis(p.ys, r.y0);
            Axis(p.ys, r.y1);
        }
    }
    int best = 0;
    for (int i = 1; i < 100; i++)
        if (counts[i] > best) {
            best = counts[i];
            p.bodySize = (float)i;
        }
    if (len(p.xs) < 3 || len(p.ys) < 3 || len(p.xs) > 128 || len(p.ys) > 512) {
        VecReset(p.xs);
        VecReset(p.ys);
        return;
    }
    qsort(p.xs.els, len(p.xs), sizeof(float), FloatCmp);
    qsort(p.ys.els, len(p.ys), sizeof(float), FloatCmp);
}
static bool InGrid(ExportPage& p, fz_rect r) {
    if (!len(p.xs)) return false;
    float x = (r.x0 + r.x1) / 2, y = (r.y0 + r.y1) / 2;
    return x >= p.xs[0] && x <= p.xs[len(p.xs) - 1] && y >= p.ys[0] && y <= p.ys[len(p.ys) - 1];
}
static void CellText(ExportPage& p, int row, int col, str::Builder& text) {
    for (auto* b : p.blocks) {
        if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
        for (auto* l = b->u.t.first_line; l; l = l->next) {
            bool started = false;
            for (auto* c = l->first_char; c; c = c->next) {
                fz_rect r = fz_rect_from_quad(c->quad);
                float x = (r.x0 + r.x1) / 2, y = (r.y0 + r.y1) / 2;
                if (x < p.xs[col] || x >= p.xs[col + 1] || y < p.ys[row] || y >= p.ys[row + 1]) continue;
                if (!started && len(text)) text.AppendChar(' ');
                started = true;
                Rune(text, c->c);
            }
        }
    }
}
static void BlockText(fz_stext_block* b, str::Builder& text) {
    for (auto* l = b->u.t.first_line; l; l = l->next) {
        if (len(text)) text.AppendChar(' ');
        for (auto* c = l->first_char; c; c = c->next) Rune(text, c->c);
    }
}
static Str LinkFor(ExportPage& p, fz_rect r) {
    for (fz_link* l = p.links; l; l = l->next) {
        if (fz_is_empty_rect(fz_intersect_rect(r, l->rect)) || !l->uri) continue;
        Str uri(l->uri);
        if (str::StartsWithI(uri, StrL("https://")) || str::StartsWithI(uri, StrL("http://")) ||
            str::StartsWithI(uri, StrL("mailto:")))
            return uri;
    }
    return {};
}
static int Heading(ExportPage& p, fz_stext_block* b) {
    auto* l = b->u.t.first_line;
    float size = l && l->first_char ? l->first_char->size : p.bodySize;
    return size >= p.bodySize * 1.7f ? 1 : size >= p.bodySize * 1.25f ? 2 : 0;
}
static bool IsList(Str text) {
    return str::StartsWith(text, StrL("•")) || str::StartsWith(text, StrL("- ")) || str::StartsWith(text, StrL("* ")) ||
           (len(text) > 2 && text.s[0] >= '0' && text.s[0] <= '9' && text.s[1] == '.');
}

static bool LoadExport(ExportDoc& d, Str source, Str range, ConversionProgress& progress) {
    if (!d.ctx) return Fail(progress, StrL("Cannot allocate PDF context."));
    bool ok = false;
    Vec<int> selected;
    fz_var(ok);
    fz_try(d.ctx) {
        fz_register_document_handlers(d.ctx);
        d.doc = fz_open_document(d.ctx, CStrTemp(source));
        if (fz_needs_password(d.ctx, d.doc))
            fz_throw(d.ctx, FZ_ERROR_GENERIC, "Save an unlocked copy before exporting.");
        if (!fz_has_permission(d.ctx, d.doc, FZ_PERMISSION_COPY))
            fz_throw(d.ctx, FZ_ERROR_GENERIC, "This document does not permit text extraction.");
        if (!PageRange(range, fz_count_pages(d.ctx, d.doc), selected))
            fz_throw(d.ctx, FZ_ERROR_GENERIC, "Invalid page range. Use all, 1-3,5.");
        InterlockedExchange(&progress.total, len(selected));
        fz_stext_options opts{};
        opts.flags = FZ_STEXT_PRESERVE_IMAGES | FZ_STEXT_COLLECT_VECTORS | FZ_STEXT_PARAGRAPH_BREAK;
        for (int n : selected) {
            CheckCancel(d.ctx, progress);
            auto* p = new ExportPage(d.ctx);
            VecAppend(d.pages, p);
            p->number = n;
            p->page = fz_load_page(d.ctx, d.doc, n - 1);
            p->text = fz_new_stext_page_from_page(d.ctx, p->page, &opts);
            p->links = fz_load_links(d.ctx, p->page);
            CollectBlocks(*p, p->text->first_block);
            FindGrid(*p);
        }
        ok = true;
    }
    fz_catch(d.ctx) {
        Fail(progress, Str(fz_caught_message(d.ctx)));
    }
    return ok;
}

static void Relationship(str::Builder& out, int id, Str type, Str target, bool external = false) {
    out.Append(
        fmt("<Relationship Id=\"rId%d\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/%s\" Target=\"",
            id, type));
    Xml(out, target);
    out.Append(external ? StrL("\" TargetMode=\"External\"/>") : StrL("\"/>"));
}
static void ContentType(str::Builder& out, Str part, Str type) {
    out.Append(fmt("<Override PartName=\"/%s\" ContentType=\"application/vnd.openxmlformats-officedocument.%s+xml\"/>",
                   part, type));
}
static bool Part(ZipCreator& zip, Str name, str::Builder& data) {
    return zip.AddFileData(name, ToStr(data));
}

static Str ImagePng(fz_context* ctx, fz_image* image) {
    fz_buffer* buffer = nullptr;
    Str result;
    fz_var(buffer);
    fz_try(ctx) {
        buffer = fz_new_buffer_from_image_as_png(ctx, image, fz_default_color_params);
        unsigned char* bytes;
        size_t n = fz_buffer_storage(ctx, buffer, &bytes);
        if (n <= INT_MAX) result = str::Dup(Str((char*)bytes, (int)n));
    }
    fz_always(ctx) {
        fz_drop_buffer(ctx, buffer);
    }
    fz_catch(ctx) {
        return {};
    }
    return result;
}
static void Base64(str::Builder& out, Str data) {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < len(data); i += 3) {
        unsigned n = (u8)data.s[i] << 16;
        if (i + 1 < len(data)) n |= (u8)data.s[i + 1] << 8;
        if (i + 2 < len(data)) n |= (u8)data.s[i + 2];
        out.AppendChar(chars[n >> 18]);
        out.AppendChar(chars[(n >> 12) & 63]);
        out.AppendChar(i + 1 < len(data) ? chars[(n >> 6) & 63] : '=');
        out.AppendChar(i + 2 < len(data) ? chars[n & 63] : '=');
    }
}
static void WordParagraph(str::Builder& xml, Str text, int heading = 0, int link = 0) {
    xml.Append(StrL("<w:p>"));
    if (heading)
        xml.Append(fmt("<w:pPr><w:outlineLvl w:val=\"%d\"/></w:pPr>", heading - 1));
    else if (IsList(text))
        xml.Append(StrL("<w:pPr><w:ind w:left=\"360\" w:hanging=\"180\"/></w:pPr>"));
    if (link) xml.Append(fmt("<w:hyperlink r:id=\"rId%d\">", link));
    xml.Append(StrL("<w:r>"));
    if (heading) xml.Append(fmt("<w:rPr><w:b/><w:sz w:val=\"%d\"/></w:rPr>", heading == 1 ? 36 : 28));
    xml.Append(StrL("<w:t xml:space=\"preserve\">"));
    Xml(xml, text);
    xml.Append(StrL("</w:t></w:r>"));
    if (link) xml.Append(StrL("</w:hyperlink>"));
    xml.Append(StrL("</w:p>"));
}
static bool WriteDocx(ExportDoc& d, ZipCreator& zip, ConversionProgress& p) {
    str::Builder xml, rels, types, root;
    xml.Append(fmt("<?xml version=\"1.0\" encoding=\"UTF-8\"?><w:document %s><w:body>", kWordNs));
    rels.Append(kRelStart);
    types.Append(kTypesStart);
    root.Append(kRelStart);
    int id = 1, imageId = 1;
    bool ok = true;
    for (auto* page : d.pages) {
        if (Cancelled(p)) return false;
        if (page != d.pages[0]) xml.Append(StrL("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>"));
        for (auto* b : page->blocks) {
            if (b->type == FZ_STEXT_BLOCK_TEXT && !InGrid(*page, b->bbox)) {
                str::Builder text;
                BlockText(b, text);
                Str link = LinkFor(*page, b->bbox);
                int linkId = len(link) ? id++ : 0;
                if (linkId) Relationship(rels, linkId, StrL("hyperlink"), link, true);
                WordParagraph(xml, ToStr(text), Heading(*page, b), linkId);
            } else if (b->type == FZ_STEXT_BLOCK_IMAGE) {
                Str data = ImagePng(d.ctx, b->u.i.image);
                if (!len(data)) return false;
                ok = zip.AddFileData(fmt("word/media/image%d.png", imageId), data) && ok;
                str::Free(data);
                Relationship(rels, id, StrL("image"), fmt("media/image%d.png", imageId));
                float width = b->bbox.x1 - b->bbox.x0, height = b->bbox.y1 - b->bbox.y0;
                if (width > 468) {
                    height *= 468 / width;
                    width = 468;
                }
                int cx = (int)(width * 12700), cy = (int)(height * 12700);
                xml.Append(fmt(
                    "<w:p><w:r><w:drawing><wp:inline "
                    "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\"><wp:extent "
                    "cx=\"%d\" cy=\"%d\"/><wp:docPr id=\"%d\" name=\"Image %d\"/><a:graphic "
                    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><a:graphicData "
                    "uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic "
                    "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:nvPicPr><pic:cNvPr "
                    "id=\"%d\" name=\"Image\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip "
                    "r:embed=\"rId%d\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off "
                    "x=\"0\" y=\"0\"/><a:ext cx=\"%d\" cy=\"%d\"/></a:xfrm><a:prstGeom "
                    "prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></"
                    "wp:inline></w:drawing></w:r></w:p>",
                    cx, cy, imageId, imageId, imageId, id++, cx, cy));
                imageId++;
            }
        }
        if (len(page->xs)) {
            xml.Append(StrL(
                "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders><w:top w:val=\"single\"/><w:left "
                "w:val=\"single\"/><w:bottom w:val=\"single\"/><w:right w:val=\"single\"/><w:insideH "
                "w:val=\"single\"/><w:insideV w:val=\"single\"/></w:tblBorders></w:tblPr><w:tblGrid>"));
            for (int c = 0; c + 1 < len(page->xs); c++)
                xml.Append(fmt("<w:gridCol w:w=\"%d\"/>", (int)((page->xs[c + 1] - page->xs[c]) * 20)));
            xml.Append(StrL("</w:tblGrid>"));
            for (int r = 0; r + 1 < len(page->ys); r++) {
                xml.Append(StrL("<w:tr>"));
                for (int c = 0; c + 1 < len(page->xs); c++) {
                    str::Builder text;
                    CellText(*page, r, c, text);
                    xml.Append(StrL("<w:tc>"));
                    WordParagraph(xml, ToStr(text));
                    xml.Append(StrL("</w:tc>"));
                }
                xml.Append(StrL("</w:tr>"));
            }
            xml.Append(StrL("</w:tbl>"));
        }
        InterlockedIncrement(&p.completed);
    }
    xml.Append(
        StrL("<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" "
             "w:bottom=\"1440\" w:left=\"1440\"/></w:sectPr></w:body></w:document>"));
    rels.Append(StrL("</Relationships>"));
    Relationship(root, 1, StrL("officeDocument"), StrL("word/document.xml"));
    root.Append(StrL("</Relationships>"));
    ContentType(types, StrL("word/document.xml"), StrL("wordprocessingml.document.main"));
    types.Append(StrL("</Types>"));
    return Part(zip, StrL("word/document.xml"), xml) && Part(zip, StrL("word/_rels/document.xml.rels"), rels) &&
           Part(zip, StrL("_rels/.rels"), root) && Part(zip, StrL("[Content_Types].xml"), types) && ok;
}

static void SheetCell(str::Builder& xml, int row, int col, Str text) {
    char name[10];
    int n = 0;
    do {
        name[n++] = 'A' + col % 26;
        col = col / 26 - 1;
    } while (col >= 0);
    xml.Append(StrL("<c r=\""));
    while (n) xml.AppendChar(name[--n]);
    xml.Append(fmt("%d\" t=\"inlineStr\"><is><t xml:space=\"preserve\">", row));
    Xml(xml, text);
    xml.Append(StrL("</t></is></c>"));
}
static bool WriteXlsx(ExportDoc& d, ZipCreator& zip, ConversionProgress& p) {
    str::Builder book, rels, root, types;
    book.Append(fmt("<?xml version=\"1.0\" encoding=\"UTF-8\"?><workbook %s><sheets>", kSheetNs));
    rels.Append(kRelStart);
    root.Append(kRelStart);
    types.Append(kTypesStart);
    bool ok = true;
    int sheet = 0;
    for (auto* page : d.pages) {
        if (Cancelled(p)) return false;
        sheet++;
        book.Append(fmt("<sheet name=\"Page %d\" sheetId=\"%d\" r:id=\"rId%d\"/>", page->number, sheet, sheet));
        Relationship(rels, sheet, StrL("worksheet"), fmt("worksheets/sheet%d.xml", sheet));
        ContentType(types, fmt("xl/worksheets/sheet%d.xml", sheet), StrL("spreadsheetml.worksheet"));
        str::Builder xml;
        xml.Append(fmt("<?xml version=\"1.0\" encoding=\"UTF-8\"?><worksheet %s><sheetData>", kSheetNs));
        int row = 0;
        if (len(page->xs)) {
            for (int r = 0; r + 1 < len(page->ys); r++) {
                xml.Append(fmt("<row r=\"%d\">", ++row));
                for (int c = 0; c + 1 < len(page->xs); c++) {
                    str::Builder text;
                    CellText(*page, r, c, text);
                    SheetCell(xml, row, c, ToStr(text));
                }
                xml.Append(StrL("</row>"));
            }
        }
        for (auto* b : page->blocks) {
            if (b->type != FZ_STEXT_BLOCK_TEXT || InGrid(*page, b->bbox)) continue;
            for (auto* l = b->u.t.first_line; l; l = l->next) {
                str::Builder text;
                for (auto* c = l->first_char; c; c = c->next) Rune(text, c->c);
                xml.Append(fmt("<row r=\"%d\">", ++row));
                SheetCell(xml, row, 0, ToStr(text));
                xml.Append(StrL("</row>"));
            }
        }
        xml.Append(StrL("</sheetData></worksheet>"));
        ok = Part(zip, fmt("xl/worksheets/sheet%d.xml", sheet), xml) && ok;
        InterlockedIncrement(&p.completed);
    }
    book.Append(StrL("</sheets></workbook>"));
    rels.Append(StrL("</Relationships>"));
    Relationship(root, 1, StrL("officeDocument"), StrL("xl/workbook.xml"));
    root.Append(StrL("</Relationships>"));
    ContentType(types, StrL("xl/workbook.xml"), StrL("spreadsheetml.sheet.main"));
    types.Append(StrL("</Types>"));
    return Part(zip, StrL("xl/workbook.xml"), book) && Part(zip, StrL("xl/_rels/workbook.xml.rels"), rels) &&
           Part(zip, StrL("_rels/.rels"), root) && Part(zip, StrL("[Content_Types].xml"), types) && ok;
}

static const Str kShapeTree = StrL(
    "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x=\"0\" "
    "y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>");
static void SlideTransform(str::Builder& xml, fz_rect r, fz_rect bounds, float sx, float sy) {
    xml.Append(fmt("<a:xfrm><a:off x=\"%d\" y=\"%d\"/><a:ext cx=\"%d\" cy=\"%d\"/></a:xfrm>",
                   (int)((r.x0 - bounds.x0) * sx), (int)((r.y0 - bounds.y0) * sy), (int)((r.x1 - r.x0) * sx),
                   (int)((r.y1 - r.y0) * sy)));
}
static bool WritePptx(ExportDoc& d, ZipCreator& zip, ConversionProgress& p) {
    str::Builder pres, rels, root, types;
    pres.Append(
        fmt("<?xml version=\"1.0\" encoding=\"UTF-8\"?><p:presentation %s><p:sldMasterIdLst><p:sldMasterId "
            "id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst><p:sldIdLst>",
            kSlideNs));
    rels.Append(kRelStart);
    root.Append(kRelStart);
    types.Append(kTypesStart);
    Relationship(rels, 1, StrL("slideMaster"), StrL("slideMasters/slideMaster1.xml"));
    int slide = 0;
    bool ok = true;
    for (auto* page : d.pages) {
        if (Cancelled(p)) return false;
        slide++;
        pres.Append(fmt("<p:sldId id=\"%d\" r:id=\"rId%d\"/>", 255 + slide, slide + 1));
        Relationship(rels, slide + 1, StrL("slide"), fmt("slides/slide%d.xml", slide));
        ContentType(types, fmt("ppt/slides/slide%d.xml", slide), StrL("presentationml.slide"));
        str::Builder xml, sr;
        xml.Append(
            fmt("<?xml version=\"1.0\" encoding=\"UTF-8\"?><p:sld %s><p:cSld><p:spTree>%s", kSlideNs, kShapeTree));
        sr.Append(kRelStart);
        Relationship(sr, 1, StrL("slideLayout"), StrL("../slideLayouts/slideLayout1.xml"));
        int id = 2;
        fz_rect bounds = page->text->mediabox;
        float sx = 9144000 / (bounds.x1 - bounds.x0), sy = 6858000 / (bounds.y1 - bounds.y0);
        for (auto* b : page->blocks) {
            if (b->type == FZ_STEXT_BLOCK_TEXT) {
                Str link = LinkFor(*page, b->bbox);
                if (len(link)) Relationship(sr, id, StrL("hyperlink"), link, true);
                xml.Append(
                    fmt("<p:sp><p:nvSpPr><p:cNvPr id=\"%d\" name=\"Text %d\"/><p:cNvSpPr "
                        "txBox=\"1\"/><p:nvPr/></p:nvSpPr><p:spPr>",
                        id, id));
                SlideTransform(xml, b->bbox, bounds, sx, sy);
                xml.Append(
                    StrL("<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/></p:spPr><p:txBody><a:bodyPr "
                         "wrap=\"none\" lIns=\"0\" tIns=\"0\" rIns=\"0\" "
                         "bIns=\"0\"><a:spAutoFit/></a:bodyPr><a:lstStyle/>"));
                for (auto* l = b->u.t.first_line; l; l = l->next) {
                    str::Builder text;
                    for (auto* c = l->first_char; c; c = c->next) Rune(text, c->c);
                    int size = (int)((l->first_char ? l->first_char->size : 12) * sy / 127);
                    size = size < 100 ? 100 : size > 400000 ? 400000 : size;
                    xml.Append(fmt("<a:p><a:r><a:rPr lang=\"en-US\" sz=\"%d\">", size));
                    if (len(link)) xml.Append(fmt("<a:hlinkClick r:id=\"rId%d\"/>", id));
                    xml.Append(StrL("</a:rPr><a:t>"));
                    Xml(xml, ToStr(text));
                    xml.Append(StrL("</a:t></a:r></a:p>"));
                }
                xml.Append(StrL("</p:txBody></p:sp>"));
                id++;
            } else if (b->type == FZ_STEXT_BLOCK_IMAGE) {
                Str png = ImagePng(d.ctx, b->u.i.image);
                if (!len(png)) return false;
                ok = zip.AddFileData(fmt("ppt/media/image%d-%d.png", slide, id), png) && ok;
                str::Free(png);
                Relationship(sr, id, StrL("image"), fmt("../media/image%d-%d.png", slide, id));
                xml.Append(
                    fmt("<p:pic><p:nvPicPr><p:cNvPr id=\"%d\" name=\"Image "
                        "%d\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr><p:blipFill><a:blip "
                        "r:embed=\"rId%d\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>",
                        id, id, id));
                SlideTransform(xml, b->bbox, bounds, sx, sy);
                xml.Append(StrL("<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>"));
                id++;
            }
        }
        xml.Append(StrL("</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>"));
        sr.Append(StrL("</Relationships>"));
        ok = Part(zip, fmt("ppt/slides/slide%d.xml", slide), xml) &&
             Part(zip, fmt("ppt/slides/_rels/slide%d.xml.rels", slide), sr) && ok;
        InterlockedIncrement(&p.completed);
    }
    pres.Append(
        StrL("</p:sldIdLst><p:sldSz cx=\"9144000\" cy=\"6858000\"/><p:notesSz cx=\"6858000\" "
             "cy=\"9144000\"/></p:presentation>"));
    rels.Append(StrL("</Relationships>"));
    str::Builder master, mr, layout, lr;
    master.Append(
        fmt("<p:sldMaster %s><p:cSld><p:spTree>%s</p:spTree></p:cSld><p:clrMap accent1=\"accent1\" accent2=\"accent2\" "
            "accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" bg1=\"lt1\" bg2=\"lt2\" "
            "folHlink=\"folHlink\" hlink=\"hlink\" tx1=\"dk1\" tx2=\"dk2\"/><p:sldLayoutIdLst><p:sldLayoutId "
            "id=\"2147483649\" "
            "r:id=\"rId1\"/></p:sldLayoutIdLst><p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles></"
            "p:sldMaster>",
            kSlideNs, kShapeTree));
    layout.Append(fmt(
        "<p:sldLayout %s type=\"blank\" preserve=\"1\"><p:cSld "
        "name=\"Blank\"><p:spTree>%s</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>",
        kSlideNs, kShapeTree));
    mr.Append(kRelStart);
    Relationship(mr, 1, StrL("slideLayout"), StrL("../slideLayouts/slideLayout1.xml"));
    mr.Append(StrL("</Relationships>"));
    lr.Append(kRelStart);
    Relationship(lr, 1, StrL("slideMaster"), StrL("../slideMasters/slideMaster1.xml"));
    lr.Append(StrL("</Relationships>"));
    ContentType(types, StrL("ppt/slideMasters/slideMaster1.xml"), StrL("presentationml.slideMaster"));
    ContentType(types, StrL("ppt/slideLayouts/slideLayout1.xml"), StrL("presentationml.slideLayout"));
    ContentType(types, StrL("ppt/presentation.xml"), StrL("presentationml.presentation.main"));
    types.Append(StrL("</Types>"));
    Relationship(root, 1, StrL("officeDocument"), StrL("ppt/presentation.xml"));
    root.Append(StrL("</Relationships>"));
    return Part(zip, StrL("ppt/presentation.xml"), pres) && Part(zip, StrL("ppt/_rels/presentation.xml.rels"), rels) &&
           Part(zip, StrL("ppt/slideMasters/slideMaster1.xml"), master) &&
           Part(zip, StrL("ppt/slideMasters/_rels/slideMaster1.xml.rels"), mr) &&
           Part(zip, StrL("ppt/slideLayouts/slideLayout1.xml"), layout) &&
           Part(zip, StrL("ppt/slideLayouts/_rels/slideLayout1.xml.rels"), lr) &&
           Part(zip, StrL("_rels/.rels"), root) && Part(zip, StrL("[Content_Types].xml"), types) && ok;
}

static void RtfText(str::Builder& out, Str text) {
    TempWStr wide = ToWStrTemp(text);
    for (int charIndex = 0; charIndex < len(wide); charIndex++) {
        WCHAR c = wide.s[charIndex];
        if (c == '\\' || c == '{' || c == '}') {
            out.AppendChar('\\');
            out.AppendChar((char)c);
        } else if (c == '\n')
            out.Append(StrL("\\line "));
        else if (c == '\t')
            out.Append(StrL("\\tab "));
        else if (c >= 32 && c < 127)
            out.AppendChar((char)c);
        else
            out.Append(fmt("\\u%d?", (int)(short)c));
    }
}
static void MarkdownText(str::Builder& out, Str text) {
    for (int charIndex = 0; charIndex < len(text); charIndex++) {
        char c = text.s[charIndex];
        if (c == '\\' || c == '*' || c == '_' || c == '[' || c == ']' || c == '<' || c == '>' || c == '`' || c == '|')
            out.AppendChar('\\');
        out.AppendChar(c);
    }
}
static bool WriteTextFormats(ExportDoc& d, ExportFormat format, str::Builder& out, ConversionProgress& p) {
    bool html = format == ExportFormat::Html, md = format == ExportFormat::Markdown;
    bool json = format == ExportFormat::Json, rtf = format == ExportFormat::Rtf;
    if (html)
        out.Append(
            StrL("<!doctype html><html><head><meta charset=\"utf-8\"><title>PDF "
                 "export</title><style>body{max-width:60em;margin:2em "
                 "auto;font-family:sans-serif}table{border-collapse:collapse}td{border:1px "
                 "solid;padding:.4em}img{max-width:100%}section{break-after:page}</style></head><body>"));
    if (json) out.Append(StrL("{\"coordinateSystem\":\"points, top-left origin\",\"pages\":["));
    if (rtf) out.Append(StrL("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\uc1\\fs24 "));
    int pageIdx = 0;
    for (auto* page : d.pages) {
        if (Cancelled(p)) return false;
        if (html) out.Append(fmt("<section data-page=\"%d\">", page->number));
        if (json) {
            if (pageIdx) out.AppendChar(',');
            fz_rect b = page->text->mediabox;
            out.Append(fmt("{\"page\":%d,\"bbox\":[%g,%g,%g,%g],\"blocks\":[", page->number, b.x0, b.y0, b.x1, b.y1));
        } else if (pageIdx && rtf)
            out.Append(StrL("\\page "));
        else if (pageIdx && !html)
            out.Append(md ? StrL("\n\n---\n\n") : StrL("\n\f\n"));
        bool comma = false;
        for (auto* b : page->blocks) {
            if (b->type == FZ_STEXT_BLOCK_IMAGE && (html || md || rtf || json)) {
                Str png = ImagePng(d.ctx, b->u.i.image);
                if (!len(png)) return false;
                if (html) out.Append(StrL("<p><img alt=\"Extracted image\" src=\"data:image/png;base64,"));
                if (md) out.Append(StrL("\n![Extracted image](data:image/png;base64,"));
                if (json) {
                    if (comma) out.AppendChar(',');
                    comma = true;
                    out.Append(fmt("{\"type\":\"image\",\"bbox\":[%g,%g,%g,%g],\"pngBase64\":\"", b->bbox.x0,
                                   b->bbox.y0, b->bbox.x1, b->bbox.y1));
                }
                if (rtf) {
                    out.Append(StrL("{\\pict\\pngblip "));
                    for (int byteIndex = 0; byteIndex < len(png); byteIndex++)
                        out.Append(fmt("%02x", (int)(u8)png.s[byteIndex]));
                    out.Append(StrL("}\\par\n"));
                } else
                    Base64(out, png);
                str::Free(png);
                if (html) out.Append(StrL("\"></p>"));
                if (md) out.Append(StrL(")\n\n"));
                if (json) out.Append(StrL("\"}"));
                continue;
            }
            if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
            if ((html || md) && InGrid(*page, b->bbox)) continue;
            str::Builder text;
            BlockText(b, text);
            Str s = ToStr(text), link = LinkFor(*page, b->bbox);
            int heading = Heading(*page, b);
            if (json) {
                if (comma) out.AppendChar(',');
                comma = true;
                out.Append(fmt("{\"type\":\"text\",\"bbox\":[%g,%g,%g,%g],\"heading\":%d,\"text\":", b->bbox.x0,
                               b->bbox.y0, b->bbox.x1, b->bbox.y1, heading));
                Json(out, s);
                out.Append(StrL(",\"url\":"));
                Json(out, link);
                out.Append(StrL(",\"lines\":["));
                int li = 0;
                for (auto* l = b->u.t.first_line; l; l = l->next) {
                    if (li++) out.AppendChar(',');
                    out.Append(fmt("{\"bbox\":[%g,%g,%g,%g],\"characters\":[", l->bbox.x0, l->bbox.y0, l->bbox.x1,
                                   l->bbox.y1));
                    int ci = 0;
                    for (auto* c = l->first_char; c; c = c->next) {
                        if (ci++) out.AppendChar(',');
                        str::Builder ch;
                        Rune(ch, c->c);
                        fz_rect box = fz_rect_from_quad(c->quad);
                        out.Append(StrL("{\"text\":"));
                        Json(out, ToStr(ch));
                        out.Append(
                            fmt(",\"bbox\":[%g,%g,%g,%g],\"fontSize\":%g}", box.x0, box.y0, box.x1, box.y1, c->size));
                    }
                    out.Append(StrL("]}"));
                }
                out.Append(StrL("]}"));
            } else if (html) {
                Str tag = heading == 1 ? StrL("h1") : heading == 2 ? StrL("h2") : IsList(s) ? StrL("li") : StrL("p");
                if (IsList(s) && !heading) out.Append(StrL("<ul>"));
                out.Append(fmt("<%s>", tag));
                if (len(link)) {
                    out.Append(StrL("<a href=\""));
                    Xml(out, link);
                    out.Append(StrL("\">"));
                }
                Xml(out, s);
                if (len(link)) out.Append(StrL("</a>"));
                out.Append(fmt("</%s>", tag));
                if (IsList(s) && !heading) out.Append(StrL("</ul>"));
            } else if (md) {
                if (heading) out.Append(heading == 1 ? StrL("# ") : StrL("## "));
                if (len(link)) out.AppendChar('[');
                MarkdownText(out, s);
                if (len(link)) {
                    out.Append(StrL("](<"));
                    for (int charIndex = 0; charIndex < len(link); charIndex++) {
                        char c = link.s[charIndex];
                        if (c == '>')
                            out.Append(StrL("%3E"));
                        else if (c == '<')
                            out.Append(StrL("%3C"));
                        else
                            out.AppendChar(c);
                    }
                    out.Append(StrL(">)"));
                }
                out.Append(StrL("\n\n"));
            } else if (rtf) {
                if (heading) out.Append(StrL("{\\b\\fs32 "));
                RtfText(out, s);
                if (heading) out.AppendChar('}');
                out.Append(StrL("\\par\n"));
            } else {
                for (auto* l = b->u.t.first_line; l; l = l->next) {
                    int indent = (int)((l->bbox.x0 - page->text->mediabox.x0) / (page->bodySize * 0.5f));
                    for (int i = 0; i < indent && i < 300; i++) out.AppendChar(' ');
                    float previous = l->bbox.x0;
                    for (auto* c = l->first_char; c; c = c->next) {
                        fz_rect box = fz_rect_from_quad(c->quad);
                        int gap = (int)((box.x0 - previous) / (page->bodySize * 0.5f));
                        if (gap > 1)
                            for (int i = 0; i < gap && i < 100; i++) out.AppendChar(' ');
                        Rune(out, c->c);
                        previous = box.x1;
                    }
                    out.AppendChar('\n');
                }
                out.AppendChar('\n');
            }
        }
        if ((html || md) && len(page->xs)) {
            if (html) out.Append(StrL("<table>"));
            for (int r = 0; r + 1 < len(page->ys); r++) {
                out.Append(html ? StrL("<tr>") : StrL("|"));
                for (int c = 0; c + 1 < len(page->xs); c++) {
                    str::Builder cell;
                    CellText(*page, r, c, cell);
                    if (html) {
                        out.Append(StrL("<td>"));
                        Xml(out, ToStr(cell));
                        out.Append(StrL("</td>"));
                    } else {
                        MarkdownText(out, ToStr(cell));
                        out.AppendChar('|');
                    }
                }
                out.Append(html ? StrL("</tr>") : StrL("\n"));
                if (md && r == 0) {
                    out.AppendChar('|');
                    for (int c = 0; c + 1 < len(page->xs); c++) out.Append(StrL("---|"));
                    out.AppendChar('\n');
                }
            }
            if (html) out.Append(StrL("</table>"));
        }
        if (json) out.Append(StrL("]}"));
        if (html) out.Append(StrL("</section>"));
        pageIdx++;
        InterlockedIncrement(&p.completed);
    }
    if (html) out.Append(StrL("</body></html>"));
    if (json) out.Append(StrL("]}"));
    if (rtf) out.AppendChar('}');
    return true;
}

static bool WriteSvg(ExportDoc& d, Str destination, ConversionProgress& p) {
    // SVG pages are bundled to preserve one atomic destination and avoid overwriting sibling files.
    ZipCreator zip(destination);
    bool ok = true;
    for (auto* page : d.pages) {
        if (Cancelled(p)) return false;
        fz_buffer* buffer = nullptr;
        fz_output* output = nullptr;
        fz_device* dev = nullptr;
        fz_var(buffer);
        fz_var(output);
        fz_var(dev);
        fz_try(d.ctx) {
            buffer = fz_new_buffer(d.ctx, 4096);
            output = fz_new_output_with_buffer(d.ctx, buffer);
            fz_rect box = page->text->mediabox;
            dev = fz_new_svg_device(d.ctx, output, box.x1 - box.x0, box.y1 - box.y0, FZ_SVG_TEXT_AS_PATH, 1);
            fz_run_page(d.ctx, page->page, dev, fz_translate(-box.x0, -box.y0), nullptr);
            fz_close_device(d.ctx, dev);
            fz_close_output(d.ctx, output);
            unsigned char* data;
            size_t n = fz_buffer_storage(d.ctx, buffer, &data);
            ok = n <= INT_MAX && zip.AddFileData(fmt("page-%d.svg", page->number), Str((char*)data, (int)n)) && ok;
        }
        fz_always(d.ctx) {
            fz_drop_device(d.ctx, dev);
            fz_drop_output(d.ctx, output);
            fz_drop_buffer(d.ctx, buffer);
        }
        fz_catch(d.ctx) {
            return Fail(p, Str(fz_caught_message(d.ctx)));
        }
        InterlockedIncrement(&p.completed);
    }
    return ok && zip.Finish();
}

bool ExportPdf(Str source, Str destination, ExportFormat format, Str pages, ConversionProgress& progress) {
    if (path::IsSame(source, destination))
        return Fail(progress, StrL("Choose a destination different from the source."));
    ExportDoc doc;
    if (!LoadExport(doc, source, pages, progress)) return false;
    file::AtomicFile output(destination);
    if (!len(output.tempPath)) return Fail(progress, StrL("Cannot create destination file."));
    bool ok = false;
    fz_var(ok);
    fz_try(doc.ctx) {
        if (format == ExportFormat::Svg)
            ok = WriteSvg(doc, output.tempPath, progress);
        else if (format == ExportFormat::Docx || format == ExportFormat::Pptx || format == ExportFormat::Xlsx) {
            ZipCreator zip(output.tempPath);
            if (format == ExportFormat::Docx)
                ok = WriteDocx(doc, zip, progress);
            else if (format == ExportFormat::Pptx)
                ok = WritePptx(doc, zip, progress);
            else
                ok = WriteXlsx(doc, zip, progress);
            ok = ok && zip.Finish();
        } else {
            str::Builder text;
            ok = WriteTextFormats(doc, format, text, progress) && file::WriteFile(output.tempPath, ToStr(text));
        }
        CheckCancel(doc.ctx, progress);
    }
    fz_catch(doc.ctx) {
        return Fail(progress, Str(fz_caught_message(doc.ctx)));
    }
    if (!ok || !output.Commit()) return Fail(progress, StrL("Could not write the exported file."));
    return true;
}

struct OfficeValue {
    VARIANT value;
    OfficeValue() { VariantInit(&value); }
    ~OfficeValue() { VariantClear(&value); }
    IDispatch* Dispatch() { return value.vt == VT_DISPATCH ? value.pdispVal : nullptr; }
};
static VARIANT IntArg(long value) {
    VARIANT v{};
    v.vt = VT_I4;
    v.lVal = value;
    return v;
}
static VARIANT BoolArg(bool value) {
    VARIANT v{};
    v.vt = VT_BOOL;
    v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    return v;
}
static VARIANT StringArg(Str value) {
    VARIANT v{};
    v.vt = VT_BSTR;
    TempWStr w = ToWStrTemp(value);
    v.bstrVal = SysAllocStringLen(w.s, len(w));
    return v;
}

static HRESULT OfficeCall(IDispatch* object, const WCHAR* method, WORD flags, OfficeValue* result, int count = 0,
                          VARIANT* args = nullptr, const WCHAR** names = nullptr) {
    if (!object || count > 8) return E_INVALIDARG;
    LPOLESTR lookup[9]{};
    DISPID ids[9]{};
    lookup[0] = (LPOLESTR)method;
    for (int i = 0; i < count && names; i++) lookup[i + 1] = (LPOLESTR)names[i];
    HRESULT hr = object->GetIDsOfNames(IID_NULL, lookup, names ? count + 1 : 1, LOCALE_USER_DEFAULT, ids);
    if (FAILED(hr)) return hr;
    VARIANT reverse[8]{};
    DISPID named[8]{};
    for (int i = 0; i < count; i++) {
        reverse[i] = args[count - i - 1];
        named[i] = ids[count - i];
    }
    DISPID put = DISPID_PROPERTYPUT;
    DISPPARAMS params{reverse, names ? named : nullptr, (UINT)count, names ? (UINT)count : 0};
    if (flags & DISPATCH_PROPERTYPUT) {
        params.rgdispidNamedArgs = &put;
        params.cNamedArgs = 1;
    }
    EXCEPINFO info{};
    UINT argError = 0;
    hr = object->Invoke(ids[0], IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result ? &result->value : nullptr, &info,
                        &argError);
    if (info.pfnDeferredFillIn) info.pfnDeferredFillIn(&info);
    SysFreeString(info.bstrSource);
    SysFreeString(info.bstrDescription);
    SysFreeString(info.bstrHelpFile);
    return hr;
}
static HRESULT OfficeSet(IDispatch* object, const WCHAR* name, VARIANT value) {
    return OfficeCall(object, name, DISPATCH_PROPERTYPUT, nullptr, 1, &value);
}

static int OfficeKind(Str source) {
    Str ext = path::GetExtTemp(source);
    if (str::EqI(ext, StrL(".doc")) || str::EqI(ext, StrL(".docx")) || str::EqI(ext, StrL(".odt"))) return 1;
    if (str::EqI(ext, StrL(".xls")) || str::EqI(ext, StrL(".xlsx")) || str::EqI(ext, StrL(".ods"))) return 2;
    if (str::EqI(ext, StrL(".ppt")) || str::EqI(ext, StrL(".pptx")) || str::EqI(ext, StrL(".odp"))) return 3;
    return 0;
}

static bool OfficeComPdf(Str source, Str destination, int kind, bool& installed, ConversionProgress& p) {
    const WCHAR* prog = kind == 1 ? L"Word.Application" : kind == 2 ? L"Excel.Application" : L"PowerPoint.Application";
    CLSID clsid;
    installed = false;
    HRESULT hr = CLSIDFromProgID(prog, &clsid);
    if (FAILED(hr)) return false;
    IDispatch* app = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&app);
    if (FAILED(hr)) return false;
    installed = true;
    OfficeValue collection, document;
    // Disable document macros before opening any untrusted input.
    hr = OfficeSet(app, L"AutomationSecurity", IntArg(3));
    if (SUCCEEDED(hr)) hr = OfficeSet(app, L"DisplayAlerts", IntArg(kind == 3 ? 1 : 0));
    if (kind != 3) OfficeSet(app, L"Visible", BoolArg(false));
    if (SUCCEEDED(hr))
        hr = OfficeCall(app,
                        kind == 1   ? L"Documents"
                        : kind == 2 ? L"Workbooks"
                                    : L"Presentations",
                        DISPATCH_PROPERTYGET, &collection);
    if (SUCCEEDED(hr) && !Cancelled(p)) {
        VARIANT args[4] = {StringArg(source), BoolArg(true), BoolArg(false), BoolArg(false)};
        const WCHAR* wordNames[] = {L"FileName", L"ReadOnly", L"AddToRecentFiles", L"ConfirmConversions"};
        const WCHAR* excelNames[] = {L"Filename", L"ReadOnly", L"UpdateLinks", L"IgnoreReadOnlyRecommended"};
        const WCHAR* pptNames[] = {L"FileName", L"ReadOnly", L"Untitled", L"WithWindow"};
        if (kind == 2) {
            args[2] = IntArg(0);
            args[3] = BoolArg(true);
        }
        hr = OfficeCall(collection.Dispatch(), L"Open", DISPATCH_METHOD, &document, 4, args,
                        kind == 1   ? wordNames
                        : kind == 2 ? excelNames
                                    : pptNames);
        VariantClear(&args[0]);
    }
    if (SUCCEEDED(hr) && document.Dispatch() && !Cancelled(p)) {
        VARIANT args[2] = {StringArg(destination), IntArg(kind == 1 ? 17 : kind == 2 ? 0 : 2)};
        const WCHAR* wordNames[] = {L"OutputFileName", L"ExportFormat"};
        const WCHAR* excelNames[] = {L"Filename", L"Type"};
        const WCHAR* pptNames[] = {L"Path", L"FixedFormatType"};
        hr = OfficeCall(document.Dispatch(), L"ExportAsFixedFormat", DISPATCH_METHOD, nullptr, 2, args,
                        kind == 1   ? wordNames
                        : kind == 2 ? excelNames
                                    : pptNames);
        VariantClear(&args[0]);
    }
    if (document.Dispatch()) {
        VARIANT save = BoolArg(false);
        OfficeCall(document.Dispatch(), L"Close", DISPATCH_METHOD, nullptr, kind == 3 ? 0 : 1, &save);
    }
    if (kind == 1) {
        VARIANT save = IntArg(0);
        OfficeCall(app, L"Quit", DISPATCH_METHOD, nullptr, 1, &save);
    } else
        OfficeCall(app, L"Quit", DISPATCH_METHOD, nullptr);
    app->Release();
    if (Cancelled(p)) return Fail(p, StrL("Conversion cancelled."));
    if (FAILED(hr) || !file::Exists(destination))
        return Fail(p,
                    fmt("Microsoft Office could not export this file (0x%08x). Close any Office prompts and try again.",
                        (unsigned)hr));
    return true;
}

struct ConversionTemp {
    Str folder;
    ConversionTemp() {
        WCHAR base[MAX_PATH], id[40];
        GUID guid;
        if (!GetTempPathW(dimof(base), base) || FAILED(CoCreateGuid(&guid))) return;
        StringFromGUID2(guid, id, dimof(id));
        Str path = path::Join(ToUtf8Temp(base), fmt("Swiftleaf-convert-%s", ToUtf8Temp(id)));
        if (CreateDirectoryW(ToWStrTemp(path).s, nullptr))
            folder = path;
        else
            str::Free(path);
    }
    ~ConversionTemp() {
        if (len(folder)) dir::RemoveAll(folder);
        str::Free(folder);
    }
};
static Str FindSoffice() {
    WCHAR found[MAX_PATH];
    DWORD n = SearchPathW(nullptr, L"soffice.exe", nullptr, dimof(found), found, nullptr);
    if (n && n < dimof(found)) return str::Dup(ToUtf8Temp(found));
    const WCHAR* vars[] = {L"ProgramFiles", L"ProgramFiles(x86)"};
    for (auto var : vars) {
        WCHAR root[MAX_PATH];
        n = GetEnvironmentVariableW(var, root, dimof(root));
        if (!n || n >= dimof(root)) continue;
        Str path = path::Join(ToUtf8Temp(root), StrL("LibreOffice\\program\\soffice.exe"));
        if (file::Exists(path)) return path;
        str::Free(path);
    }
    return {};
}
static bool LibreOfficePdf(Str source, Str destination, ConversionProgress& p) {
    Str exe = FindSoffice();
    if (!len(exe))
        return Fail(p, StrL("Install Microsoft Word, Excel or PowerPoint for this file type, or install LibreOffice to "
                            "create a PDF."));
    ConversionTemp tmp;
    if (!len(tmp.folder)) {
        str::Free(exe);
        return Fail(p, StrL("Cannot create a temporary conversion folder."));
    }
    WCHAR uri[4096];
    DWORD uriLen = dimof(uri);
    Str profile = path::JoinTemp(tmp.folder, StrL("profile"));
    HRESULT hr = UrlCreateFromPathW(ToWStrTemp(profile).s, uri, &uriLen, 0);
    if (FAILED(hr)) {
        str::Free(exe);
        return Fail(p, StrL("Cannot create LibreOffice profile path."));
    }
    TempStr cmd =
        fmt("\"%s\" \"-env:UserInstallation=%s\" --headless --nologo --nodefault --nofirststartwizard --convert-to pdf "
            "--outdir \"%s\" \"%s\"",
            exe, ToUtf8Temp(uri), tmp.folder, source);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    bool configured = job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    bool started = configured && CreateProcessW(ToWStrTemp(exe).s, ToWStrTemp(cmd).s, nullptr, nullptr, FALSE,
                                                CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    str::Free(exe);
    if (!started) {
        if (job) CloseHandle(job);
        return Fail(p, StrL("Could not start LibreOffice."));
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        return Fail(p, StrL("Could not isolate the LibreOffice conversion process."));
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    ULONGLONG start = GetTickCount64();
    bool timeout = false;
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if (Cancelled(p) || GetTickCount64() - start > 300000) {
            timeout = true;
            TerminateJobObject(job, 1);
            break;
        }
    }
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    if (Cancelled(p)) return Fail(p, StrL("Conversion cancelled."));
    if (timeout) return Fail(p, StrL("LibreOffice conversion timed out."));
    Str output = path::JoinTemp(tmp.folder, fmt("%s.pdf", path::GetPathNoExtTemp(path::GetBaseNameTemp(source))));
    if (code != 0 || !file::Exists(output) || !CopyFileW(ToWStrTemp(output).s, ToWStrTemp(destination).s, FALSE))
        return Fail(p, StrL("LibreOffice did not produce a PDF for this file."));
    return true;
}

struct RtfStream {
    Str text;
    int offset = 0;
};
static DWORD CALLBACK ReadRtf(DWORD_PTR cookie, LPBYTE buffer, LONG capacity, LONG* read) {
    auto* input = (RtfStream*)cookie;
    *read = std::min((int)capacity, len(input->text) - input->offset);
    memcpy(buffer, input->text.s + input->offset, *read);
    input->offset += *read;
    return 0;
}
static Str RtfPlainText(Str input) {
    HMODULE rich = LoadLibraryW(L"Msftedit.dll");
    if (!rich) return {};
    HWND edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"", ES_MULTILINE, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    Str result;
    if (edit) {
        RtfStream data{input};
        EDITSTREAM stream{(DWORD_PTR)&data, 0, ReadRtf};
        SendMessageW(edit, EM_EXLIMITTEXT, 0, 32 * 1024 * 1024);
        SendMessageW(edit, EM_STREAMIN, SF_RTF, (LPARAM)&stream);
        if (!stream.dwError) {
            int n = GetWindowTextLengthW(edit);
            WCHAR* text = AllocArray<WCHAR>(n + 1);
            GetWindowTextW(edit, text, n + 1);
            result = str::Dup(ToUtf8Temp(WStr(text, n)));
            free(text);
        }
        DestroyWindow(edit);
    }
    FreeLibrary(rich);
    return result;
}
static Str SourceHtml(Str source) {
    Str input = file::ReadFile(source);
    if (!input.s) return {};
    Str ext = path::GetExtTemp(source), result;
    if (str::EqI(ext, StrL(".html")) || str::EqI(ext, StrL(".htm"))) return input;
    if (str::EqI(ext, StrL(".md")) || str::EqI(ext, StrL(".markdown")))
        result = MarkdownToHtmlPage(input);
    else {
        Str text = input;
        if (str::EqI(ext, StrL(".rtf")))
            text = RtfPlainText(input);
        else if (len(input) >= 2 && (u8)input.s[0] == 0xff && (u8)input.s[1] == 0xfe)
            text = str::Dup(ToUtf8Temp(WStr((WCHAR*)(input.s + 2), (len(input) - 2) / 2)));
        str::Builder html;
        html.Append(StrL("<!doctype html><meta charset=\"utf-8\"><pre style=\"white-space:pre-wrap\">"));
        Xml(html, text);
        html.Append(StrL("</pre>"));
        result = html.TakeStr();
        if (text.s != input.s) str::Free(text);
    }
    str::Free(input);
    return result;
}
static bool IsStoryFile(Str path) {
    Str ext = path::GetExtTemp(path);
    return str::EqI(ext, StrL(".txt")) || str::EqI(ext, StrL(".rtf")) || str::EqI(ext, StrL(".html")) ||
           str::EqI(ext, StrL(".htm")) || str::EqI(ext, StrL(".md")) || str::EqI(ext, StrL(".markdown"));
}
static bool StoryPdf(Str source, Str destination, ConversionProgress& p) {
    Str html = SourceHtml(source);
    if (!html.s) return Fail(p, StrL("Cannot read the source text."));
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        str::Free(html);
        return false;
    }
    fz_buffer* buffer = nullptr;
    fz_story* story = nullptr;
    fz_document_writer* writer = nullptr;
    fz_archive* archive = nullptr;
    bool ok = false;
    fz_var(buffer);
    fz_var(story);
    fz_var(writer);
    fz_var(archive);
    fz_var(ok);
    fz_try(ctx) {
        buffer = fz_new_buffer_from_copied_data(ctx, (unsigned char*)html.s, len(html));
        archive = fz_open_directory(ctx, CStrTemp(path::GetDirTemp(source)));
        story = fz_new_story(ctx, buffer,
                             "body { font-family: serif; } img { max-width: 100%; } pre { white-space: pre-wrap; }", 12,
                             archive);
        writer = fz_new_pdf_writer(ctx, CStrTemp(destination), "compress");
        int more = 1, count = 0;
        while (more) {
            CheckCancel(ctx, p);
            if (++count > 10000) fz_throw(ctx, FZ_ERROR_LIMIT, "Text requires too many pages or cannot fit the page.");
            fz_rect filled;
            more = fz_place_story_flags(ctx, story, fz_make_rect(36, 36, 576, 756), &filled,
                                        FZ_PLACE_STORY_FLAG_NO_OVERFLOW);
            if (more == FZ_PLACE_STORY_RETURN_OVERFLOW_WIDTH)
                fz_throw(ctx, FZ_ERROR_FORMAT, "Content is too wide for a page.");
            fz_device* dev = fz_begin_page(ctx, writer, fz_make_rect(0, 0, 612, 792));
            fz_draw_story(ctx, story, dev, fz_identity);
            fz_end_page(ctx, writer);
        }
        fz_close_document_writer(ctx, writer);
        ok = true;
    }
    fz_always(ctx) {
        fz_drop_document_writer(ctx, writer);
        fz_drop_story(ctx, story);
        fz_drop_archive(ctx, archive);
        fz_drop_buffer(ctx, buffer);
    }
    fz_catch(ctx) {
        Fail(p, Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    str::Free(html);
    return ok;
}

bool CreatePdfFiles(const StrVec& sources, Str destination, Str range, ConversionProgress& p) {
    if (!len(sources)) return Fail(p, StrL("Choose at least one source file."));
    for (Str source : sources)
        if (path::IsSame(source, destination))
            return Fail(p, StrL("Choose a destination different from every source."));
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ConversionTemp temp;
    if (!len(temp.folder)) {
        if (SUCCEEDED(com)) CoUninitialize();
        return Fail(p, StrL("Cannot create temporary folder."));
    }
    StrVec inputs;
    bool ok = true;
    InterlockedExchange(&p.total, len(sources));
    int index = 0;
    for (Str source : sources) {
        if (Cancelled(p)) {
            ok = Fail(p, StrL("Conversion cancelled."));
            break;
        }
        Str converted = path::JoinTemp(temp.folder, fmt("input-%d.pdf", index++));
        if (IsStoryFile(source))
            ok = StoryPdf(source, converted, p);
        else if (int kind = OfficeKind(source)) {
            bool installed = false;
            if (FAILED(com))
                ok = Fail(p, StrL("Could not initialize Office automation."));
            else {
                ok = OfficeComPdf(source, converted, kind, installed, p);
                if (!installed) ok = LibreOfficePdf(source, converted, p);
            }
        } else
            converted = source;
        if (!ok) break;
        inputs.Append(converted);
        InterlockedIncrement(&p.completed);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    if (!ok) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return Fail(p, StrL("Cannot allocate PDF context."));
    file::AtomicFile output(destination);
    fz_document_writer* writer = nullptr;
    fz_document* doc = nullptr;
    fz_page* page = nullptr;
    Vec<int> selected;
    fz_var(writer);
    fz_var(doc);
    fz_var(page);
    fz_var(ok);
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        writer = fz_new_pdf_writer(ctx, CStrTemp(output.tempPath), "compress");
        for (Str input : inputs) {
            CheckCancel(ctx, p);
            doc = fz_open_document(ctx, CStrTemp(input));
            if (fz_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "Unlock password-protected inputs first.");
            VecReset(selected);
            if (!PageRange(range, fz_count_pages(ctx, doc), selected))
                fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page range for one of the input files.");
            for (int n : selected) {
                CheckCancel(ctx, p);
                page = fz_load_page(ctx, doc, n - 1);
                fz_rect box = fz_bound_page(ctx, page);
                fz_device* dev = fz_begin_page(ctx, writer, box);
                fz_run_page(ctx, page, dev, fz_identity, nullptr);
                fz_end_page(ctx, writer);
                fz_drop_page(ctx, page);
                page = nullptr;
            }
            fz_drop_document(ctx, doc);
            doc = nullptr;
        }
        fz_close_document_writer(ctx, writer);
        CheckCancel(ctx, p);
    }
    fz_always(ctx) {
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, doc);
        fz_drop_document_writer(ctx, writer);
    }
    fz_catch(ctx) {
        ok = Fail(p, Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    if (ok && !output.Commit()) ok = Fail(p, StrL("Could not save the PDF."));
    return ok;
}

bool CreateClipboardPdf(HWND, Str destination, ConversionProgress& p) {
    Pixmap* image = GetClipboardImageAsPixmap();
    if (!image) return Fail(p, StrL("The clipboard does not contain an image."));
    Str bmp = PixmapToBmpFormat(image);
    FreePixmap(image);
    ConversionTemp tmp;
    Str source = path::JoinTemp(tmp.folder, StrL("clipboard.bmp"));
    bool ok = len(tmp.folder) && file::WriteFile(source, bmp);
    str::Free(bmp);
    if (!ok) return Fail(p, StrL("Cannot read the clipboard image."));
    StrVec inputs;
    inputs.Append(source);
    return CreatePdfFiles(inputs, destination, {}, p);
}

#if IS_DEBUG
#include "base/tests/UtAssert.h"

static bool CheckExportPart(Str path, Str part, Str needle, Str forbidden = {}) {
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    fz_archive* zip = nullptr;
    fz_buffer* data = nullptr;
    fz_xml* xml = nullptr;
    bool ok = false;
    fz_var(zip);
    fz_var(data);
    fz_var(xml);
    fz_var(ok);
    fz_try(ctx) {
        zip = fz_open_archive(ctx, CStrTemp(path));
        // Parse every XML part, not just the part containing the asserted text.
        int count = fz_count_archive_entries(ctx, zip);
        for (int i = 0; i < count; i++) {
            Str name(fz_list_archive_entry(ctx, zip, i));
            if (!str::EndsWith(name, StrL(".xml")) && !str::EndsWith(name, StrL(".rels"))) continue;
            data = fz_read_archive_entry(ctx, zip, CStrTemp(name));
            xml = fz_parse_xml(ctx, data, 0);
            fz_drop_xml(ctx, xml);
            xml = nullptr;
            fz_drop_buffer(ctx, data);
            data = nullptr;
        }
        data = fz_read_archive_entry(ctx, zip, CStrTemp(part));
        unsigned char* bytes;
        size_t n = fz_buffer_storage(ctx, data, &bytes);
        Str text((char*)bytes, (int)n);
        ok = str::Contains(text, needle) && (!len(forbidden) || !str::Contains(text, forbidden));
    }
    fz_always(ctx) {
        fz_drop_xml(ctx, xml);
        fz_drop_buffer(ctx, data);
        fz_drop_archive(ctx, zip);
    }
    fz_catch(ctx) {
        ok = false;
    }
    fz_drop_context(ctx);
    return ok;
}

void ExportFormatsUnitTests() {
    Str source = StrL("docs/test/export-fixture.pdf");
    utassert(file::Exists(source));
    if (!file::Exists(source)) return;
    ConversionTemp tmp;
    utassert(len(tmp.folder) > 0);
    if (!len(tmp.folder)) return;
    const char* extensions[] = {"docx", "pptx", "xlsx", "html", "rtf", "md", "txt", "json", "zip"};
    for (int i = 0; i < dimof(extensions); i++) {
        ConversionProgress p;
        Str dest = path::JoinTemp(tmp.folder, fmt("output.%s", Str(extensions[i])));
        utassert(ExportPdf(source, dest, (ExportFormat)i, StrL("1"), p));
        utassert(file::GetSize(dest) > 0);
        if (i == 0) {
            utassert(CheckExportPart(dest, StrL("word/document.xml"), StrL("Hello &amp; &lt;world&gt;"),
                                     StrL("Second page only")));
            utassert(CheckExportPart(dest, StrL("word/document.xml"), StrL("<w:tbl>")));
            utassert(CheckExportPart(dest, StrL("word/document.xml"), StrL("Apple")));
            utassert(
                CheckExportPart(dest, StrL("word/_rels/document.xml.rels"), StrL("https://example.com/?a=1&amp;b=2")));
            utassert(CheckExportPart(dest, StrL("word/document.xml"), StrL("<w:drawing>")));
        } else if (i == 1) {
            utassert(
                CheckExportPart(dest, StrL("ppt/slides/slide1.xml"), StrL("Export fixture"), StrL("Second page only")));
            utassert(CheckExportPart(dest, StrL("ppt/slides/slide1.xml"), StrL("<p:pic>")));
        } else if (i == 2) {
            utassert(CheckExportPart(dest, StrL("xl/worksheets/sheet1.xml"), StrL("Apple"), StrL("<f>")));
            utassert(CheckExportPart(dest, StrL("xl/worksheets/sheet1.xml"), StrL("=1+1")));
            utassert(CheckExportPart(dest, StrL("xl/worksheets/sheet1.xml"), StrL("r=\"B2\"")));
        } else if (i == 8) {
            utassert(CheckExportPart(dest, StrL("page-1.svg"), StrL("<svg")));
        } else {
            Str text = file::ReadFile(dest);
            utassert(str::Contains(text, StrL("Export fixture")));
            utassert(!str::Contains(text, StrL("Second page only")));
            if (i == 7) {
                utassert(str::Contains(text, StrL("\"bbox\"")));
                utassert(str::Contains(text, StrL("\"characters\"")));
            }
            str::Free(text);
        }
    }
    Str dest = path::JoinTemp(tmp.folder, StrL("protected.txt"));
    utassert(file::WriteFile(dest, StrL("keep")));
    {
        ConversionProgress p;
        utassert(!ExportPdf(source, dest, ExportFormat::Text, StrL("0,9999"), p));
    }
    {
        ConversionProgress p;
        p.cancelled = 1;
        utassert(!ExportPdf(source, dest, ExportFormat::Text, {}, p));
    }
    Str old = file::ReadFile(dest);
    utassert(str::Eq(old, StrL("keep")));
    str::Free(old);
    {
        ConversionProgress p;
        utassert(ExportPdf(source, dest, ExportFormat::Text, StrL("2"), p));
        Str text = file::ReadFile(dest);
        utassert(str::Contains(text, StrL("Second page only")));
        utassert(!str::Contains(text, StrL("Export fixture")));
        str::Free(text);
    }
    {
        ConversionProgress p;
        Str sample = StrL("docs/test/swiftleaf-sample.pdf");
        utassert(file::Exists(sample));
        utassert(ExportPdf(sample, path::JoinTemp(tmp.folder, StrL("sample.docx")), ExportFormat::Docx, StrL("1"), p));
    }
    {
        Str html = path::JoinTemp(tmp.folder, StrL("source.html"));
        Str md = path::JoinTemp(tmp.folder, StrL("source.md"));
        utassert(file::WriteFile(html, StrL("<h1>Created HTML</h1><p>Alpha &amp; beta</p>")));
        utassert(file::WriteFile(md, StrL("# Created Markdown\n\nA paragraph.")));
        StrVec files;
        files.Append(html);
        files.Append(md);
        ConversionProgress p;
        Str pdf = path::JoinTemp(tmp.folder, StrL("combined.pdf"));
        utassert(CreatePdfFiles(files, pdf, StrL("all"), p));
        ConversionProgress q;
        utassert(ExportPdf(pdf, dest, ExportFormat::Text, {}, q));
        Str text = file::ReadFile(dest);
        utassert(str::Contains(text, StrL("Created HTML")));
        utassert(str::Contains(text, StrL("Created Markdown")));
        str::Free(text);
    }
    printf("Export formats: packages, tables, links, images, ranges, atomic failures and creation checked\n");
}
#endif
