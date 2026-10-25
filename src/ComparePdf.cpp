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

static bool PixelChange(fz_context* ctx, fz_document* a, fz_document* b, int pageNo, double* ratioOut) {
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
            }
        }
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
        int wc = abs(aw - bw);
        if (wc == 0 && !str::Eq(a, b)) wc = 1;
        wordChanges += wc;
        double pixelRatio = 0;
        PixelChange(ctx, left, right, i, &pixelRatio);
        pixelChanges += pixelRatio;
        html.Append(
            fmt("<section class=\"page\" id=\"page-%d\"><h2>Page %d</h2><p>%d vs %d words; %.2f%% changed "
                "pixels</p><h3>Text (left)</h3><pre>",
                i + 1, i + 1, aw, bw, pixelRatio * 100.0));
        AppendHtml(html, a);
        html.Append(StrL("</pre><h3>Text (right)</h3><pre>"));
        AppendHtml(html, b);
        html.Append(StrL("</pre></section>"));
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
