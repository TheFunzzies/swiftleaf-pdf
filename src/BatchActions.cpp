/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/DirScan.h"
#include "base/JsonParser.h"
#include "base/Win.h"
#include <commdlg.h>
#include <shlobj.h>
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
#include "Ocr.h"
#include "shared/PdfTools.h"
#include <mupdf/pdf.h>
#include "PageTools2.h"
#include "ExportFormats.h"
#include "BatchActions.h"

extern "C" int pdfclean_main(int argc, char** argv);

struct BatchAction {
    Str type;
    Str text;
    Str pages;
    Str title;
    Str author;
    Str subject;
    Str keywords;
    float value = 0;
};

struct BatchDefinition {
    Vec<BatchAction> actions;
    Str suffix = StrL("_batch");
    Str outputFolder;
};

static bool BatchNameValid(Str name) {
    if (len(name) == 0 || len(name) > 64) return false;
    for (int i = 0; i < len(name); i++) {
        char c = name.s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    return true;
}

static TempStr BatchNamedPath(Str name) {
    if (!BatchNameValid(name)) return {};
    TempStr dir = path::JoinTemp(GetAppDataDirTemp(), StrL("batch-actions"));
    if (!dir::CreateAll(dir)) return {};
    return path::JoinTemp(dir, fmt("%s.json", name));
}

static void BatchIgnoreJsonValue(json::Value*) {}

bool SaveBatchActionsJson(Str name, Str definition) {
    if (!BatchNameValid(name) || !json::Parse(definition, MkFunc1Void(BatchIgnoreJsonValue))) return false;
    TempStr path = BatchNamedPath(name);
    if (len(path) == 0) return false;
    file::AtomicFile output(path);
    return len(output.tempPath) > 0 && file::WriteFile(output.tempPath, definition) && output.Commit();
}

TempStr LoadBatchActionsJsonTemp(Str name) {
    TempStr path = BatchNamedPath(name);
    if (len(path) == 0) return {};
    Str data = file::ReadFile(path);
    TempStr result = str::DupTemp(data);
    str::Free(data);
    return result;
}

static void ReadActionValue(BatchDefinition* batch, json::Value* v) {
    StrNode* a = v->path;
    if (!a) return;
    Str rootKey = json::PathSegKey(a);
    if (!a->next) {
        if (str::Eq(rootKey, StrL("suffix"))) batch->suffix = str::Dup(GetPermArena(), v->value);
        if (str::Eq(rootKey, StrL("outputFolder"))) batch->outputFolder = str::Dup(GetPermArena(), v->value);
        return;
    }
    if (!str::Eq(rootKey, StrL("actions")) || !a->next || json::PathSegIndex(a->next) < 0) return;
    int idx = json::PathSegIndex(a->next);
    if (idx >= len(batch->actions)) VecResize(batch->actions, idx + 1);
    BatchAction& action = batch->actions[idx];
    Str key = json::PathSegKey(a->next->next);
    Str value = str::Dup(GetPermArena(), v->value);
    if (str::Eq(key, StrL("type")))
        action.type = value;
    else if (str::Eq(key, StrL("text")))
        action.text = value;
    else if (str::Eq(key, StrL("pages")))
        action.pages = value;
    else if (str::Eq(key, StrL("title")))
        action.title = value;
    else if (str::Eq(key, StrL("author")))
        action.author = value;
    else if (str::Eq(key, StrL("subject")))
        action.subject = value;
    else if (str::Eq(key, StrL("keywords")))
        action.keywords = value;
    else if (str::Eq(key, StrL("value")))
        action.value = (float)atof(CStrTemp(value));
}

static bool ApplyBatchAction(Str src, Str dst, const BatchAction& action, PdfOptimizationAudit* auditOut) {
    if (str::Eq(action.type, StrL("ocr"))) return BatchOcrPdf(src, dst);
    if (str::Eq(action.type, StrL("compress")) || str::Eq(action.type, StrL("optimize"))) {
        PdfOptimizerPreset preset = PdfOptimizerPreset::Web;
        if (str::EqI(action.text, StrL("print"))) preset = PdfOptimizerPreset::Print;
        if (str::EqI(action.text, StrL("archive"))) preset = PdfOptimizerPreset::Archive;
        if (str::EqI(action.text, StrL("min-size"))) preset = PdfOptimizerPreset::MinSize;
        return PdfOptimizeFile(src, dst, preset, auditOut);
    }
    if (str::Eq(action.type, StrL("watermark"))) {
        PdfWatermarkParams p{};
        p.text = action.text;
        p.pagesSpec = action.pages;
        return PdfApplyWatermark(src, dst, p);
    }
    if (str::Eq(action.type, StrL("header-footer"))) {
        PdfHeaderFooterParams p{};
        p.headerCenter = action.text;
        p.pagesSpec = action.pages;
        return PdfApplyHeaderFooter(src, dst, p);
    }
    if (str::Eq(action.type, StrL("bates"))) {
        PdfBatesParams p{};
        p.prefix = action.text;
        p.pagesSpec = action.pages;
        return PdfApplyBatesNumbering(src, dst, p);
    }
    if (str::Eq(action.type, StrL("crop"))) {
        PdfPageBoxesParams p{};
        p.pagesSpec = action.pages;
        p.marginLeft = p.marginBottom = p.marginRight = p.marginTop = action.value;
        return PdfApplyPageBoxes(src, dst, p);
    }
    if (str::Eq(action.type, StrL("rotate"))) {
        fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
        if (!ctx) return false;
        fz_register_document_handlers(ctx);
        fz_document* doc = nullptr;
        int nPages = 0;
        fz_try(ctx) {
            doc = fz_open_document(ctx, CStrTemp(src));
            nPages = fz_count_pages(ctx, doc);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        if (nPages <= 0) return false;
        Vec<PdfMergeSource> sources;
        VecAppend(sources, PdfMergeSource{src, {}});
        Vec<PdfMergePage> pages;
        int degrees = (int)action.value;
        for (int i = 1; i <= nPages; i++) VecAppend(pages, PdfMergePage{0, i, degrees});
        return EngineMupdfMergePdfs(sources, pages, dst);
    }
    if (str::Eq(action.type, StrL("flatten")) || str::Eq(action.type, StrL("flatten-layers"))) return PdfFlattenLayers(src, dst);
    if (str::Eq(action.type, StrL("sanitize"))) {
        PdfSanitizeOptions opts{};
        Str error;
        bool ok = EngineMupdfSanitizePdf({src, {}}, dst, opts, &error);
        str::Free(error);
        return ok;
    }
    if (str::Eq(action.type, StrL("redact-matches"))) {
        int count = 0;
        Str error;
        bool ok = EngineMupdfRedactMatches({src, {}}, dst, action.text, PdfSearchMode::Text, &count, &error);
        str::Free(error);
        return ok;
    }
    if (str::Eq(action.type, StrL("encrypt"))) {
        char* pwdZ = CStrTemp(action.text);
        file::AtomicFile output(dst);
        if (len(output.tempPath) == 0) return false;
        char* argv[] = {(char*)"clean", (char*)"-E", (char*)"aes-256", (char*)"-U", pwdZ, (char*)"-O", pwdZ,
                        CStrTemp(src), CStrTemp(output.tempPath)};
        fz_set_optind(0);
        int res = pdfclean_main(9, argv);
        if (res == 0 && !output.Commit()) res = 1;
        return res == 0;
    }
    if (str::Eq(action.type, StrL("properties")) || str::Eq(action.type, StrL("set-properties"))) {
        PdfDocPropsParams p{};
        p.title = action.title;
        p.author = action.author;
        p.subject = action.subject;
        p.keywords = action.keywords;
        return PdfSetDocProps(src, dst, p);
    }
    if (str::Eq(action.type, StrL("export-text"))) {
        ConversionProgress progress{};
        return ExportPdf(src, dst, ExportFormat::Text, action.pages, progress);
    }
    if (str::Eq(action.type, StrL("export-images")) || str::Eq(action.type, StrL("export-image"))) {
        fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
        if (!ctx) return false;
        fz_register_document_handlers(ctx);
        fz_document* doc = nullptr;
        bool ok = true;
        fz_try(ctx) {
            doc = fz_open_document(ctx, CStrTemp(src));
            int nPages = fz_count_pages(ctx, doc);
            if (nPages <= 0) {
                ok = false;
            } else {
                for (int i = 0; i < nPages; i++) {
                    fz_page* page = fz_load_page(ctx, doc, i);
                    fz_pixmap* pix = fz_new_pixmap_from_page(ctx, page, fz_scale(2.0f, 2.0f), fz_device_rgb(ctx), 0);
                    TempStr outImg = (nPages == 1) ? str::DupTemp(dst) : fmt("%s_%d.png", path::GetPathNoExtTemp(dst), i + 1);
                    fz_save_pixmap_as_png(ctx, pix, CStrTemp(outImg));
                    fz_drop_pixmap(ctx, pix);
                    fz_drop_page(ctx, page);
                }
            }
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            ok = false;
        }
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return ok;
    }
    if (str::Eq(action.type, StrL("export-html")) || str::Eq(action.type, StrL("export-markdown")) ||
        str::Eq(action.type, StrL("export-json")) || str::Eq(action.type, StrL("export-svg"))) {
        ExportFormat format = ExportFormat::Html;
        if (str::Eq(action.type, StrL("export-markdown"))) format = ExportFormat::Markdown;
        if (str::Eq(action.type, StrL("export-json"))) format = ExportFormat::Json;
        if (str::Eq(action.type, StrL("export-svg"))) format = ExportFormat::Svg;
        ConversionProgress progress{};
        return ExportPdf(src, dst, format, action.pages, progress);
    }
    return false;
}

static Str BatchExportExt(Str type) {
    if (str::Eq(type, StrL("export-text"))) return StrL(".txt");
    if (str::Eq(type, StrL("export-html"))) return StrL(".html");
    if (str::Eq(type, StrL("export-markdown"))) return StrL(".md");
    if (str::Eq(type, StrL("export-json"))) return StrL(".json");
    if (str::Eq(type, StrL("export-svg"))) return StrL(".svg");
    if (str::Eq(type, StrL("export-images")) || str::Eq(type, StrL("export-image"))) return StrL(".png");
    return {};
}

static TempStr BatchOutputPath(Str input, Str suffix, Str folder, Str outputExt = {}) {
    TempStr base = path::GetBaseNameTemp(input);
    TempStr stem = path::GetPathNoExtTemp(base);
    TempStr ext = len(outputExt) > 0 ? str::DupTemp(outputExt) : path::GetExtTemp(input);
    TempStr name = fmt("%s%s%s", stem, suffix, ext);
    if (len(folder) > 0) return path::JoinTemp(folder, name);
    return path::JoinTemp(path::GetDirTemp(input), name);
}

TempStr RunBatchActionsJson(Str definition, const Vec<Str>& files, BatchProgress* progress) {
    BatchDefinition batch;
    if (!json::Parse(definition, MkFunc1(ReadActionValue, &batch))) {
        return str::DupTemp(StrL("{\"error\":\"invalid action JSON\",\"results\":[]}"));
    }
    if (len(batch.outputFolder) > 0 && !dir::CreateAll(batch.outputFolder)) {
        return str::DupTemp(StrL("{\"error\":\"cannot create output folder\",\"results\":[]}"));
    }
    str::Builder out;
    bool allOk = true;
    if (progress) {
        InterlockedExchange(&progress->completedFiles, 0);
        InterlockedExchange(&progress->totalFiles, len(files));
        InterlockedExchange(&progress->totalActions, 0);
    }
    out.Append(StrL("{\"ok\":"));
    out.Append(StrL("true,\"total\":"));
    out.Append(fmt("%d,\"results\":[", len(files)));
    for (int i = 0; i < len(files); i++) {
        if (i) out.AppendChar(',');
        Str current = files[i];
        bool ok = true;
        Str error;
        PdfOptimizationAudit audit{};
        if (progress) {
            InterlockedExchange(&progress->currentFile, i + 1);
            InterlockedExchange(&progress->totalActions, len(batch.actions));
        }
        for (int j = 0; j < len(batch.actions); j++) {
            if (progress && InterlockedCompareExchange(&progress->cancelled, 0, 0)) {
                ok = false;
                error = StrL("cancelled");
                break;
            }
            if (progress) InterlockedExchange(&progress->currentAction, j + 1);
            const BatchAction& action = batch.actions[j];
            bool last = j == len(batch.actions) - 1;
            Str outputExt = last ? BatchExportExt(action.type) : Str();
            TempStr dest;
            if (last) {
                dest = BatchOutputPath(files[i], batch.suffix, batch.outputFolder, outputExt);
            } else {
                TempStr suffix = fmt("%s_%d_%d", batch.suffix, i + 1, j + 1);
                dest = BatchOutputPath(files[i], suffix, batch.outputFolder);
            }
            TempStr staged =
                last ? BatchOutputPath(files[i], fmt("%s_partial", batch.suffix), batch.outputFolder, outputExt) : dest;
            ok = ApplyBatchAction(current, staged, action, &audit);
            if (ok && last) ok = file::RenameReplace(dest, staged);
            if (!ok) {
                error = action.type;
                file::Delete(staged);
                if (j > 0 && !str::Eq(current, files[i])) file::Delete(current);
                break;
            }
            if (j > 0 && !str::Eq(current, files[i])) file::Delete(current);
            current = str::DupTemp(dest);
        }
        if (len(batch.actions) == 0) {
            ok = false;
            error = StrL("no actions configured");
        }
        allOk = allOk && ok;
        if (progress) InterlockedExchange(&progress->completedFiles, i + 1);
        out.Append(StrL("{\"input\":\""));
        out.Append(json::EscapeStrTemp(files[i]));
        out.Append(StrL("\",\"ok\":"));
        out.Append(ok ? StrL("true") : StrL("false"));
        if (ok) {
            out.Append(StrL(",\"output\":\""));
            out.Append(json::EscapeStrTemp(current));
            out.AppendChar('"');
            if (audit.beforeBytes > 0) {
                out.Append(fmt(",\"beforeBytes\":%lld,\"afterBytes\":%lld,\"imageBytes\":%lld,\"fontBytes\":%lld,\"objectBytes\":%lld,\"keptOriginal\":%s",
                    audit.beforeBytes, audit.afterBytes, audit.imageBytes, audit.fontBytes, audit.objectBytes,
                    audit.keptOriginal ? StrL("true") : StrL("false")));
            }
        } else {
            out.Append(StrL(",\"error\":\"action failed: "));
            out.Append(json::EscapeStrTemp(error));
            out.AppendChar('"');
        }
        out.AppendChar('}');
    }
    out.Append(StrL("]}"));
    if (!allOk) {
        Str json = ToStrTemp(out);
        return str::ReplaceTemp(json, StrL("\"ok\":true"), StrL("\"ok\":false"));
    }
    return ToStrTemp(out);
}

struct BatchWizardJob {
    Str definition;
    Vec<Str> files;
    BatchProgress progress;
    Str resultJson;
    volatile LONG done = 0;
};

static void RunBatchWizardThread(BatchWizardJob* job) {
    TempStr res = RunBatchActionsJson(job->definition, job->files, &job->progress);
    job->resultJson = str::Dup(res);
    InterlockedExchange(&job->done, 1);
}

struct BatchActionsDialog : WindowBase {
    MainWindow* win = nullptr;
    VirtFixedLinesText* status = nullptr;
    VirtText* fileCountText = nullptr;
    VirtButton* btnAddFiles = nullptr;
    VirtButton* btnAddFolder = nullptr;
    VirtButton* btnSetFolder = nullptr;
    VirtButton* btnStart = nullptr;
    VirtButton* btnCancel = nullptr;
    DropDown* actionPreset = nullptr;
    Edit* suffixEdit = nullptr;
    Vec<Str> inputFiles;
    Str outputFolder;
    BatchWizardJob* job = nullptr;
    bool running = false;

    ~BatchActionsDialog() override {
        if (job) {
            InterlockedExchange(&job->progress.cancelled, 1);
            str::Free(job->definition);
            str::Free(job->resultJson);
            delete job;
        }
        str::Free(outputFolder);
    }

    bool Create(MainWindow* win);
    void OnAddFiles(VirtMouseEvent*);
    void OnAddFolder(VirtMouseEvent*);
    void OnSetFolder(VirtMouseEvent*);
    void OnStart(VirtMouseEvent*);
    void OnCancel(VirtMouseEvent*);
    void Poll(WindowBase::TimerEvent*);
    void Closed(WindowBase::CloseEvent*) { Close(); }
};

void BatchActionsDialog::OnAddFiles(VirtMouseEvent*) {
    WCHAR buf[65536]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = dimof(buf);
    ofn.lpstrFilter = L"PDF Files\0*.pdf\0All Files\0*.*\0";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    const WCHAR* p = buf;
    TempStr dir = ToUtf8Temp(WStr(p));
    p += wcslen(p) + 1;
    if (!*p) {
        VecAppend(inputFiles, str::Dup(dir));
    } else {
        while (*p) {
            TempStr full = path::JoinTemp(dir, ToUtf8Temp(WStr(p)));
            VecAppend(inputFiles, str::Dup(full));
            p += wcslen(p) + 1;
        }
    }
    fileCountText->SetText(fmt(Tr("%d PDF files selected").s, len(inputFiles)));
    btnStart->SetIsEnabled(len(inputFiles) > 0);
}

void BatchActionsDialog::OnAddFolder(VirtMouseEvent*) {
    WCHAR pathBuf[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select Folder with PDF Files";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    if (SHGetPathFromIDListW(pidl, pathBuf)) {
        TempStr dir = ToUtf8Temp(WStr(pathBuf));
        DirIter it{dir};
        it.includeDirs = false;
        it.includeFiles = true;
        it.recurse = true;
        for (DirIterEntry* e : it) {
            if (str::EqI(path::GetExtTemp(e->name), StrL(".pdf"))) {
                VecAppend(inputFiles, str::Dup(e->filePath));
            }
        }
        fileCountText->SetText(fmt(Tr("%d PDF files selected").s, len(inputFiles)));
        btnStart->SetIsEnabled(len(inputFiles) > 0);
    }
    CoTaskMemFree(pidl);
}

void BatchActionsDialog::OnSetFolder(VirtMouseEvent*) {
    WCHAR pathBuf[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select Output Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    if (SHGetPathFromIDListW(pidl, pathBuf)) {
        str::ReplaceWithCopy(&outputFolder, ToUtf8Temp(WStr(pathBuf)));
        status->SetText(fmt(Tr("Output folder: %s").s, outputFolder));
    }
    CoTaskMemFree(pidl);
}

void BatchActionsDialog::OnStart(VirtMouseEvent*) {
    if (running || len(inputFiles) == 0) return;
    int sel = CbGetCurrentSelection(actionPreset);
    Str def;
    switch (sel) {
        case 0: def = StrL("{\"actions\":[{\"type\":\"ocr\"}]}"); break;
        case 1: def = StrL("{\"actions\":[{\"type\":\"optimize\",\"text\":\"web\"}]}"); break;
        case 2: def = StrL("{\"actions\":[{\"type\":\"optimize\",\"text\":\"print\"}]}"); break;
        case 3: def = StrL("{\"actions\":[{\"type\":\"optimize\",\"text\":\"min-size\"}]}"); break;
        case 4: def = StrL("{\"actions\":[{\"type\":\"sanitize\"}]}"); break;
        case 5: def = StrL("{\"actions\":[{\"type\":\"flatten\"}]}"); break;
        case 6: def = StrL("{\"actions\":[{\"type\":\"watermark\",\"text\":\"CONFIDENTIAL\"}]}"); break;
        default: def = StrL("{\"actions\":[{\"type\":\"optimize\",\"text\":\"web\"}]}"); break;
    }
    TempStr sfx = suffixEdit ? suffixEdit->GetTextTemp() : StrL("_batch");
    if (len(sfx) == 0) sfx = StrL("_batch");

    str::Builder b;
    b.Append(StrL("{\"suffix\":\""));
    b.Append(json::EscapeStrTemp(sfx));
    b.Append(StrL("\""));
    if (len(outputFolder) > 0) {
        b.Append(StrL(",\"outputFolder\":\""));
        b.Append(json::EscapeStrTemp(outputFolder));
        b.AppendChar('"');
    }
    b.Append(StrL(",\"actions\":"));
    b.Append(Str(def.s + 11)); // skip {"actions":

    if (job) {
        str::Free(job->definition);
        str::Free(job->resultJson);
        delete job;
    }
    job = new BatchWizardJob();
    job->definition = str::Dup(ToStr(b));
    job->files = inputFiles;

    running = true;
    btnAddFiles->SetIsEnabled(false);
    btnAddFolder->SetIsEnabled(false);
    btnSetFolder->SetIsEnabled(false);
    actionPreset->SetIsEnabled(false);
    btnStart->SetIsEnabled(false);
    btnCancel->SetText(Tr("Cancel"));
    status->SetText(Tr("Starting batch execution..."));

    ThreadHandle thread = StartThread(MkFunc0(RunBatchWizardThread, job), StrL("Batch Actions"));
    if (!thread) {
        running = false;
        return;
    }
    SafeCloseThreadHandle(&thread);
    SetTimer(hwnd, 1, 100, nullptr);
}

void BatchActionsDialog::OnCancel(VirtMouseEvent*) {
    if (running && job) {
        InterlockedExchange(&job->progress.cancelled, 1);
        status->SetText(Tr("Cancelling..."));
        return;
    }
    Close();
}

void BatchActionsDialog::Poll(WindowBase::TimerEvent*) {
    if (!job) return;
    if (!InterlockedCompareExchange(&job->done, 0, 0)) {
        int f = InterlockedCompareExchange(&job->progress.currentFile, 0, 0);
        int totF = InterlockedCompareExchange(&job->progress.totalFiles, 0, 0);
        int a = InterlockedCompareExchange(&job->progress.currentAction, 0, 0);
        int totA = InterlockedCompareExchange(&job->progress.totalActions, 0, 0);
        if (totF > 0) {
            status->SetText(fmt(Tr("Processing file %d of %d (action %d of %d)...").s, f, totF, a, totA));
        }
        return;
    }
    KillTimer(hwnd, 1);
    running = false;
    btnAddFiles->SetIsEnabled(true);
    btnAddFolder->SetIsEnabled(true);
    btnSetFolder->SetIsEnabled(true);
    actionPreset->SetIsEnabled(true);
    btnStart->SetIsEnabled(len(inputFiles) > 0);
    btnCancel->SetText(Tr("Close"));
    if (InterlockedCompareExchange(&job->progress.cancelled, 0, 0)) {
        status->SetText(Tr("Batch actions cancelled."));
    } else {
        int comp = InterlockedCompareExchange(&job->progress.completedFiles, 0, 0);
        status->SetText(fmt(Tr("Batch actions completed (%d files processed).").s, comp));
    }
}

bool BatchActionsDialog::Create(MainWindow* w) {
    win = w;
    closeOnEsc = true;
    onClose = MkMethod1<BatchActionsDialog, WindowBase::CloseEvent*, &BatchActionsDialog::Closed>(this);
    onTimer = MkMethod1<BatchActionsDialog, WindowBase::TimerEvent*, &BatchActionsDialog::Poll>(this);

    CreateCustomArgs args;
    args.owner = win ? win->hwndFrame : nullptr;
    args.title = Tr("Action Wizard");
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
    status->SetText(Tr("Run automated batch actions over multiple PDF files."));
    box->AddChild(status);

    fileCountText = NewVirtText({.s = Tr("No PDF files selected"), .font = font});
    box->AddChild(fileCountText);

    auto* hFiles = new HBox();
    hFiles->gap = DpiScale(8);
    btnAddFiles = NewThemedButton(hwnd, Tr("Add Files..."), font, false);
    btnAddFiles->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnAddFiles>(this);
    hFiles->AddChild(btnAddFiles);

    btnAddFolder = NewThemedButton(hwnd, Tr("Add Folder..."), font, false);
    btnAddFolder->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnAddFolder>(this);
    hFiles->AddChild(btnAddFolder);

    btnSetFolder = NewThemedButton(hwnd, Tr("Output Folder..."), font, false);
    btnSetFolder->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnSetFolder>(this);
    hFiles->AddChild(btnSetFolder);
    box->AddChild(hFiles);

    box->AddChild(NewVirtText({.s = Tr("Action Preset:"), .font = font}));
    actionPreset = new DropDown();
    actionPreset->Create({.parent = hwnd, .font = font});
    StrVec presets;
    presets.Append(Tr("OCR Searchable"));
    presets.Append(Tr("Optimize for Web"));
    presets.Append(Tr("Optimize for Print"));
    presets.Append(Tr("Optimize for Min Size"));
    presets.Append(Tr("Sanitize Document"));
    presets.Append(Tr("Flatten Layers"));
    presets.Append(Tr("Watermark (CONFIDENTIAL)"));
    actionPreset->SetItems(presets);
    CbSetCurrentSelection(actionPreset, 1);
    box->AddChild(actionPreset);

    auto* hSuffix = new HBox();
    hSuffix->gap = DpiScale(8);
    hSuffix->AddChild(NewVirtText({.s = Tr("File suffix:"), .font = font}));
    Edit::CreateArgs sargs;
    sargs.parent = hwnd;
    sargs.withBorder = true;
    sargs.text = StrL("_batch");
    sargs.font = font;
    suffixEdit = new Edit();
    suffixEdit->Create(sargs);
    hSuffix->AddChild(suffixEdit, 1);
    box->AddChild(hSuffix);

    auto* buttons = new HBox();
    buttons->gap = DpiScale(8);
    btnStart = NewThemedButton(hwnd, Tr("Start Batch"), font, true);
    btnStart->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnStart>(this);
    btnStart->SetIsEnabled(false);
    buttons->AddChild(btnStart);

    btnCancel = NewThemedButton(hwnd, Tr("Close"), font, false);
    btnCancel->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnCancel>(this);
    buttons->AddChild(btnCancel);
    box->AddChild(buttons);

    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(540), DpiScale(340)};
    AdjustWindowRectEx(&rect, args.style, FALSE, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    UpdateTheme();
    HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
    SetIsVisible(true);
    return true;
}

void ShowBatchActionsDialog(MainWindow* win) {
    auto* dlg = new BatchActionsDialog();
    if (!dlg->Create(win)) delete dlg;
}
