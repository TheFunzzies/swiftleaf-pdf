/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/JsonParser.h"
#include "base/Win.h"
#include <commdlg.h>
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "SumatraPDF.h"
#include "shared/AppTools.h"
#include "shared/Theme.h"
#include "DarkMode.h"
#include "Translations.h"
#include <mupdf/pdf.h>
#include "ComparePdf.h"

#include <algorithm>

// Bounded LCS table: at most ~1.5 million cells (e.g. 1200x1200 words). If a
// single page's changed span exceeds this, we fall back to a coarse diff
// instead of allocating unbounded O(N*M) memory.
static constexpr int kMaxDiffCells = 1 << 20;

// Maximum page pairs to compare with text similarity. 200x200 pages is 40,000
// comparisons; above that we keep index alignment.
static constexpr int kMaxAlignPairs = 20000;

// Similarity threshold (percent, 0..100) to consider two pages a match.
static constexpr int kPageMatchPercent = 40;

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

static void AppendBase64(str::Builder& out, const u8* data, size_t size) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < size; i += 3) {
        u32 value = (u32)data[i] << 16;
        if (i + 1 < size) {
            value |= (u32)data[i + 1] << 8;
        }
        if (i + 2 < size) {
            value |= data[i + 2];
        }
        out.AppendChar(table[(value >> 18) & 63]);
        out.AppendChar(table[(value >> 12) & 63]);
        out.AppendChar(i + 1 < size ? table[(value >> 6) & 63] : '=');
        out.AppendChar(i + 2 < size ? table[value & 63] : '=');
    }
}

static TempStr PixmapPngUri(fz_context* ctx, fz_pixmap* pixmap) {
    fz_buffer* buffer = fz_new_buffer_from_pixmap_as_png(ctx, pixmap, fz_default_color_params);
    unsigned char* data = nullptr;
    size_t size = fz_buffer_storage(ctx, buffer, &data);
    str::Builder uri;
    uri.Append(StrL("data:image/png;base64,"));
    AppendBase64(uri, (const u8*)data, size);
    fz_drop_buffer(ctx, buffer);
    return ToStrTemp(uri);
}

static bool PageText(fz_context* ctx, fz_document* doc, int pageNo, TempStr& outText) {
    fz_buffer* buf = nullptr;
    fz_stext_page* text = nullptr;
    fz_page* page = nullptr;
    bool ok = false;
    fz_var(buf);
    fz_var(text);
    fz_var(page);
    fz_var(ok);
    fz_stext_options opts{};
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, pageNo);
        text = fz_new_stext_page_from_page(ctx, page, &opts);
        buf = fz_new_buffer_from_stext_page(ctx, text);
        unsigned char* data = nullptr;
        size_t size = fz_buffer_storage(ctx, buf, &data);
        if (size > 16 * 1024 * 1024) fz_throw(ctx, FZ_ERROR_LIMIT, "Page text exceeds comparison limit");
        outText = str::DupTemp(Str((char*)data, (int)size));
        ok = true;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        ok = false;
    }
    fz_drop_buffer(ctx, buf);
    fz_drop_stext_page(ctx, text);
    fz_drop_page(ctx, page);
    return ok;
}

static bool RenderPagePng(fz_context* ctx, fz_document* doc, int pageNo, TempStr& outPng) {
    fz_page* page = nullptr;
    fz_pixmap* pix = nullptr;
    bool ok = false;
    fz_var(page);
    fz_var(pix);
    fz_var(ok);
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, pageNo);
        fz_rect r = fz_bound_page(ctx, page);
        float side = std::max(r.x1 - r.x0, r.y1 - r.y0);
        if (!isfinite(side) || side <= 0) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page dimensions");
        float zoom = std::min(0.25f, 1024.0f / side);
        pix = fz_new_pixmap_from_page(ctx, page, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
        outPng = PixmapPngUri(ctx, pix);
        ok = true;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        ok = false;
    }
    fz_drop_pixmap(ctx, pix);
    fz_drop_page(ctx, page);
    return ok;
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
    int start = -1;
    for (int i = 0; i <= len(s); i++) {
        bool end = (i == len(s)) || str::IsWs(s.s[i]);
        if (!end && start < 0) {
            start = i;
        } else if (end && start >= 0) {
            VecAppend(words, Str(s.s + start, i - start));
            start = -1;
        }
    }
}

enum WordOp : u8 {
    WordSame,
    WordDel,
    WordIns
};

static void AppendWordSpan(str::Builder& out, const Vec<Str>& words, int from, int to, bool& first) {
    for (int i = from; i < to; i++) {
        if (!first) out.AppendChar(' ');
        first = false;
        AppendHtml(out, words[i]);
    }
}

// Emit one edit run: deleted words first (if any), then inserted words.
static void AppendWordRun(str::Builder& out, const Vec<Str>& del, int dfrom, int dto, const Vec<Str>& ins, int ifrom,
                          int ito, bool& first) {
    if (dto > dfrom) {
        if (!first) out.AppendChar(' ');
        first = false;
        out.Append(StrL("<del>"));
        for (int i = dfrom; i < dto; i++) {
            if (i > dfrom) out.AppendChar(' ');
            AppendHtml(out, del[i]);
        }
        out.Append(StrL("</del>"));
    }
    if (ito > ifrom) {
        if (!first) out.AppendChar(' ');
        first = false;
        out.Append(StrL("<ins>"));
        for (int i = ifrom; i < ito; i++) {
            if (i > ifrom) out.AppendChar(' ');
            AppendHtml(out, ins[i]);
        }
        out.Append(StrL("</ins>"));
    }
}

PdfWordDiff ComparePdfWordDiff(Str leftText, Str rightText) {
    PdfWordDiff res;
    Vec<Str> a, b;
    SplitWords(leftText, a);
    SplitWords(rightText, b);

    // Trim the common prefix/suffix first: it shrinks the LCS table and keeps
    // unchanged text out of the edit script.
    int prefix = 0;
    while (prefix < len(a) && prefix < len(b) && str::Eq(a[prefix], b[prefix])) prefix++;
    int suffix = 0;
    while (suffix < len(a) - prefix && suffix < len(b) - prefix &&
           str::Eq(a[len(a) - suffix - 1], b[len(b) - suffix - 1])) {
        suffix++;
    }
    int an = len(a) - prefix - suffix;
    int bn = len(b) - prefix - suffix;

    str::Builder out;
    bool first = true;
    AppendWordSpan(out, a, 0, prefix, first);

    bool tooBig = (i64)an * bn > kMaxDiffCells;
    if (tooBig || an == 0 || bn == 0) {
        if (an > 0 || bn > 0) {
            AppendWordRun(out, a, prefix, prefix + an, b, prefix, prefix + bn, first);
            res.editRuns = 1;
            res.changedWords = std::max(an, bn);
        }
        res.coarse = tooBig;
    } else {
        // Bounded LCS table; traceback derives the edit script without a
        // second direction matrix.
        int cols = bn + 1;
        u32* dp = (u32*)AllocZero((an + 1) * cols, sizeof(u32));
        for (int i = 1; i <= an; i++) {
            for (int j = 1; j <= bn; j++) {
                if (str::Eq(a[prefix + i - 1], b[prefix + j - 1])) {
                    dp[i * cols + j] = dp[(i - 1) * cols + j - 1] + 1;
                } else {
                    dp[i * cols + j] = std::max(dp[(i - 1) * cols + j], dp[i * cols + j - 1]);
                }
            }
        }

        Vec<Str> words;
        Vec<u8> ops;
        int i = an, j = bn;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 && str::Eq(a[prefix + i - 1], b[prefix + j - 1])) {
                VecAppend(words, a[prefix + i - 1]);
                VecAppend(ops, (u8)WordSame);
                i--;
                j--;
            } else if (j > 0 && (i == 0 || dp[i * cols + j - 1] >= dp[(i - 1) * cols + j])) {
                VecAppend(words, b[prefix + j - 1]);
                VecAppend(ops, (u8)WordIns);
                j--;
            } else {
                VecAppend(words, a[prefix + i - 1]);
                VecAppend(ops, (u8)WordDel);
                i--;
            }
        }
        VecReverse(words);
        VecReverse(ops);
        free(dp);

        // Walk the script once: same words are printed, every maximal run of
        // del/ins words becomes one <del>..</del> <ins>..</ins> edit.
        for (int k = 0; k < len(ops);) {
            if (ops[k] == WordSame) {
                AppendWordSpan(out, words, k, k + 1, first);
                k++;
                continue;
            }
            Vec<Str> dels, inss;
            while (k < len(ops) && ops[k] != WordSame) {
                VecAppend(ops[k] == WordDel ? dels : inss, words[k]);
                k++;
            }
            AppendWordRun(out, dels, 0, len(dels), inss, 0, len(inss), first);
            res.editRuns++;
            res.changedWords += std::max(len(dels), len(inss));
        }
    }

    AppendWordSpan(out, a, len(a) - suffix, len(a), first);
    res.html = ToStrTemp(out);
    return res;
}

// --- page alignment -------------------------------------------------------
// Each page is reduced to sorted word hashes. Similarity is bag-of-words
// overlap, so an inserted or moved page no longer shifts every later index.
struct PageSigs {
    Vec<u32> hashes;
    Vec<int> offsets; // one past-end offset per page, starting with 0
};

static void StartSigs(PageSigs& sigs) {
    VecAppend(sigs.offsets, 0);
}

static int CmpU32(const void* x, const void* y) {
    u32 a = *(const u32*)x, b = *(const u32*)y;
    return (a > b) - (a < b);
}

static void AddPageSig(PageSigs& sigs, Str text) {
    Vec<Str> words;
    SplitWords(text, words);
    int start = len(sigs.hashes);
    for (Str w : words) VecAppend(sigs.hashes, MurmurHash2(w));
    int count = len(sigs.hashes) - start;
    if (count > 0) qsort((void*)(sigs.hashes.els + start), (size_t)count, sizeof(u32), CmpU32);
    VecAppend(sigs.offsets, len(sigs.hashes));
}

static int PageSim(const PageSigs& a, int ai, const PageSigs& b, int bi) {
    int ab = a.offsets[ai], ae = a.offsets[ai + 1];
    int bb = b.offsets[bi], be = b.offsets[bi + 1];
    int na = ae - ab, nb = be - bb;
    if (na + nb == 0) return 0;
    int i = ab, j = bb, common = 0;
    while (i < ae && j < be) {
        u32 ha = a.hashes[i], hb = b.hashes[j];
        if (ha == hb) {
            common++;
            i++;
            j++;
        } else if (ha < hb) {
            i++;
        } else {
            j++;
        }
    }
    return (200 * common) / (na + nb);
}

static void AddMatch(Vec<PdfPageMatch>& out, int left, int right, int similarity, bool moved) {
    PdfPageMatch m;
    m.left = left;
    m.right = right;
    m.similarity = similarity;
    m.moved = moved;
    VecAppend(out, m);
}

static void AlignPageSigs(const PageSigs& left, const PageSigs& right, Vec<PdfPageMatch>& out) {
    int nl = len(left.offsets) - 1, nr = len(right.offsets) - 1;
    if (nl <= 0 || nr <= 0) {
        for (int i = 0; i < nl; i++) AddMatch(out, i, -1, 0, false);
        for (int j = 0; j < nr; j++) AddMatch(out, -1, j, 0, false);
        return;
    }

    int* rightOfLeft = AllocArray<int>(nl);
    int* leftOfRight = AllocArray<int>(nr);
    u8* moved = AllocArray<u8>(nl);
    for (int i = 0; i < nl; i++) rightOfLeft[i] = -1;
    for (int j = 0; j < nr; j++) leftOfRight[j] = -1;

    // Above the pair cap keep the old index alignment; expensive but bounded.
    if ((i64)nl * nr <= kMaxAlignPairs) {
        u8* sim = (u8*)AllocZero(nl * nr, 1);
        for (int i = 0; i < nl; i++) {
            for (int j = 0; j < nr; j++) sim[i * nr + j] = (u8)PageSim(left, i, right, j);
        }

        // Phase 1: order-preserving LCS over sufficiently similar pages. This
        // matches the unchanged pages around an insertion or deletion.
        int cols = nr + 1;
        u16* dp = (u16*)AllocZero((nl + 1) * cols, sizeof(u16));
        for (int i = 1; i <= nl; i++) {
            for (int j = 1; j <= nr; j++) {
                if (sim[(i - 1) * nr + (j - 1)] >= kPageMatchPercent) {
                    dp[i * cols + j] = dp[(i - 1) * cols + j - 1] + 1;
                } else {
                    dp[i * cols + j] = std::max(dp[(i - 1) * cols + j], dp[i * cols + j - 1]);
                }
            }
        }
        int i = nl, j = nr;
        while (i > 0 && j > 0) {
            int s = sim[(i - 1) * nr + (j - 1)];
            if (s >= kPageMatchPercent && dp[i * cols + j] == dp[(i - 1) * cols + j - 1] + 1) {
                rightOfLeft[i - 1] = j - 1;
                leftOfRight[j - 1] = i - 1;
                i--;
                j--;
            } else if (dp[(i - 1) * cols + j] >= dp[i * cols + j - 1]) {
                i--;
            } else {
                j--;
            }
        }
        free(dp);

        // Phase 2: pages left over but textually the same are moves (index
        // order alone cannot explain them).
        for (int li = 0; li < nl; li++) {
            if (rightOfLeft[li] >= 0) continue;
            int best = -1, bestSim = kPageMatchPercent - 1;
            for (int ri = 0; ri < nr; ri++) {
                if (leftOfRight[ri] >= 0) continue;
                int s = sim[li * nr + ri];
                if (s > bestSim) {
                    bestSim = s;
                    best = ri;
                }
            }
            if (best < 0) continue;
            rightOfLeft[li] = best;
            leftOfRight[best] = li;
            moved[li] = 1;
        }
        free(sim);
    }

    // Phase 3: index fallback for the rest (scanned or fully rewritten pages
    // have no text signal; pairing them by index preserves side-by-side view).
    int ri = 0;
    for (int li = 0; li < nl; li++) {
        if (rightOfLeft[li] >= 0) continue;
        while (ri < nr && leftOfRight[ri] >= 0) ri++;
        if (ri >= nr) break;
        rightOfLeft[li] = ri;
        leftOfRight[ri] = li;
    }

    for (int li = 0; li < nl; li++) {
        int r = rightOfLeft[li];
        if (r >= 0) {
            AddMatch(out, li, r, PageSim(left, li, right, r), moved[li] != 0);
        } else {
            AddMatch(out, li, -1, 0, false);
        }
    }
    for (int r = 0; r < nr; r++) {
        if (leftOfRight[r] < 0) AddMatch(out, -1, r, 0, false);
    }

    free(rightOfLeft);
    free(leftOfRight);
    free(moved);
}

void ComparePdfAlignPages(const Vec<Str>& leftTexts, const Vec<Str>& rightTexts, Vec<PdfPageMatch>& out) {
    PageSigs ls, rs;
    StartSigs(ls);
    StartSigs(rs);
    for (Str t : leftTexts) AddPageSig(ls, t);
    for (Str t : rightTexts) AddPageSig(rs, t);
    AlignPageSigs(ls, rs, out);
}

static bool PixelChange(fz_context* ctx, fz_document* a, fz_document* b, int aPageNo, int bPageNo, double* ratioOut,
                        Str* leftPngOut, Str* rightPngOut) {
    fz_page* pa = nullptr;
    fz_page* pb = nullptr;
    fz_pixmap* xa = nullptr;
    fz_pixmap* xb = nullptr;
    bool ok = false;
    fz_var(pa);
    fz_var(pb);
    fz_var(xa);
    fz_var(xb);
    fz_var(ok);
    fz_try(ctx) {
        pa = fz_load_page(ctx, a, aPageNo);
        pb = fz_load_page(ctx, b, bPageNo);
        fz_rect ra = fz_bound_page(ctx, pa), rb = fz_bound_page(ctx, pb);
        float side = std::max(std::max(ra.x1 - ra.x0, ra.y1 - ra.y0), std::max(rb.x1 - rb.x0, rb.y1 - rb.y0));
        if (!isfinite(side) || side <= 0) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page dimensions");
        float zoom = std::min(0.25f, 1024.0f / side);
        xa = fz_new_pixmap_from_page(ctx, pa, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
        xb = fz_new_pixmap_from_page(ctx, pb, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
        *leftPngOut = PixmapPngUri(ctx, xa);
        *rightPngOut = PixmapPngUri(ctx, xb);
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

static bool WriteReportPart(fz_context* ctx, file::FileHandle file, fz_document_writer* writer, Str html,
                            PdfWorkProgress* progress) {
    if (!writer) return file::WriteAll(file, html);
    fz_buffer* buffer = nullptr;
    fz_story* story = nullptr;
    bool ok = false;
    fz_var(buffer);
    fz_var(story);
    fz_var(ok);
    fz_try(ctx) {
        buffer = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)html.s, len(html));
        story = fz_new_story(ctx, buffer, "body { font-size: 11pt; } img { max-width: 100%; }", 11, nullptr);
        int more = 1, pages = 0;
        while (more) {
            if (progress && progress->IsCancelled()) fz_throw(ctx, FZ_ERROR_ABORT, "Cancelled");
            if (++pages > 1000) fz_throw(ctx, FZ_ERROR_LIMIT, "Report section is too long");
            fz_rect filled;
            more = fz_place_story_flags(ctx, story, fz_make_rect(36, 36, 576, 756), &filled,
                                        FZ_PLACE_STORY_FLAG_NO_OVERFLOW);
            if (more == FZ_PLACE_STORY_RETURN_OVERFLOW_WIDTH)
                fz_throw(ctx, FZ_ERROR_LIMIT, "Report section does not fit");
            fz_device* dev = fz_begin_page(ctx, writer, fz_make_rect(0, 0, 612, 792));
            fz_draw_story(ctx, story, dev, fz_identity);
            fz_end_page(ctx, writer);
        }
        ok = true;
    }
    fz_always(ctx) {
        fz_drop_story(ctx, story);
        fz_drop_buffer(ctx, buffer);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return ok;
}

bool ComparePdfReport(Str leftPath, Str rightPath, Str reportPath, Str* resultJsonOut, PdfWorkProgress* progress) {
    if (resultJsonOut) *resultJsonOut = StrL("{\"ok\":false}");
    if (progress) {
        AtomicIntSet(&progress->completed, 0);
        AtomicIntSet(&progress->total, 0);
        if (progress->IsCancelled()) return false;
    }
    if (len(leftPath) == 0 || len(rightPath) == 0 || len(reportPath) == 0) {
        if (resultJsonOut) {
            *resultJsonOut = str::DupTemp(StrL("{\"ok\":false,\"error\":\"Invalid input or report path\"}"));
        }
        return false;
    }
    if (path::IsSame(leftPath, reportPath) || path::IsSame(rightPath, reportPath)) {
        if (resultJsonOut) {
            *resultJsonOut = str::DupTemp(StrL("{\"ok\":false,\"error\":\"Report path collides with input\"}"));
        }
        return false;
    }

    file::AtomicFile output(reportPath);
    if (len(output.tempPath) == 0) {
        if (resultJsonOut) {
            *resultJsonOut = str::DupTemp(StrL("{\"ok\":false,\"error\":\"Cannot create report destination\"}"));
        }
        return false;
    }

    constexpr size_t kCompareCacheBytes = 32 * 1024 * 1024;
    fz_context* ctx = fz_new_context(nullptr, nullptr, kCompareCacheBytes);
    if (!ctx) {
        if (resultJsonOut) {
            *resultJsonOut = str::DupTemp(StrL("{\"ok\":false,\"error\":\"Failed to initialize engine context\"}"));
        }
        return false;
    }
    fz_register_document_handlers(ctx);
    fz_document* left = nullptr;
    fz_document* right = nullptr;
    fz_document_writer* writer = nullptr;
    file::FileHandle file = file::kInvalidFileHandle;
    int leftPages = 0, rightPages = 0;
    bool ok = false;
    fz_var(left);
    fz_var(right);
    fz_var(writer);
    fz_var(leftPages);
    fz_var(rightPages);
    fz_var(ok);
    fz_try(ctx) {
        left = fz_open_document(ctx, CStrTemp(leftPath));
        right = fz_open_document(ctx, CStrTemp(rightPath));
        if (!left || !right) fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to open document");
        leftPages = fz_count_pages(ctx, left);
        rightPages = fz_count_pages(ctx, right);
        if (str::EndsWithI(reportPath, StrL(".pdf")))
            writer = fz_new_pdf_writer(ctx, CStrTemp(output.tempPath), "compress");
        ok = true;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        if (resultJsonOut) {
            *resultJsonOut = str::DupTemp(StrL("{\"ok\":false,\"error\":\"Failed to open comparison documents\"}"));
        }
        if (left) fz_drop_document(ctx, left);
        if (right) fz_drop_document(ctx, right);
        fz_drop_context(ctx);
        return false;
    }

    PageSigs leftSigs, rightSigs;
    StartSigs(leftSigs);
    StartSigs(rightSigs);
    for (int i = 0; i < leftPages; i++) {
        if (progress && progress->IsCancelled()) {
            ok = false;
            break;
        }
        TempStr t;
        AddPageSig(leftSigs, PageText(ctx, left, i, t) ? t : Str());
    }
    for (int i = 0; ok && i < rightPages; i++) {
        if (progress && progress->IsCancelled()) {
            ok = false;
            break;
        }
        TempStr t;
        AddPageSig(rightSigs, PageText(ctx, right, i, t) ? t : Str());
    }
    if (!ok) {
        if (writer) fz_drop_document_writer(ctx, writer);
        if (left) fz_drop_document(ctx, left);
        if (right) fz_drop_document(ctx, right);
        fz_drop_context(ctx);
        if (resultJsonOut) {
            *resultJsonOut = str::DupTemp(StrL("{\"ok\":false,\"error\":\"Cancelled\"}"));
        }
        return false;
    }

    Vec<PdfPageMatch> matches;
    AlignPageSigs(leftSigs, rightSigs, matches);

    if (progress) AtomicIntSet(&progress->total, len(matches));

    if (ok && !writer) {
        file = file::OpenReadWrite(output.tempPath, true);
        ok = file != file::kInvalidFileHandle;
    }

    str::Builder html;
    html.Append(StrL("<!doctype html><meta charset=\"utf-8\"><title>PDF comparison</title>"));
    html.Append(
        StrL("<style>body{font:15px "
             "system-ui;margin:2rem;color:#222}pre{white-space:pre-wrap}del{background:#ffd8d8}ins{background:#d8ffe0}."
             "error{color:#b00;font-weight:bold}"
             ".images{display:flex;gap:1rem;align-items:flex-start}.images figure{margin:0}.images "
             "img{max-width:48vw;border:1px solid #ccc}."
             "page{border-top:1px solid #ccc;padding:1rem 0}</style>"));
    html.Append(StrL("<h1>PDF comparison</h1><p>"));
    AppendHtml(html, leftPath);
    html.Append(StrL(" compared with "));
    AppendHtml(html, rightPath);
    html.Append(fmt("</p><p>%d pages vs %d pages</p>", leftPages, rightPages));
    if (ok) ok = WriteReportPart(ctx, file, writer, ToStr(html), progress);

    int wordChanges = 0;
    int editRuns = 0;
    int coarsePages = 0;
    int movedPages = 0;
    int addedPages = 0;
    int removedPages = 0;
    double pixelChanges = 0;
    int pixelPages = 0;
    int totalPages = std::max(leftPages, rightPages);

    for (int matchIdx = 0; ok && matchIdx < len(matches); matchIdx++) {
        AutoArenaSavepoint scratch;
        if (progress && progress->IsCancelled()) {
            ok = false;
            break;
        }
        const PdfPageMatch& m = matches[matchIdx];
        TempStr a, b;
        bool okA = m.left >= 0 && PageText(ctx, left, m.left, a);
        bool okB = m.right >= 0 && PageText(ctx, right, m.right, b);
        int aw = okA ? CountWords(a) : 0;
        int bw = okB ? CountWords(b) : 0;

        str::Builder section;
        if (m.left >= 0 && m.right >= 0) {
            str::Builder diff;
            int wc = 0, runs = 0;
            bool coarse = false;
            if (okA && okB) {
                PdfWordDiff d = ComparePdfWordDiff(a, b);
                diff.Append(d.html);
                wc = d.changedWords;
                runs = d.editRuns;
                coarse = d.coarse;
            } else {
                wc = std::max(1, std::max(aw, bw));
                diff.Append(StrL("<span class=\"error\">[Text extraction failed"));
                if (!okA) diff.Append(StrL(" on left"));
                if (!okB) diff.Append(StrL(" on right"));
                diff.Append(StrL("]</span>"));
            }
            wordChanges += wc;
            editRuns += runs;
            if (coarse) coarsePages++;
            if (m.moved) movedPages++;

            double pixelRatio = 0;
            TempStr leftPng, rightPng;
            bool pixelOk = PixelChange(ctx, left, right, m.left, m.right, &pixelRatio, &leftPng, &rightPng);
            if (pixelOk) {
                pixelChanges += pixelRatio;
                pixelPages++;
            }

            if (m.left == m.right) {
                section.Append(fmt("<section class=\"page\" id=\"match-%d\"><a id=\"page-%d\"></a><h2>Page %d",
                                   m.left + 1, m.left + 1, m.left + 1));
            } else {
                section.Append(fmt("<section class=\"page\" id=\"match-%d\"><a id=\"page-%d\"></a><h2>Left page %d, right page %d",
                                   m.left + 1, m.left + 1, m.left + 1, m.right + 1));
            }
            if (m.moved) section.Append(StrL(" (moved)"));
            section.Append(StrL("</h2><p>"));

            if (okA && okB) {
                section.Append(fmt("%d vs %d words; %d changed words in %d edits", aw, bw, wc, runs));
                if (coarse) section.Append(StrL(" (coarse: too many changed words)"));
                section.Append(StrL("; "));
            } else {
                section.Append(StrL("<span class=\"error\">[Text extraction failed]</span> "));
            }
            if (pixelOk) {
                section.Append(fmt("%.2f%% changed pixels</p>", pixelRatio * 100.0));
            } else {
                section.Append(StrL("<span class=\"error\">[Pixel comparison failed: render error]</span></p>"));
            }

            section.Append(StrL("<h3>Word diff</h3><pre>"));
            section.Append(ToStrTemp(diff));
            section.Append(StrL("</pre><div class=\"images\">"));

            if (len(leftPng) > 0) {
                section.Append(StrL("<figure><figcaption>Left</figcaption><img src=\""));
                section.Append(leftPng);
                section.Append(StrL("\"></figure>"));
            } else {
                section.Append(
                    StrL("<figure><figcaption>Left</figcaption><p class=\"error\">[Render failed]</p></figure>"));
            }

            if (len(rightPng) > 0) {
                section.Append(StrL("<figure><figcaption>Right</figcaption><img src=\""));
                section.Append(rightPng);
                section.Append(StrL("\"></figure>"));
            } else {
                section.Append(
                    StrL("<figure><figcaption>Right</figcaption><p class=\"error\">[Render failed]</p></figure>"));
            }

            section.Append(StrL("</div></section>"));
        } else if (m.left >= 0) {
            removedPages++;
            int wc = std::max(1, aw);
            wordChanges += wc;

            TempStr leftPng;
            bool renderOk = RenderPagePng(ctx, left, m.left, leftPng);
            if (renderOk) {
                pixelChanges += 1.0;
                pixelPages++;
            }

            section.Append(fmt("<section class=\"page\" id=\"left-%d\"><a id=\"page-%d\"></a><h2>Page %d (Only in Left / Removed)</h2><p>",
                               m.left + 1, m.left + 1, m.left + 1));
            if (okA) {
                section.Append(fmt("%d words in removed page; %d changed words; 100.00%% changed pixels</p>", aw, wc));
            } else {
                section.Append(StrL("<span class=\"error\">[Text extraction failed]</span> removed page</p>"));
            }
            section.Append(StrL("<h3>Word diff</h3><pre><del>"));
            if (okA) {
                AppendHtml(section, a);
            } else {
                section.Append(StrL("[Text extraction failed]"));
            }
            section.Append(StrL("</del></pre><div class=\"images\">"));
            if (len(leftPng) > 0) {
                section.Append(StrL("<figure><figcaption>Left (Removed)</figcaption><img src=\""));
                section.Append(leftPng);
                section.Append(StrL("\"></figure>"));
            } else {
                section.Append(
                    StrL("<figure><figcaption>Left</figcaption><p class=\"error\">[Render failed]</p></figure>"));
            }
            section.Append(StrL("<figure><figcaption>Right</figcaption><p><em>(Page absent)</em></p></figure>"));
            section.Append(StrL("</div></section>"));
        } else {
            addedPages++;
            int wc = std::max(1, bw);
            wordChanges += wc;

            TempStr rightPng;
            bool renderOk = RenderPagePng(ctx, right, m.right, rightPng);
            if (renderOk) {
                pixelChanges += 1.0;
                pixelPages++;
            }

            section.Append(fmt("<section class=\"page\" id=\"right-%d\"><a id=\"page-%d\"></a><h2>Page %d (Only in Right / Added)</h2><p>",
                               m.right + 1, m.right + 1, m.right + 1));
            if (okB) {
                section.Append(fmt("%d words in added page; %d changed words; 100.00%% changed pixels</p>", bw, wc));
            } else {
                section.Append(StrL("<span class=\"error\">[Text extraction failed]</span> added page</p>"));
            }
            section.Append(StrL("<h3>Word diff</h3><pre><ins>"));
            if (okB) {
                AppendHtml(section, b);
            } else {
                section.Append(StrL("[Text extraction failed]"));
            }
            section.Append(StrL("</ins></pre><div class=\"images\">"));
            section.Append(StrL("<figure><figcaption>Left</figcaption><p><em>(Page absent)</em></p></figure>"));
            if (len(rightPng) > 0) {
                section.Append(StrL("<figure><figcaption>Right (Added)</figcaption><img src=\""));
                section.Append(rightPng);
                section.Append(StrL("\"></figure>"));
            } else {
                section.Append(StrL("<figure><figcaption>Right</figcaption><p class=\"error\">[Render failed]</p></figure>"));
            }
            section.Append(StrL("</div></section>"));
        }

        ok = WriteReportPart(ctx, file, writer, ToStr(section), progress);
        if (ok && progress) AtomicIntSet(&progress->completed, matchIdx + 1);
    }

    if (ok) {
        str::Builder summary;
        summary.Append(StrL("<h2>Summary</h2><ul>"));
        for (const PdfPageMatch& m : matches) {
            if (m.left >= 0 && m.right >= 0) {
                if (m.left == m.right) {
                    summary.Append(fmt("<li><a href=\"#match-%d\">Page %d</a></li>", m.left + 1, m.left + 1));
                } else {
                    summary.Append(
                        fmt("<li><a href=\"#match-%d\">Left page %d, right page %d", m.left + 1, m.left + 1, m.right + 1));
                    if (m.moved) summary.Append(StrL(" (moved)"));
                    summary.Append(StrL("</a></li>"));
                }
            } else if (m.left >= 0) {
                summary.Append(fmt("<li><a href=\"#left-%d\">Page %d (Left only)</a></li>", m.left + 1, m.left + 1));
            } else {
                summary.Append(fmt("<li><a href=\"#right-%d\">Page %d (Right only)</a></li>", m.right + 1, m.right + 1));
            }
        }
        summary.Append(StrL("</ul>"));
        ok = WriteReportPart(ctx, file, writer, ToStr(summary), progress);
    }

    if (writer && ok) {
        fz_try(ctx) {
            fz_close_document_writer(ctx, writer);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            ok = false;
        }
    }
    fz_drop_document_writer(ctx, writer);
    if (file != file::kInvalidFileHandle) {
        if (ok) ok = file::Flush(file);
        file::Close(file);
    }
    if (progress && progress->IsCancelled()) ok = false;
    if (ok) ok = output.Commit();

    double avgPixelPct = pixelPages > 0 ? (pixelChanges * 100.0 / pixelPages) : 0.0;
    if (resultJsonOut) {
        if (ok) {
            *resultJsonOut = str::DupTemp(
                fmt("{\"ok\":true,\"pages\":%d,\"sections\":%d,\"wordChanges\":%d,\"editRuns\":%d,\"movedPages\":%d,"
                    "\"addedPages\":%d,\"removedPages\":%d,\"coarsePages\":%d,\"pixelChangePercent\":%.2f,\"report\":\"\"%s\"\"}",
                    totalPages, len(matches), wordChanges, editRuns, movedPages, addedPages, removedPages, coarsePages,
                    avgPixelPct, json::EscapeStrTemp(reportPath)));
        } else {
            Str errStr = progress && progress->IsCancelled() ? StrL("Cancelled")
                                                             : StrL("Failed to generate comparison report");
            *resultJsonOut = str::DupTemp(
                fmt("{\"ok\":false,\"error\":\"%s\",\"pages\":%d,\"wordChanges\":%d,"
                    "\"pixelChangePercent\":%.2f}",
                    errStr, totalPages, wordChanges, avgPixelPct));
        }
    }
    if (left) fz_drop_document(ctx, left);
    if (right) fz_drop_document(ctx, right);
    fz_drop_context(ctx);
    return ok;
}

struct CompareJob {
    AtomicInt refs = 1;
    AtomicInt done = 0;
    PdfWorkProgress progress;
    Str left, right, report;
    bool ok = false;
    ~CompareJob() {
        str::Free(left);
        str::Free(right);
        str::Free(report);
    }
    void Release() {
        if (AtomicIntDec(&refs) == 0) delete this;
    }
};

static void RunCompare(CompareJob* job) {
    job->ok = ComparePdfReport(job->left, job->right, job->report, nullptr, &job->progress);
    AtomicIntSet(&job->done, 1);
    job->Release();
}

struct ComparePdfDialog : WindowBase {
    MainWindow* win = nullptr;
    VirtFixedLinesText* status = nullptr;
    Edit* leftEdit = nullptr;
    Edit* rightEdit = nullptr;
    Edit* reportEdit = nullptr;
    VirtButton* btnBrowseLeft = nullptr;
    VirtButton* btnBrowseRight = nullptr;
    VirtButton* btnBrowseReport = nullptr;
    VirtButton* btnCompare = nullptr;
    VirtButton* btnClose = nullptr;

    CompareJob* job = nullptr;
    ~ComparePdfDialog() override {
        if (job) {
            AtomicIntSet(&job->progress.cancelled, 1);
            job->Release();
        }
    }
    void Poll(WindowBase::TimerEvent*);
    bool Create(MainWindow* win);
    void OnBrowseLeft(VirtMouseEvent*);
    void OnBrowseRight(VirtMouseEvent*);
    void OnBrowseReport(VirtMouseEvent*);
    void OnCompare(VirtMouseEvent*);
    void OnClose(VirtMouseEvent*) {
        if (job && !AtomicIntGet(&job->done)) {
            AtomicIntSet(&job->progress.cancelled, 1);
            status->SetText(Tr("Cancelling..."));
        } else
            Close();
    }
    void Closed(WindowBase::CloseEvent*) { ScheduleDelete(); }
};

static TempStr PickPdfFileTemp(HWND hwnd) {
    WCHAR buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = dimof(buf);
    ofn.lpstrFilter = L"PDF Documents (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        return ToUtf8Temp(buf);
    }
    return {};
}

static TempStr PickSaveReportFileTemp(HWND hwnd) {
    WCHAR buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = dimof(buf);
    ofn.lpstrFilter = L"HTML Document (*.html)\0*.html\0PDF Document (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrDefExt = L"html";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) {
        return ToUtf8Temp(buf);
    }
    return {};
}

void ComparePdfDialog::OnBrowseLeft(VirtMouseEvent*) {
    TempStr p = PickPdfFileTemp(hwnd);
    if (!len(p)) return;
    leftEdit->SetText(p);
}

void ComparePdfDialog::OnBrowseRight(VirtMouseEvent*) {
    TempStr p = PickPdfFileTemp(hwnd);
    if (!len(p)) return;
    rightEdit->SetText(p);
}

void ComparePdfDialog::OnBrowseReport(VirtMouseEvent*) {
    TempStr p = PickSaveReportFileTemp(hwnd);
    if (!len(p)) return;
    reportEdit->SetText(p);
}

void ComparePdfDialog::OnCompare(VirtMouseEvent*) {
    Str left = leftEdit->GetTextTemp();
    Str right = rightEdit->GetTextTemp();
    Str report = reportEdit->GetTextTemp();
    if (!len(left) || !len(right) || !len(report)) {
        status->SetText(Tr("Please select two PDF documents and a report path."));
        return;
    }
    if (job && !AtomicIntGet(&job->done)) return;
    if (job) job->Release();
    job = new CompareJob();
    job->left = str::Dup(left);
    job->right = str::Dup(right);
    job->report = str::Dup(report);
    btnCompare->SetIsEnabled(false);
    btnClose->SetText(Tr("Cancel"));
    status->SetText(Tr("Comparing documents..."));
    AtomicIntInc(&job->refs);
    ThreadHandle thread = StartThread(MkFunc0(RunCompare, job), StrL("PDF compare"));
    if (!thread) {
        AtomicIntSet(&job->done, 1);
        job->Release();
    }
    SafeCloseThreadHandle(&thread);
    SetTimer(hwnd, 1, 150, nullptr);
}

void ComparePdfDialog::Poll(WindowBase::TimerEvent*) {
    if (!job) return;
    if (!AtomicIntGet(&job->done)) {
        if (!job->progress.IsCancelled())
            status->SetText(fmt(Tr("Comparing: %d of %d pages").s, AtomicIntGet(&job->progress.completed),
                                AtomicIntGet(&job->progress.total)));
        return;
    }
    KillTimer(hwnd, 1);
    btnCompare->SetIsEnabled(true);
    btnClose->SetText(Tr("Close"));
    if (job->ok) {
        OpenPathInDefaultFileManager(job->report);
        Close();
    } else {
        status->SetText(job->progress.IsCancelled() ? Tr("Cancelled. No report written.")
                                                    : Tr("Comparison failed. No report written."));
    }
}

bool ComparePdfDialog::Create(MainWindow* w) {
    win = w;
    onTimer = MkMethod1<ComparePdfDialog, WindowBase::TimerEvent*, &ComparePdfDialog::Poll>(this);
    closeOnEsc = true;
    onClose = MkMethod1<ComparePdfDialog, WindowBase::CloseEvent*, &ComparePdfDialog::Closed>(this);

    CreateCustomArgs args;
    args.owner = win ? win->hwndFrame : nullptr;
    args.title = Tr("Compare PDFs");
    args.font = GetDefaultGuiFont();
    args.style = WS_OVERLAPPEDWINDOW;
    args.visible = false;
    args.bgColor = DarkModeDialogBgColor();
    if (!CreateCustom(args)) return false;

    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;
    box->gap = DpiScale(10);

    status = new VirtFixedLinesText();
    status->font = font;
    status->nLines = 2;
    status->SetText(Tr("Select two PDF documents to compare visual layout and word differences."));
    box->AddChild(status);

    box->AddChild(NewVirtText({.s = Tr("First Document (Left):"), .font = font}));
    auto* h1 = new HBox();
    h1->gap = DpiScale(8);
    Str cur = (win && win->CurrentTab()) ? win->CurrentTab()->filePath : Str();
    Edit::CreateArgs e1;
    e1.parent = hwnd;
    e1.withBorder = true;
    e1.text = cur;
    e1.font = font;
    leftEdit = new Edit();
    leftEdit->Create(e1);
    h1->AddChild(leftEdit, 1);
    btnBrowseLeft = NewThemedButton(hwnd, Tr("Browse..."), font, false);
    btnBrowseLeft->onClick = MkMethod1<ComparePdfDialog, VirtMouseEvent*, &ComparePdfDialog::OnBrowseLeft>(this);
    h1->AddChild(btnBrowseLeft);
    box->AddChild(h1);

    box->AddChild(NewVirtText({.s = Tr("Second Document (Right):"), .font = font}));
    auto* h2 = new HBox();
    h2->gap = DpiScale(8);
    Edit::CreateArgs e2;
    e2.parent = hwnd;
    e2.withBorder = true;
    e2.font = font;
    rightEdit = new Edit();
    rightEdit->Create(e2);
    h2->AddChild(rightEdit, 1);
    btnBrowseRight = NewThemedButton(hwnd, Tr("Browse..."), font, false);
    btnBrowseRight->onClick = MkMethod1<ComparePdfDialog, VirtMouseEvent*, &ComparePdfDialog::OnBrowseRight>(this);
    h2->AddChild(btnBrowseRight);
    box->AddChild(h2);

    box->AddChild(NewVirtText({.s = Tr("Comparison Report Output:"), .font = font}));
    auto* h3 = new HBox();
    h3->gap = DpiScale(8);
    Edit::CreateArgs e3;
    e3.parent = hwnd;
    e3.withBorder = true;
    e3.text = path::JoinTemp(GetTempDirTemp(), StrL("compare_report.html"));
    e3.font = font;
    reportEdit = new Edit();
    reportEdit->Create(e3);
    h3->AddChild(reportEdit, 1);
    btnBrowseReport = NewThemedButton(hwnd, Tr("Browse..."), font, false);
    btnBrowseReport->onClick = MkMethod1<ComparePdfDialog, VirtMouseEvent*, &ComparePdfDialog::OnBrowseReport>(this);
    h3->AddChild(btnBrowseReport);
    box->AddChild(h3);

    auto* buttons = new HBox();
    buttons->gap = DpiScale(8);
    btnCompare = NewThemedButton(hwnd, Tr("Compare"), font, true);
    btnCompare->onClick = MkMethod1<ComparePdfDialog, VirtMouseEvent*, &ComparePdfDialog::OnCompare>(this);
    buttons->AddChild(btnCompare);

    btnClose = NewThemedButton(hwnd, Tr("Close"), font, false);
    btnClose->onClick = MkMethod1<ComparePdfDialog, VirtMouseEvent*, &ComparePdfDialog::OnClose>(this);
    buttons->AddChild(btnClose);
    box->AddChild(buttons);

    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(540), DpiScale(320)};
    AdjustWindowRectEx(&rect, args.style, FALSE, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    UpdateTheme();
    HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
    SetIsVisible(true);
    return true;
}

void ShowComparePdfDialog(MainWindow* win) {
    auto* dlg = new ComparePdfDialog();
    if (!dlg->Create(win)) delete dlg;
}
