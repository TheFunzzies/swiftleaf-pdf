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

static bool ReadPageText(fz_context* ctx, fz_document* doc, int pageNo, Str* result) {
    fz_buffer* buf = nullptr;
    fz_stext_page* text = nullptr;
    fz_page* page = nullptr;
    fz_device* dev = nullptr;
    bool ok = false;
    fz_var(dev);
    fz_var(buf);
    fz_var(text);
    fz_var(page);
    fz_var(ok);
    fz_stext_options opts{};
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, pageNo);
        text = fz_new_stext_page(ctx, fz_bound_page(ctx, page));
        dev = fz_new_stext_device(ctx, text, &opts);
        fz_cookie cookie{};
        fz_run_page(ctx, page, dev, fz_identity, &cookie);
        fz_close_device(ctx, dev);
        if (cookie.errors || cookie.incomplete) fz_throw(ctx, FZ_ERROR_FORMAT, "Incomplete page text");
        buf = fz_new_buffer_from_stext_page(ctx, text);
        unsigned char* data = nullptr;
        size_t size = fz_buffer_storage(ctx, buf, &data);
        if (size > 1024 * 1024) fz_throw(ctx, FZ_ERROR_LIMIT, "Page text exceeds comparison limit");
        *result = str::DupTemp(Str((char*)data, (int)size));
        ok = true;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_device(ctx, dev);
    fz_drop_buffer(ctx, buf);
    fz_drop_stext_page(ctx, text);
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
        bool ws = i == len(s) || str::IsWs(s.s[i]);
        if (!ws && start < 0) start = i;
        if (ws && start >= 0) {
            VecAppend(words, Str(s.s + start, i - start));
            start = -1;
        }
    }
}

static int AppendWordDiff(str::Builder& out, Str left, Str right) {
    Vec<Str> a, b;
    SplitWords(left, a);
    SplitWords(right, b);
    constexpr u64 maxCells = 4 * 1024 * 1024;
    u64 cells = (u64)(len(a) + 1) * (len(b) + 1);
    if (cells > maxCells) return -1;
    Vec<int> table;
    VecResize(table, (int)cells);
    int width = len(b) + 1;
    for (int i = len(a) - 1; i >= 0; i--) {
        for (int j = len(b) - 1; j >= 0; j--) {
            table[i * width + j] = str::Eq(a[i], b[j]) ? 1 + table[(i + 1) * width + j + 1]
                                                       : std::max(table[(i + 1) * width + j], table[i * width + j + 1]);
        }
    }
    int i = 0, j = 0, changes = 0;
    while (i < len(a) || j < len(b)) {
        if (i < len(a) && j < len(b) && str::Eq(a[i], b[j])) {
            if (len(out)) out.AppendChar(' ');
            AppendHtml(out, a[i++]);
            j++;
            continue;
        }
        bool remove = i < len(a) && (j == len(b) || table[(i + 1) * width + j] >= table[i * width + j + 1]);
        out.Append(remove ? StrL(" <del>") : StrL(" <ins>"));
        AppendHtml(out, remove ? a[i++] : b[j++]);
        out.Append(remove ? StrL("</del>") : StrL("</ins>"));
        changes++;
    }
    return changes;
}

static fz_pixmap* ComparePixmap(fz_context* ctx, fz_page* page) {
    fz_rect box = fz_bound_page(ctx, page);
    float side = std::max(box.x1 - box.x0, box.y1 - box.y0);
    if (!isfinite(side) || side <= 0) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page bounds");
    float scale = std::min(0.25f, 1024.0f / side);
    fz_matrix transform = fz_scale(scale, scale);
    fz_irect bounds = fz_round_rect(fz_transform_rect(box, transform));
    fz_pixmap* pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), bounds, nullptr, 0);
    fz_device* dev = nullptr;
    fz_var(dev);
    fz_try(ctx) {
        fz_clear_pixmap_with_value(ctx, pix, 255);
        dev = fz_new_draw_device(ctx, transform, pix);
        fz_cookie cookie{};
        fz_run_page(ctx, page, dev, fz_identity, &cookie);
        fz_close_device(ctx, dev);
        if (cookie.errors || cookie.incomplete) fz_throw(ctx, FZ_ERROR_FORMAT, "Incomplete page render");
    }
    fz_always(ctx) {
        fz_drop_device(ctx, dev);
    }
    fz_catch(ctx) {
        fz_drop_pixmap(ctx, pix);
        fz_rethrow(ctx);
    }
    return pix;
}

static bool PixelChange(fz_context* ctx, fz_document* a, fz_document* b, int leftNo, int rightNo, double* ratioOut,
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
        pa = fz_load_page(ctx, a, leftNo);
        pb = fz_load_page(ctx, b, rightNo);
        xa = ComparePixmap(ctx, pa);
        xb = ComparePixmap(ctx, pb);
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

static bool CompareCancelled(CompareProgress* progress) {
    return progress && InterlockedCompareExchange(&progress->cancelled, 0, 0);
}

struct PageFingerprint {
    unsigned char digest[16]{};
};

static bool Fingerprint(fz_context* ctx, fz_document* doc, int n, PageFingerprint& result) {
    Str text;
    if (!ReadPageText(ctx, doc, n, &text)) return false;
    fz_page* page = nullptr;
    fz_pixmap* pix = nullptr;
    bool ok = false;
    fz_var(page);
    fz_var(pix);
    fz_var(ok);
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, n);
        pix = ComparePixmap(ctx, page);
        fz_md5 hash;
        fz_md5_init(&hash);
        fz_md5_update(&hash, (const unsigned char*)text.s, len(text));
        fz_md5_update_int64(&hash, pix->w);
        fz_md5_update_int64(&hash, pix->h);
        for (int y = 0; y < pix->h; y++) fz_md5_update(&hash, pix->samples + y * pix->stride, pix->w * pix->n);
        fz_md5_final(&hash, result.digest);
        ok = true;
    }
    fz_always(ctx) {
        fz_drop_pixmap(ctx, pix);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return ok;
}

// Exact page matches survive insertions and moves. Unmatched gaps pair in order.
static void AlignPages(const Vec<PageFingerprint>& a, const Vec<PageFingerprint>& b, Vec<int>& pairs, Vec<int>& used) {
    VecResize(pairs, len(a));
    VecResize(used, len(b));
    for (int i = 0; i < len(a); i++) {
        pairs[i] = -1;
        for (int j = 0; j < len(b); j++) {
            if (!used[j] && memcmp(a[i].digest, b[j].digest, 16) == 0) {
                pairs[i] = j;
                used[j] = 1;
                break;
            }
        }
    }
    int next = 0;
    for (int i = 0; i < len(a); i++) {
        if (pairs[i] >= 0) {
            next = std::max(next, pairs[i] + 1);
            continue;
        }
        int end = len(b);
        for (int k = i + 1; k < len(a); k++)
            if (pairs[k] >= 0) {
                end = pairs[k];
                break;
            }
        while (next < end && used[next]) next++;
        if (next < end) {
            pairs[i] = next;
            used[next++] = 1;
        }
    }
}

static bool WriteReportPart(fz_context* ctx, HANDLE file, fz_document_writer* writer, Str html,
                            CompareProgress* progress) {
    if (CompareCancelled(progress)) return false;
    if (!writer) {
        DWORD written = 0;
        return WriteFile(file, html.s, len(html), &written, nullptr) && written == (DWORD)len(html);
    }
    fz_buffer* buffer = nullptr;
    fz_story* story = nullptr;
    bool ok = false;
    fz_var(buffer);
    fz_var(story);
    fz_var(ok);
    fz_try(ctx) {
        buffer = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)html.s, len(html));
        story = fz_new_story(ctx, buffer, "body{font:11pt sans-serif} img{max-width:250pt} pre{white-space:pre-wrap}",
                             11, nullptr);
        int more = 1, pages = 0;
        while (more) {
            if (CompareCancelled(progress)) fz_throw(ctx, FZ_ERROR_ABORT, "Cancelled");
            if (++pages > 1000) fz_throw(ctx, FZ_ERROR_LIMIT, "Report section too long");
            fz_rect filled;
            more = fz_place_story_flags(ctx, story, fz_make_rect(36, 36, 576, 756), &filled,
                                        FZ_PLACE_STORY_FLAG_NO_OVERFLOW);
            if (more == FZ_PLACE_STORY_RETURN_OVERFLOW_WIDTH) fz_throw(ctx, FZ_ERROR_LIMIT, "Report overflow");
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
        *resultJsonOut = StrL("{\"ok\":false,\"error\":\"Comparison failed or cancelled; no report published\"}");
    if (path::IsSame(leftPath, reportPath) || path::IsSame(rightPath, reportPath)) return false;
    file::AtomicFile output(reportPath);
    if (!len(output.tempPath)) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, 64 * 1024 * 1024);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    fz_document* left = nullptr;
    fz_document* right = nullptr;
    fz_document_writer* writer = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
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
        leftPages = fz_count_pages(ctx, left);
        rightPages = fz_count_pages(ctx, right);
        if (leftPages <= 0 || rightPages <= 0 || leftPages > 4096 || rightPages > 4096)
            fz_throw(ctx, FZ_ERROR_LIMIT, "Compare supports 1 to 4096 pages per document");
        if (str::EndsWithI(reportPath, StrL(".pdf")))
            writer = fz_new_pdf_writer(ctx, CStrTemp(output.tempPath), "compress");
        ok = true;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    if (ok && !writer) {
        file = CreateFileW(CWStrTemp(output.tempPath), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
        ok = file != INVALID_HANDLE_VALUE;
    }
    Vec<PageFingerprint> a, b;
    VecResize(a, ok ? leftPages : 0);
    VecResize(b, ok ? rightPages : 0);
    if (progress) InterlockedExchange(&progress->total, 2 * (leftPages + rightPages));
    int completed = 0;
    for (int side = 0; ok && side < 2; side++) {
        auto& hashes = side ? b : a;
        for (int i = 0; ok && i < len(hashes); i++) {
            AutoArenaSavepoint scratch;
            ok = !CompareCancelled(progress) && Fingerprint(ctx, side ? right : left, i, hashes[i]);
            if (progress) InterlockedExchange(&progress->completed, ++completed);
        }
    }
    Vec<int> pairs, used;
    if (ok) AlignPages(a, b, pairs, used);
    Str header = StrL(
        "<!doctype html><meta charset=\"utf-8\"><title>PDF comparison</title><style>body{font:15px "
        "sans-serif}pre{white-space:pre-wrap}del{background:#ffd8d8}ins{background:#d8ffe0}img{max-width:45%}</"
        "style><h1>PDF comparison</h1><p>Word edits count inserted and deleted words. Pixel comparison uses capped "
        "thumbnails. Page alignment uses exact text and visual fingerprints; edited pages pair within unmatched "
        "gaps.</p>");
    if (ok) ok = WriteReportPart(ctx, file, writer, header, progress);
    int words = 0, compared = 0, added = 0, removed = 0;
    double pixels = 0;
    for (int index = 0; ok && index < leftPages + rightPages; index++) {
        AutoArenaSavepoint scratch;
        int l = index < leftPages ? index : -1;
        int r = l >= 0 ? pairs[l] : index - leftPages;
        if (l < 0 && used[r]) continue;
        Str lt, rt;
        ok = !CompareCancelled(progress) && (l < 0 || ReadPageText(ctx, left, l, &lt)) &&
             (r < 0 || ReadPageText(ctx, right, r, &rt));
        if (!ok) break;
        str::Builder part;
        part.Append(fmt("<section><h2>Left page %d / Right page %d</h2>", l + 1, r + 1));
        if (l < 0 || r < 0) {
            added += l < 0;
            removed += r < 0;
            part.Append(l < 0 ? StrL("<p>Inserted page</p><pre><ins>") : StrL("<p>Deleted page</p><pre><del>"));
            AppendHtml(part, l < 0 ? rt : lt);
            words += CountWords(l < 0 ? rt : lt);
            part.Append(l < 0 ? StrL("</ins></pre>") : StrL("</del></pre>"));
            double ignored;
            Str image, duplicate;
            fz_document* doc = l < 0 ? right : left;
            int page = l < 0 ? r : l;
            ok = PixelChange(ctx, doc, doc, page, page, &ignored, &image, &duplicate);
            if (!ok) break;
            part.Append(fmt("<img src=\"%s\">", image));
        } else {
            if (l != r) part.Append(StrL("<p>Moved or shifted page</p>"));
            part.Append(StrL("<pre>"));
            int changes = AppendWordDiff(part, lt, rt);
            if (changes < 0) {
                ok = false;
                break;
            }
            words += changes;
            double ratio = 0;
            Str lp, rp;
            ok = PixelChange(ctx, left, right, l, r, &ratio, &lp, &rp);
            if (!ok) break;
            pixels += ratio;
            compared++;
            part.Append(fmt("</pre><p>%d word edits; %.2f%% changed pixels</p><img src=\"%s\"><img src=\"%s\">",
                            changes, ratio * 100, lp, rp));
        }
        part.Append(StrL("</section>"));
        ok = WriteReportPart(ctx, file, writer, ToStr(part), progress);
        if (progress) InterlockedExchange(&progress->completed, ++completed);
    }
    if (writer) {
        fz_try(ctx) {
            if (ok) fz_close_document_writer(ctx, writer);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            ok = false;
        }
        fz_drop_document_writer(ctx, writer);
    }
    if (file != INVALID_HANDLE_VALUE) {
        if (!FlushFileBuffers(file)) ok = false;
        CloseHandle(file);
    }
    fz_drop_document(ctx, left);
    fz_drop_document(ctx, right);
    fz_drop_context(ctx);
    ok = ok && !CompareCancelled(progress) && output.Commit();
    if (ok && resultJsonOut)
        *resultJsonOut =
            fmt("{\"ok\":true,\"pages\":%d,\"insertedPages\":%d,\"deletedPages\":%d,\"wordChanges\":%d,"
                "\"pixelChangePercent\":%.2f}",
                compared, added, removed, words, compared ? pixels * 100 / compared : 0);
    if (ok && progress) InterlockedExchange(&progress->completed, progress->total);
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
    CompareJob* job = nullptr;
    ~ComparePdfDialog() override {
        if (job) {
            InterlockedExchange(&job->progress.cancelled, 1);
            job->Release();
        }
    }
    void Poll(WindowBase::TimerEvent*);
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
    void Closed(WindowBase::CloseEvent*) { ScheduleDelete(); }
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
    btnCompare->SetIsEnabled(false);
    btnClose->SetText(Tr("Cancel"));
    status->SetText(Tr("Comparing documents..."));
    InterlockedIncrement(&job->refs);
    ThreadHandle thread = StartThread(MkFunc0(RunCompare, job), StrL("Compare PDFs"));
    if (!thread) {
        job->Release();
        InterlockedExchange(&job->done, 1);
    } else
        SafeCloseThreadHandle(&thread);
    SetTimer(hwnd, 1, 100, nullptr);
}

void ComparePdfDialog::Poll(WindowBase::TimerEvent*) {
    if (!job) return;
    if (!InterlockedCompareExchange(&job->done, 0, 0)) {
        status->SetText(fmt(Tr("Comparing: %d of %d steps").s,
                            (int)InterlockedCompareExchange(&job->progress.completed, 0, 0),
                            (int)InterlockedCompareExchange(&job->progress.total, 0, 0)));
        return;
    }
    KillTimer(hwnd, 1);
    btnCompare->SetIsEnabled(true);
    btnClose->SetText(Tr("Close"));
    status->SetText(job->ok ? Tr("Report saved.") : Tr("Comparison failed or cancelled. No report published."));
    if (job->ok) OpenPathInDefaultFileManager(job->report);
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

#if IS_DEBUG
#include "base/tests/UtAssert.h"

void ComparePdfUnitTests() {
    str::Builder diff;
    AppendWordDiff(diff, StrL("one old middle bad end"), StrL("one new middle good end"));
    utassert(str::Contains(ToStr(diff), StrL("</ins> middle")));
    utassert(!str::Contains(ToStr(diff), StrL("old middle bad")));
    TempStr report = GetTempFilePathTemp(StrL("compare-failure"));
    utassert(file::WriteFile(report, StrL("keep")));
    Str result;
    utassert(!ComparePdfReport(StrL("missing-left.pdf"), StrL("missing-right.pdf"), report, &result));
    utassert(str::Contains(result, StrL("\"ok\":false")));
    Str data = file::ReadFile(report);
    utassert(str::Eq(data, StrL("keep")));
    str::Free(data);
    file::Delete(report);
    Vec<PageFingerprint> a, b;
    VecResize(a, 3);
    VecResize(b, 4);
    for (int i = 0; i < 3; i++) a[i].digest[0] = (u8)(i + 1);
    b[0].digest[0] = 9;
    b[1] = a[2];
    b[2] = a[0];
    b[3] = a[1];
    Vec<int> pairs, used;
    AlignPages(a, b, pairs, used);
    utassert(pairs[0] == 2 && pairs[1] == 3 && pairs[2] == 1 && !used[0]);
    str::Builder large, bounded;
    for (int i = 0; i < 2100; i++) large.Append(StrL("word "));
    utassert(AppendWordDiff(bounded, ToStr(large), ToStr(large)) == -1);
}
#endif
