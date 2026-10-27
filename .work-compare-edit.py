from pathlib import Path
p=Path('src/ComparePdf.cpp')
s=p.read_text()
s=s.replace('static TempStr PageText(fz_context* ctx, fz_document* doc, int pageNo)', 'static bool PageText(fz_context* ctx, fz_document* doc, int pageNo, Str* result)')
s=s.replace('    TempStr result;\n    fz_stext_options opts{};', '    bool ok = false;\n    fz_var(buf);\n    fz_var(text);\n    fz_var(page);\n    fz_var(ok);\n    fz_stext_options opts{};')
s=s.replace('        result = str::DupTemp(Str((char*)data, (int)size));', '        if (size > 16 * 1024 * 1024) fz_throw(ctx, FZ_ERROR_LIMIT, "Page text exceeds comparison limit");\n        *result = str::DupTemp(Str((char*)data, (int)size));\n        ok = true;')
s=s.replace('    return result;\n}\n\nstatic int CountWords', '    return ok;\n}\n\nstatic int CountWords')
s=s.replace('    fz_try(ctx) {\n        pa =', '    fz_var(pa);\n    fz_var(pb);\n    fz_var(xa);\n    fz_var(xb);\n    fz_var(ok);\n    fz_try(ctx) {\n        pa =')
s=s.replace('        xa = fz_new_pixmap_from_page(ctx, pa, fz_scale(0.25f, 0.25f), fz_device_rgb(ctx), 0);\n        xb = fz_new_pixmap_from_page(ctx, pb, fz_scale(0.25f, 0.25f), fz_device_rgb(ctx), 0);', '''        fz_rect ra = fz_bound_page(ctx, pa), rb = fz_bound_page(ctx, pb);
        float side = std::max(std::max(ra.x1 - ra.x0, ra.y1 - ra.y0),
                              std::max(rb.x1 - rb.x0, rb.y1 - rb.y0));
        if (!isfinite(side) || side <= 0) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page dimensions");
        float zoom = std::min(0.25f, 1024.0f / side);
        xa = fz_new_pixmap_from_page(ctx, pa, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
        xb = fz_new_pixmap_from_page(ctx, pb, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);''')
start=s.index('bool ComparePdfReport(')
end=s.index('\nstruct ComparePdfDialog', start)
old=s[start:end]
header=old[old.index('    html.Append(StrL("<!doctype'):old.index('    int wordChanges')]
page=old[old.index('        html.Append(\n            fmt("<section'):old.index('    if (leftPages != rightPages)')]
page=page[:page.rfind('    }')]
new='''static bool WriteReportPart(fz_context* ctx, FileHandle file, fz_document_writer* writer, Str html,
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
    fz_catch(ctx) { fz_report_error(ctx); }
    return ok;
}

bool ComparePdfReport(Str leftPath, Str rightPath, Str reportPath, Str* resultJsonOut, PdfWorkProgress* progress) {
    if (resultJsonOut) *resultJsonOut = StrL("{\\"ok\\":false}");
    if (progress) {
        AtomicIntSet(&progress->completed, 0);
        AtomicIntSet(&progress->total, 0);
        if (progress->IsCancelled()) return false;
    }
    if (path::IsSame(leftPath, reportPath) || path::IsSame(rightPath, reportPath)) return false;
    file::AtomicFile output(reportPath);
    if (!len(output.tempPath)) return false;
    constexpr size_t kCompareCacheBytes = 32 * 1024 * 1024;
    fz_context* ctx = fz_new_context(nullptr, nullptr, kCompareCacheBytes);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    fz_document* left = nullptr;
    fz_document* right = nullptr;
    fz_document_writer* writer = nullptr;
    FileHandle file = kInvalidFileHandle;
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
        if (str::EndsWithI(reportPath, StrL(".pdf")))
            writer = fz_new_pdf_writer(ctx, CStrTemp(output.tempPath), "compress");
        ok = true;
    }
    fz_catch(ctx) { fz_report_error(ctx); }
    if (ok && !writer) {
        file = file::OpenReadWrite(output.tempPath, true);
        ok = file != kInvalidFileHandle;
    }
    str::Builder html;
'''+header+'''    if (ok) ok = WriteReportPart(ctx, file, writer, ToStr(html), progress);
    int wordChanges = 0;
    double pixelChanges = 0;
    int compared = std::min(leftPages, rightPages);
    if (progress) AtomicIntSet(&progress->total, compared);
    for (int i = 0; ok && i < compared; i++) {
        AutoArenaSavepoint scratch;
        if (progress && progress->IsCancelled()) { ok = false; break; }
        Str a, b;
        ok = PageText(ctx, left, i, &a) && PageText(ctx, right, i, &b);
        if (!ok) break;
        int aw = CountWords(a), bw = CountWords(b);
        str::Builder diff;
        int wc = AppendWordDiff(diff, a, b);
        wordChanges += wc;
        double pixelRatio = 0;
        Str leftPng, rightPng;
        ok = PixelChange(ctx, left, right, i, &pixelRatio, &leftPng, &rightPng);
        if (!ok) break;
        pixelChanges += pixelRatio;
        html.Reset();
'''+page+'''        ok = WriteReportPart(ctx, file, writer, ToStr(html), progress);
        if (ok && progress) AtomicIntSet(&progress->completed, i + 1);
    }
    if (leftPages != rightPages) wordChanges += abs(leftPages - rightPages);
    html.Reset();
    html.Append(fmt("<h2>Summary</h2><p>%d pages vs %d pages; %d unpaired pages</p>",
                    leftPages, rightPages, abs(leftPages - rightPages)));
    if (ok) ok = WriteReportPart(ctx, file, writer, ToStr(html), progress);
    // Navigation is streamed too; neither HTML nor PDF retains all page images.
    for (int i = 0; ok && !writer && i < compared; i++) {
        AutoArenaSavepoint scratch;
        if (progress && progress->IsCancelled()) { ok = false; break; }
        ok = file::WriteAll(file, fmt("<a href=\\"#page-%d\\">Page %d</a> ", i + 1, i + 1));
    }
    if (writer && ok) {
        fz_try(ctx) { fz_close_document_writer(ctx, writer); }
        fz_catch(ctx) { fz_report_error(ctx); ok = false; }
    }
    fz_drop_document_writer(ctx, writer);
    if (file != kInvalidFileHandle) {
        if (ok) ok = file::Flush(file);
        file::Close(file);
    }
    if (progress && progress->IsCancelled()) ok = false;
    if (ok) ok = output.Commit();
    if (ok && resultJsonOut) {
        *resultJsonOut = fmt("{\\"ok\\":true,\\"pages\\":%d,\\"wordChanges\\":%d,\\"pixelChangePercent\\":%.2f,\\"report\\":\\"%s\\"}",
                             compared, wordChanges, compared ? pixelChanges * 100.0 / compared : 0.0,
                             json::EscapeStrTemp(reportPath));
    }
    fz_drop_document(ctx, left);
    fz_drop_document(ctx, right);
    fz_drop_context(ctx);
    return ok;
}

struct CompareJob {
    AtomicInt refs = 1;
    AtomicInt done = 0;
    PdfWorkProgress progress;
    Str left, right, report;
    bool ok = false;
    ~CompareJob() { str::Free(left); str::Free(right); str::Free(report); }
    void Release() { if (AtomicIntDec(&refs) == 0) delete this; }
};

static void RunCompare(CompareJob* job) {
    job->ok = ComparePdfReport(job->left, job->right, job->report, nullptr, &job->progress);
    AtomicIntSet(&job->done, 1);
    job->Release();
}
'''
s=s[:start]+new+s[end:]
s=s.replace('    bool Create(MainWindow* win);', '''    CompareJob* job = nullptr;
    ~ComparePdfDialog() override {
        if (job) { AtomicIntSet(&job->progress.cancelled, 1); job->Release(); }
    }
    void Poll(WindowBase::TimerEvent*);
    bool Create(MainWindow* win);''')
s=s.replace('    void OnClose(VirtMouseEvent*) { Close(); }\n    void Closed(WindowBase::CloseEvent*) { Close(); }', '''    void OnClose(VirtMouseEvent*) {
        if (job && !AtomicIntGet(&job->done)) {
            AtomicIntSet(&job->progress.cancelled, 1);
            status->SetText(Tr("Cancelling..."));
        } else Close();
    }
    void Closed(WindowBase::CloseEvent*) { ScheduleDelete(); }''')
a=s.index('    status->SetText(Tr("Comparing documents..."));')
b=s.index('\nbool ComparePdfDialog::Create',a)
s=s[:a]+'''    if (job && !AtomicIntGet(&job->done)) return;
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
    if (!thread) { AtomicIntSet(&job->done, 1); job->Release(); }
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
        status->SetText(job->progress.IsCancelled() ? Tr("Cancelled. No report written.") :
                        Tr("Comparison failed. No report written."));
    }
}
''' + s[b:]
s=s.replace('    win = w;\n    closeOnEsc', '    win = w;\n    onTimer = MkMethod1<ComparePdfDialog, WindowBase::TimerEvent*, &ComparePdfDialog::Poll>(this);\n    closeOnEsc')
p.write_text(s)
p=Path('src/ComparePdf.h');s=p.read_text().replace('struct MainWindow;', 'struct MainWindow;\nstruct PdfWorkProgress;').replace('Str* resultJsonOut = nullptr);','Str* resultJsonOut = nullptr,\n                      PdfWorkProgress* progress = nullptr);');p.write_text(s)
p=Path('src/shared/EngineAll.h');s=p.read_text();pos=s.index('bool EngineMupdfReplaceAllText');s=s[:pos]+'''struct PdfWorkProgress {
    AtomicInt cancelled = 0;
    AtomicInt completed = 0;
    AtomicInt total = 0;
    bool IsCancelled() { return AtomicIntGet(&cancelled) != 0; }
};

'''+s[pos:];p.write_text(s)
