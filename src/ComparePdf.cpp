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

static void AppendBase64(str::Builder& out, Str data) {
    static constexpr char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < len(data); i += 3) {
        unsigned value = (u8)data.s[i] << 16;
        if (i + 1 < len(data)) value |= (u8)data.s[i + 1] << 8;
        if (i + 2 < len(data)) value |= (u8)data.s[i + 2];
        out.AppendChar(chars[(value >> 18) & 63]);
        out.AppendChar(chars[(value >> 12) & 63]);
        out.AppendChar(i + 1 < len(data) ? chars[(value >> 6) & 63] : '=');
        out.AppendChar(i + 2 < len(data) ? chars[value & 63] : '=');
    }
}

static TempStr PixmapPngUri(fz_context* ctx, fz_pixmap* pixmap) {
    fz_buffer* buffer = fz_new_buffer_from_pixmap_as_png(ctx, pixmap, fz_default_color_params);
    unsigned char* data = nullptr;
    size_t size = fz_buffer_storage(ctx, buffer, &data);
    str::Builder uri;
    uri.Append(StrL("data:image/png;base64,"));
    AppendBase64(uri, Str((char*)data, (int)size));
    fz_drop_buffer(ctx, buffer);
    return ToStrTemp(uri);
}

// Limits fail explicitly; they never turn an unmeasured page into a match.
static constexpr int kMaxTextBytes = 2 * 1024 * 1024;
static constexpr int kMaxPages = 10000;
static constexpr int kMaxDiffCells = 4 * 1024 * 1024;
static constexpr int kMaxRenderSide = 2048;

static bool Cancelled(CompareProgress* p) {
    return p && InterlockedCompareExchange(&p->cancelled, 0, 0);
}

static bool PageText(fz_context* ctx, fz_document* doc, int pageNo, Str* result) {
    fz_buffer* buf = nullptr;
    fz_stext_page* text = nullptr;
    fz_page* page = nullptr;
    fz_device* device = nullptr;
    bool ok = false;
    fz_var(buf);
    fz_var(text);
    fz_var(page);
    fz_var(device);
    fz_var(ok);
    fz_stext_options opts{};
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, pageNo);
        text = fz_new_stext_page(ctx, fz_bound_page(ctx, page));
        device = fz_new_stext_device(ctx, text, &opts);
        fz_cookie cookie{};
        fz_run_page(ctx, page, device, fz_identity, &cookie);
        fz_close_device(ctx, device);
        if (cookie.errors || cookie.incomplete) fz_throw(ctx, FZ_ERROR_FORMAT, "Incomplete page text");
        buf = fz_new_buffer_from_stext_page(ctx, text);
        unsigned char* data = nullptr;
        size_t size = fz_buffer_storage(ctx, buf, &data);
        if (size > kMaxTextBytes) fz_throw(ctx, FZ_ERROR_LIMIT, "Page text exceeds comparison limit");
        *result = str::DupTemp(Str((char*)data, (int)size));
        ok = true;
    }
    fz_always(ctx) {
        fz_drop_device(ctx, device);
        fz_drop_buffer(ctx, buf);
        fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return ok;
}

static void SplitWords(Str s, Vec<Str>& words) {
    int start = -1;
    for (int i = 0; i <= len(s); i++) {
        bool ws = i == len(s) || str::IsWs(s.s[i]);
        if (!ws && start < 0) start = i;
        if (ws && start >= 0) {
            VecAppend(words, Str(s.s + start, i - start));
            start = -1;
        }
    }
}

// Bounded LCS traceback preserves unchanged islands between independent edits.
static int AppendWordDiff(str::Builder& out, Str left, Str right, CompareProgress* progress) {
    Vec<Str> a, b;
    SplitWords(left, a);
    SplitWords(right, b);
    int prefix = 0, suffix = 0;
    while (prefix < len(a) && prefix < len(b) && str::Eq(a[prefix], b[prefix])) prefix++;
    while (suffix < len(a) - prefix && suffix < len(b) - prefix &&
           str::Eq(a[len(a) - suffix - 1], b[len(b) - suffix - 1]))
        suffix++;
    for (int k = 0; k < prefix; k++) {
        AppendHtml(out, a[k]);
        out.AppendChar(' ');
    }
    int n = len(a) - prefix - suffix, m = len(b) - prefix - suffix;
    if ((i64)(n + 1) * (m + 1) > kMaxDiffCells) return -1;
    Vec<int> cells;
    VecResize(cells, (n + 1) * (m + 1));
    auto at = [&](int i, int j) -> int& { return cells[i * (m + 1) + j]; };
    for (int i = n - 1; i >= 0; i--) {
        if (Cancelled(progress)) return -1;
        for (int j = m - 1; j >= 0; j--)
            at(i, j) =
                str::Eq(a[i + prefix], b[j + prefix]) ? 1 + at(i + 1, j + 1) : std::max(at(i + 1, j), at(i, j + 1));
    }
    int i = 0, j = 0, changes = 0;
    while (i < n || j < m) {
        if (i < n && j < m && str::Eq(a[i + prefix], b[j + prefix])) {
            AppendHtml(out, a[prefix + i++]);
            j++;
            out.AppendChar(' ');
            continue;
        }
        int removed = 0, added = 0;
        while ((i < n || j < m) && !(i < n && j < m && str::Eq(a[i + prefix], b[j + prefix]))) {
            bool del = i < n && (j == m || at(i + 1, j) >= at(i, j + 1));
            out.Append(del ? StrL("<del>") : StrL("<ins>"));
            AppendHtml(out, del ? a[prefix + i++] : b[prefix + j++]);
            out.Append(del ? StrL("</del> ") : StrL("</ins> "));
            if (del)
                removed++;
            else
                added++;
        }
        changes += std::max(removed, added);
    }
    for (int k = len(a) - suffix; k < len(a); k++) {
        AppendHtml(out, a[k]);
        out.AppendChar(' ');
    }
    return changes;
}

static fz_pixmap* RenderPage(fz_context* ctx, fz_document* doc, int index) {
    fz_page* page = fz_load_page(ctx, doc, index);
    fz_pixmap* pix = nullptr;
    fz_device* device = nullptr;
    fz_var(pix);
    fz_var(device);
    fz_try(ctx) {
        fz_rect box = fz_bound_page(ctx, page);
        float side = std::max(box.x1 - box.x0, box.y1 - box.y0);
        if (!(side > 0) || !isfinite(side)) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page bounds");
        float scale = std::min(1.0f, (float)kMaxRenderSide / side);
        fz_matrix matrix = fz_scale(scale, scale);
        pix =
            fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), fz_round_rect(fz_transform_rect(box, matrix)), nullptr, 0);
        fz_clear_pixmap_with_value(ctx, pix, 255);
        device = fz_new_draw_device(ctx, matrix, pix);
        fz_cookie cookie{};
        fz_run_page(ctx, page, device, fz_identity, &cookie);
        fz_close_device(ctx, device);
        if (cookie.errors || cookie.incomplete) fz_throw(ctx, FZ_ERROR_FORMAT, "Incomplete page rendering");
    }
    fz_always(ctx) {
        fz_drop_device(ctx, device);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx) {
        fz_drop_pixmap(ctx, pix);
        fz_rethrow(ctx);
    }
    return pix;
}

static bool PixelChange(fz_context* ctx, fz_document* a, fz_document* b, int ai, int bi, double* ratio, Str* leftPng,
                        Str* rightPng) {
    fz_pixmap* x = nullptr;
    fz_pixmap* y = nullptr;
    bool ok = false;
    fz_var(x);
    fz_var(y);
    fz_var(ok);
    fz_try(ctx) {
        if (ai >= 0) x = RenderPage(ctx, a, ai);
        if (bi >= 0) y = RenderPage(ctx, b, bi);
        if (x) *leftPng = PixmapPngUri(ctx, x);
        if (y) *rightPng = PixmapPngUri(ctx, y);
        int aw = x ? x->w : 0, ah = x ? x->h : 0;
        int bw = y ? y->w : 0, bh = y ? y->h : 0;
        int w = std::max(aw, bw), h = std::max(ah, bh), changed = 0;
        for (int row = 0; row < h; row++)
            for (int col = 0; col < w; col++) {
                bool different = col >= aw || col >= bw || row >= ah || row >= bh;
                if (!different) {
                    auto* as = x->samples + row * x->stride + col * x->n;
                    auto* bs = y->samples + row * y->stride + col * y->n;
                    different = abs(as[0] - bs[0]) > 24 || abs(as[1] - bs[1]) > 24 || abs(as[2] - bs[2]) > 24;
                }
                changed += different;
            }
        *ratio = (double)changed / std::max(1, w * h);
        ok = true;
    }
    fz_always(ctx) {
        fz_drop_pixmap(ctx, x);
        fz_drop_pixmap(ctx, y);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return ok;
}

struct PageSignature {
    u64 hash = 0;
    int bytes = 0;
    u64 sketch[16]{};
};
static bool RasterSignature(fz_context* ctx, fz_document* doc, int page, u64* hash) {
    fz_pixmap* pix = nullptr;
    bool ok = false;
    fz_var(pix);
    fz_var(ok);
    fz_try(ctx) {
        pix = RenderPage(ctx, doc, page);
        u64 h = 14695981039346656037ULL;
        for (int y = 0; y < pix->h; y++)
            for (int x = 0; x < pix->w * pix->n; x++) {
                h ^= pix->samples[y * pix->stride + x];
                h *= 1099511628211ULL;
            }
        *hash = h ^ ((u64)pix->w << 32) ^ pix->h;
        ok = true;
    }
    fz_always(ctx) {
        fz_drop_pixmap(ctx, pix);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return ok;
}

static bool SignPages(fz_context* ctx, fz_document* doc, int count, Vec<PageSignature>& sig, CompareProgress* p) {
    for (int i = 0; i < count; i++) {
        AutoArenaSavepoint scratch;
        if (Cancelled(p)) return false;
        Str text;
        if (!PageText(ctx, doc, i, &text)) return false;
        u64 h = 14695981039346656037ULL;
        for (int j = 0; j < len(text); j++) {
            h ^= (u8)text.s[j];
            h *= 1099511628211ULL;
        }
        PageSignature signature{h, len(text)};
        if (!len(text)) {
            if (!RasterSignature(ctx, doc, i, &signature.hash)) return false;
            signature.bytes = -1;
        }
        Vec<Str> words;
        SplitWords(text, words);
        for (Str word : words) {
            u64 wh = 14695981039346656037ULL;
            for (int k = 0; k < len(word); k++) {
                wh ^= (u8)word.s[k];
                wh *= 1099511628211ULL;
            }
            int bucket = wh % 16;
            if (!signature.sketch[bucket] || wh < signature.sketch[bucket]) signature.sketch[bucket] = wh;
        }
        VecAppend(sig, signature);
        if (p) InterlockedIncrement(&p->completed);
    }
    return true;
}

static bool WriteStory(fz_context* ctx, fz_document_writer* writer, Str html, CompareProgress* p) {
    fz_buffer* buffer = nullptr;
    fz_story* story = nullptr;
    bool ok = false;
    fz_var(buffer);
    fz_var(story);
    fz_var(ok);
    fz_try(ctx) {
        buffer = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)html.s, len(html));
        story =
            fz_new_story(ctx, buffer, "body{font-size:10pt}img{max-width:500pt}pre{white-space:pre-wrap}", 10, nullptr);
        int more = 1, count = 0;
        while (more) {
            if (Cancelled(p) || ++count > 1000) fz_throw(ctx, FZ_ERROR_LIMIT, "Report cancelled or too long");
            fz_rect filled;
            more = fz_place_story_flags(ctx, story, fz_make_rect(36, 36, 576, 756), &filled,
                                        FZ_PLACE_STORY_FLAG_NO_OVERFLOW);
            if (more == FZ_PLACE_STORY_RETURN_OVERFLOW_WIDTH) fz_throw(ctx, FZ_ERROR_LIMIT, "Report content too wide");
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

bool ComparePdfReport(Str leftPath, Str rightPath, Str reportPath, Str* resultJsonOut, CompareProgress* progress) {
    if (resultJsonOut)
        *resultJsonOut = StrL("{\"ok\":false,\"error\":\"Comparison failed or exceeded limits; no report published\"}");
    if (path::IsSame(leftPath, reportPath) || path::IsSame(rightPath, reportPath)) return false;
    file::AtomicFile output(reportPath);
    if (!len(output.tempPath)) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, 64 * 1024 * 1024);
    if (!ctx) return false;
    fz_document* left = nullptr;
    fz_document* right = nullptr;
    fz_document_writer* writer = nullptr;
    int na = 0, nb = 0;
    bool ok = false;
    fz_var(left);
    fz_var(right);
    fz_var(writer);
    fz_var(na);
    fz_var(nb);
    fz_var(ok);
    bool pdf = str::EndsWithI(reportPath, StrL(".pdf"));
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        left = fz_open_document(ctx, CStrTemp(leftPath));
        right = fz_open_document(ctx, CStrTemp(rightPath));
        if (fz_needs_password(ctx, left) || fz_needs_password(ctx, right))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Password required");
        na = fz_count_pages(ctx, left);
        nb = fz_count_pages(ctx, right);
        if (na <= 0 || nb <= 0 || na > kMaxPages || nb > kMaxPages)
            fz_throw(ctx, FZ_ERROR_LIMIT, "Page count out of range");
        if (pdf) writer = fz_new_pdf_writer(ctx, CStrTemp(output.tempPath), "compress");
        ok = true;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    file::FileHandle handle = pdf ? file::kInvalidFileHandle : file::OpenReadWrite(output.tempPath, false);
    if (!pdf && handle == file::kInvalidFileHandle) ok = false;
    if (progress) {
        InterlockedExchange(&progress->total, 2 * (na + nb));
        InterlockedExchange(&progress->completed, 0);
    }
    Vec<PageSignature> a, b;
    if (ok) ok = SignPages(ctx, left, na, a, progress) && SignPages(ctx, right, nb, b, progress);
    Vec<int> matches, used;
    VecResize(matches, na);
    VecResize(used, nb);
    for (int i = 0; i < na; i++) matches[i] = -1;
    // Match exact text or raster fingerprints globally first, including moved scans.
    for (int i = 0; ok && i < na; i++)
        for (int j = 0; j < nb; j++) {
            if (!used[j] && a[i].bytes && a[i].bytes == b[j].bytes && a[i].hash == b[j].hash) {
                matches[i] = j;
                used[j] = 1;
                break;
            }
        }
    // Similarity sketches pair edited pages without consuming unrelated insertions.
    for (int i = 0; ok && i < na; i++)
        if (matches[i] < 0) {
            if (Cancelled(progress)) {
                ok = false;
                break;
            }
            int best = -1;
            double score = 0.25;
            for (int j = 0; j < nb; j++)
                if (!used[j]) {
                    int equal = 0, occupied = 0;
                    for (int k = 0; k < 16; k++) {
                        if (a[i].sketch[k] || b[j].sketch[k]) occupied++;
                        if (a[i].sketch[k] && a[i].sketch[k] == b[j].sketch[k]) equal++;
                    }
                    double candidate = occupied ? (double)equal / occupied : (i == j ? 1.0 : 0.0);
                    if (candidate > score) {
                        best = j;
                        score = candidate;
                    }
                }
            if (best >= 0) {
                matches[i] = best;
                used[best] = 1;
            }
        }
    auto write = [&](Str html) { return pdf ? WriteStory(ctx, writer, html, progress) : file::WriteAll(handle, html); };
    if (ok)
        ok =
            write(StrL("<!doctype html><meta charset=utf-8><title>PDF comparison</title><style>body{font:15px "
                       "sans-serif}pre{white-space:pre-wrap}del{background:#fdd}ins{background:#dfd}img{max-width:48%}<"
                       "/style><h1>PDF comparison</h1><p>72 dpi previews, downscaled to 2048 pixels per side. "
                       "Text/raster fingerprint alignment; edited scans use page order.</p>"));
    int words = 0, compared = 0;
    double pixels = 0;
    for (int index = 0; ok && index < na + nb; index++) {
        AutoArenaSavepoint scratch;
        int ai = index < na ? index : -1;
        int bi = index < na ? matches[index] : index - na;
        if (index >= na && used[bi]) continue;
        if (Cancelled(progress)) {
            ok = false;
            break;
        }
        Str at, bt;
        if ((ai >= 0 && !PageText(ctx, left, ai, &at)) || (bi >= 0 && !PageText(ctx, right, bi, &bt))) {
            ok = false;
            break;
        }
        str::Builder section;
        section.Append(fmt("<section><h2>Left page %d / Right page %d</h2><p>%s</p><pre>", ai + 1, bi + 1,
                           ai < 0   ? StrL("Inserted page")
                           : bi < 0 ? StrL("Deleted page")
                                    : StrL("Aligned pages")));
        int changed = AppendWordDiff(section, at, bt, progress);
        double ratio = 0;
        Str ap, bp;
        if (changed < 0 || !PixelChange(ctx, left, right, ai, bi, &ratio, &ap, &bp)) {
            ok = false;
            break;
        }
        words += changed;
        pixels += ratio;
        compared++;
        section.Append(fmt("</pre><p>%d changed words; %.2f%% changed pixels</p>", changed, ratio * 100));
        if (len(ap)) {
            section.Append(StrL("<img alt=Left src=\""));
            section.Append(ap);
            section.Append(StrL("\">"));
        }
        if (len(bp)) {
            section.Append(StrL("<img alt=Right src=\""));
            section.Append(bp);
            section.Append(StrL("\">"));
        }
        section.Append(StrL("</section>"));
        ok = write(ToStr(section));
        if (progress) InterlockedIncrement(&progress->completed);
    }
    if (ok) ok = write(fmt("<h2>Summary</h2><p>%d page pairs; %d changed words</p>", compared, words));
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
    if (handle != file::kInvalidFileHandle) {
        if (!file::Flush(handle)) ok = false;
        file::Close(handle);
    }
    fz_drop_document(ctx, left);
    fz_drop_document(ctx, right);
    fz_drop_context(ctx);
    if (ok && !Cancelled(progress))
        ok = output.Commit();
    else
        ok = false;
    if (ok && resultJsonOut)
        *resultJsonOut = fmt("{\"ok\":true,\"pages\":%d,\"wordChanges\":%d,\"pixelChangePercent\":%.2f}", compared,
                             words, compared ? pixels * 100 / compared : 0);
    if (ok && progress) InterlockedExchange(&progress->completed, InterlockedCompareExchange(&progress->total, 0, 0));
    return ok;
}

struct CompareJob {
    volatile LONG refs = 1, done = 0;
    CompareProgress progress;
    Str left, right, report;
    bool ok = false;
    ~CompareJob() {
        str::Free(left);
        str::Free(right);
        str::Free(report);
    }
    void Release() {
        if (!InterlockedDecrement(&refs)) delete this;
    }
};
static void RunCompare(CompareJob* job) {
    job->ok = ComparePdfReport(job->left, job->right, job->report, nullptr, &job->progress);
    InterlockedExchange(&job->done, 1);
    job->Release();
}

struct ComparePdfDialog : WindowBase {
    MainWindow* win = nullptr;
    CompareJob* job = nullptr;
    ~ComparePdfDialog() override {
        if (job) {
            InterlockedExchange(&job->progress.cancelled, 1);
            job->Release();
        }
    }
    void Poll(WindowBase::TimerEvent*);

    VirtFixedLinesText* status = nullptr;
    Edit* leftEdit = nullptr;
    Edit* rightEdit = nullptr;
    Edit* reportEdit = nullptr;
    VirtButton* btnBrowseLeft = nullptr;
    VirtButton* btnBrowseRight = nullptr;
    VirtButton* btnBrowseReport = nullptr;
    VirtButton* btnCompare = nullptr;
    VirtButton* btnClose = nullptr;

    bool Create(MainWindow* win);
    void OnBrowseLeft(VirtMouseEvent*);
    void OnBrowseRight(VirtMouseEvent*);
    void OnBrowseReport(VirtMouseEvent*);
    void OnCompare(VirtMouseEvent*);
    void OnClose(VirtMouseEvent*) {
        if (job && !InterlockedCompareExchange(&job->done, 0, 0)) {
            InterlockedExchange(&job->progress.cancelled, 1);
            status->SetText(Tr("Cancelling..."));
            return;
        }
        Close();
    }
    void Closed(WindowBase::CloseEvent*) { Close(); }
};

static TempStr PickPdfFileTemp(HWND hwnd) {
    WCHAR buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = dimof(buf);
    ofn.lpstrFilter = L"PDF Files\0*.pdf\0All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return {};
    return ToUtf8Temp(WStr(buf));
}

void ComparePdfDialog::OnBrowseLeft(VirtMouseEvent*) {
    TempStr path = PickPdfFileTemp(hwnd);
    if (len(path) > 0) leftEdit->SetText(path);
}

void ComparePdfDialog::OnBrowseRight(VirtMouseEvent*) {
    TempStr path = PickPdfFileTemp(hwnd);
    if (len(path) > 0) rightEdit->SetText(path);
}

void ComparePdfDialog::OnBrowseReport(VirtMouseEvent*) {
    WCHAR buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = dimof(buf);
    ofn.lpstrFilter = L"HTML Report\0*.html\0PDF Report\0*.pdf\0All Files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    reportEdit->SetText(ToUtf8Temp(WStr(buf)));
}

void ComparePdfDialog::OnCompare(VirtMouseEvent*) {
    TempStr left = leftEdit->GetTextTemp();
    TempStr right = rightEdit->GetTextTemp();
    TempStr report = reportEdit->GetTextTemp();
    if (len(left) == 0 || len(right) == 0 || len(report) == 0) {
        status->SetText(Tr("Please select two PDF documents and a report path."));
        return;
    }
    if (job && !InterlockedCompareExchange(&job->done, 0, 0)) return;
    if (job) job->Release();
    job = new CompareJob();
    job->left = str::Dup(left);
    job->right = str::Dup(right);
    job->report = str::Dup(report);
    InterlockedIncrement(&job->refs);
    ThreadHandle thread = StartThread(MkFunc0(RunCompare, job), StrL("Compare PDFs"));
    if (!thread) {
        job->Release();
        job->Release();
        job = nullptr;
        status->SetText(Tr("Could not start comparison."));
        return;
    }
    SafeCloseThreadHandle(&thread);
    btnCompare->SetIsEnabled(false);
    btnClose->SetText(Tr("Cancel"));
    status->SetText(Tr("Comparing documents..."));
    SetTimer(hwnd, 1, 100, nullptr);
}

void ComparePdfDialog::Poll(WindowBase::TimerEvent*) {
    if (!job) return;
    if (!InterlockedCompareExchange(&job->done, 0, 0)) {
        status->SetText(fmt("Comparing: %d / %d", (int)InterlockedCompareExchange(&job->progress.completed, 0, 0),
                            (int)InterlockedCompareExchange(&job->progress.total, 0, 0)));
        return;
    }
    KillTimer(hwnd, 1);
    btnCompare->SetIsEnabled(true);
    btnClose->SetText(Tr("Close"));
    status->SetText(job->ok ? Tr("Report saved.")
                            : Tr("Comparison failed, cancelled or exceeded limits. No report published."));
    if (job->ok) OpenPathInDefaultFileManager(job->report);
}

bool ComparePdfDialog::Create(MainWindow* w) {
    win = w;
    closeOnEsc = true;
    onTimer = MkMethod1<ComparePdfDialog, WindowBase::TimerEvent*, &ComparePdfDialog::Poll>(this);
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
