/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/JsonParser.h"
#include <mupdf/pdf.h>
#include "ComparePdf.h"

static void AppendHtml(str::Builder& out, Str s) {
    for (int i = 0; i < len(s); i++) {
        switch (s.s[i]) {
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
                out.AppendChar(s.s[i]);
                break;
        }
    }
}

static TempStr PageText(fz_context* ctx, fz_document* doc, int pageNo) {
    fz_buffer* buf = nullptr;
    fz_stext_page* text = nullptr;
    fz_page* page = nullptr;
    TempStr result;
    fz_stext_options opts{};
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, pageNo);
        text = fz_new_stext_page_from_page(ctx, page, &opts);
        buf = fz_new_buffer_from_stext_page(ctx, text);
        unsigned char* data = nullptr;
        size_t size = fz_buffer_storage(ctx, buf, &data);
        result = str::DupTemp(Str((char*)data, (int)size));
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_buffer(ctx, buf);
    fz_drop_stext_page(ctx, text);
    fz_drop_page(ctx, page);
    return result;
}

static int CountWords(Str s) {
    int count = 0;
    bool inWord = false;
    for (int i = 0; i < len(s); i++) {
        bool ws = str::IsWs(s.s[i]);
        if (!ws && !inWord) count++;
        inWord = !ws;
    }
    return count;
}

static void SplitWords(Str s, Vec<Str>& words) {
    int start = 0;
    while (start < len(s)) {
        while (start < len(s) && str::IsWs(s.s[start])) start++;
        int end = start;
        while (end < len(s) && !str::IsWs(s.s[end])) end++;
        if (end > start) VecAppend(words, Str(s.s + start, end - start));
        start = end;
    }
}

static void AppendWords(str::Builder& html, const Vec<Str>& words, int first, int last) {
    for (int i = first; i < last; i++) {
        if (i > first) html.AppendChar(' ');
        AppendHtml(html, words[i]);
    }
}

static void AppendWordDiff(str::Builder& html, Str left, Str right) {
    Vec<Str> a, b;
    SplitWords(left, a);
    SplitWords(right, b);
    int prefix = 0;
    while (prefix < len(a) && prefix < len(b) && str::Eq(a[prefix], b[prefix])) prefix++;
    int suffix = 0;
    while (suffix < len(a) - prefix && suffix < len(b) - prefix &&
           str::Eq(a[len(a) - suffix - 1], b[len(b) - suffix - 1]))
        suffix++;
    AppendWords(html, a, 0, prefix);
    if (prefix) html.AppendChar(' ');
    if (prefix < len(a) - suffix) {
        html.Append(StrL("<del>"));
        AppendWords(html, a, prefix, len(a) - suffix);
        html.Append(StrL("</del> "));
    }
    if (prefix < len(b) - suffix) {
        html.Append(StrL("<ins>"));
        AppendWords(html, b, prefix, len(b) - suffix);
        html.Append(StrL("</ins> "));
    }
    AppendWords(html, a, len(a) - suffix, len(a));
}

static int CountWordChanges(Str left, Str right) {
    Vec<Str> a, b;
    SplitWords(left, a);
    SplitWords(right, b);
    int prefix = 0;
    while (prefix < len(a) && prefix < len(b) && str::Eq(a[prefix], b[prefix])) prefix++;
    int suffix = 0;
    while (suffix < len(a) - prefix && suffix < len(b) - prefix &&
           str::Eq(a[len(a) - suffix - 1], b[len(b) - suffix - 1]))
        suffix++;
    return len(a) + len(b) - 2 * (prefix + suffix);
}

static bool PixelChange(fz_context* ctx, fz_document* a, fz_document* b, int pageNo, double* ratioOut,
                        str::Builder& svg) {
    fz_page* pa = nullptr;
    fz_page* pb = nullptr;
    fz_pixmap* xa = nullptr;
    fz_pixmap* xb = nullptr;
    bool ok = false;
    fz_try(ctx) {
        pa = fz_load_page(ctx, a, pageNo);
        pb = fz_load_page(ctx, b, pageNo);
        xa = fz_new_pixmap_from_page(ctx, pa, fz_scale(0.25f, 0.25f), fz_device_rgb(ctx), 0);
        xb = fz_new_pixmap_from_page(ctx, pb, fz_scale(0.25f, 0.25f), fz_device_rgb(ctx), 0);
        int aw = xa->w, ah = xa->h, bw = xb->w, bh = xb->h;
        int w = std::max(aw, bw), h = std::max(ah, bh), changed = 0;
        const unsigned char* as = fz_pixmap_samples(ctx, xa);
        const unsigned char* bs = fz_pixmap_samples(ctx, xb);
        svg.Append(
            fmt("<svg role=\"img\" aria-label=\"Pixel difference on page %d\" viewBox=\"0 0 %d %d\" width=\"320\" "
                "style=\"background:#fff;border:1px solid #aaa\">",
                pageNo + 1, w, h));
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                bool different = x >= aw || y >= ah || x >= bw || y >= bh;
                if (!different) {
                    int ao = y * xa->stride + x * xa->n;
                    int bo = y * xb->stride + x * xb->n;
                    different = abs((int)as[ao] - (int)bs[bo]) > 24 || abs((int)as[ao + 1] - (int)bs[bo + 1]) > 24 ||
                                abs((int)as[ao + 2] - (int)bs[bo + 2]) > 24;
                }
                changed += different;
                if (different)
                    svg.Append(fmt("<rect x=\"%d\" y=\"%d\" width=\"1\" height=\"1\" fill=\"#e11d48\"/>", x, y));
            }
        }
        svg.Append(StrL("</svg>"));
        int total = std::max(1, w * h);
        *ratioOut = (double)changed / total;
        ok = true;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_pixmap(ctx, xa);
    fz_drop_pixmap(ctx, xb);
    fz_drop_page(ctx, pa);
    fz_drop_page(ctx, pb);
    return ok;
}

bool ComparePdfReport(Str leftPath, Str rightPath, Str reportPath, Str* resultJsonOut) {
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    fz_document* left = nullptr;
    fz_document* right = nullptr;
    int leftPages = 0, rightPages = 0;
    str::Builder html;
    fz_try(ctx) {
        left = fz_open_document(ctx, CStrTemp(leftPath));
        right = fz_open_document(ctx, CStrTemp(rightPath));
        leftPages = fz_count_pages(ctx, left);
        rightPages = fz_count_pages(ctx, right);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        fz_drop_document(ctx, left);
        fz_drop_document(ctx, right);
        fz_drop_context(ctx);
        return false;
    }

    html.Append(StrL("<!doctype html><meta charset=\"utf-8\"><title>PDF comparison</title>"));
    html.Append(
        StrL("<style>body{font:15px "
             "system-ui;margin:2rem;color:#222}pre{white-space:pre-wrap}del{background:#ffd8d8}ins{background:#d8ffe0}."
             "page{border-top:1px solid #ccc;padding:1rem 0}</style>"));
    html.Append(StrL("<h1>PDF comparison</h1><p>"));
    AppendHtml(html, leftPath);
    html.Append(StrL(" compared with "));
    AppendHtml(html, rightPath);
    html.Append(fmt("</p><p>%d pages vs %d pages</p>", leftPages, rightPages));
    int wordChanges = 0;
    double pixelChanges = 0;
    int compared = std::min(leftPages, rightPages);
    for (int i = 0; i < compared; i++) {
        TempStr a = PageText(ctx, left, i);
        TempStr b = PageText(ctx, right, i);
        int aw = CountWords(a), bw = CountWords(b);
        wordChanges += CountWordChanges(a, b);
        double pixelRatio = 0;
        str::Builder svg;
        PixelChange(ctx, left, right, i, &pixelRatio, svg);
        pixelChanges += pixelRatio;
        html.Append(
            fmt("<section class=\"page\" id=\"page-%d\"><h2>Page %d</h2><p>%d vs %d words; %.2f%% changed "
                "pixels</p><h3>Pixel difference heatmap</h3>",
                i + 1, i + 1, aw, bw, pixelRatio * 100.0));
        html.Append(ToStrTemp(svg));
        html.Append(StrL("<h3>Word changes</h3><p>"));
        AppendWordDiff(html, a, b);
        html.Append(StrL("</p></section>"));
    }
    if (leftPages != rightPages) wordChanges += abs(leftPages - rightPages);
    html.Append(StrL("<h2>Summary</h2><ul>"));
    for (int i = 0; i < compared; i++) html.Append(fmt("<li><a href=\"#page-%d\">Page %d</a></li>", i + 1, i + 1));
    html.Append(StrL("</ul>"));
    bool ok = file::WriteFile(reportPath, ToStrTemp(html));
    if (resultJsonOut) {
        *resultJsonOut = str::DupTemp(
            fmt("{\"ok\":%s,\"pages\":%d,\"wordChanges\":%d,\"pixelChangePercent\":%.2f,\"report\":\"%s\"}",
                ok ? StrL("true") : StrL("false"), compared, wordChanges,
                compared ? pixelChanges * 100.0 / compared : 0.0, json::EscapeStrTemp(reportPath)));
    }
    fz_drop_document(ctx, left);
    fz_drop_document(ctx, right);
    fz_drop_context(ctx);
    return ok;
}
