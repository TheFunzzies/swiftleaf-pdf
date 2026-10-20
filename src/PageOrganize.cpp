/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "SumatraPDF.h"
#include "SumatraConfig.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Translations.h"
#include "Notifications.h"
#include "AppTools.h"
#include "Commands.h"
#include "Toolbar.h"
#include "PageOrganize.h"

// TODO(B2): temporary stand-in until review/b2-edit-text lands; it defines
// CommitOpenEditors in EditText.cpp. Delete this when rebasing onto it.
bool CommitOpenEditors(MainWindow*) {
    return true;
}

// OK / Cancel warning, Cancel by default
bool ConfirmWarning(HWND hwnd, Str msg, Str title) {
    uint flags = MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2 | MbRtlReadingMaybe();
    return MessageBoxW(hwnd, CWStrTemp(ToWStrTemp(msg)), CWStrTemp(ToWStrTemp(title)), flags) == IDOK;
}

PdfLiveSnapshot::~PdfLiveSnapshot() {
    file::Delete(path);
    str::Free(path);
}

// Saves the live document (unsaved edits and applied redactions included) to
// a temp file, so exports never read the stale file on disk.
bool PdfLiveSnapshot::Create(EngineBase* engine) {
    if (!engine || len(path) != 0) {
        return false;
    }
    TempStr tmp = GetTempFilePathTemp(StrL("sle"));
    if (len(tmp) == 0) {
        return false;
    }
    path = str::Dup(tmp);
    return EngineMupdfSaveCopy(engine, path);
}

// a new empty file in target's directory, so a rename onto target stays on one volume
TempStr PdfTempBeside(Str target) {
    WCHAR name[MAX_PATH]{};
    if (!GetTempFileNameW(CWStrTemp(path::GetDirTemp(target)), L"slf", 0, name)) {
        return {};
    }
    return ToUtf8Temp(WStr(name));
}

static bool FlushFile(Str path) {
    HANDLE h = CreateFileW(CWStrTemp(path), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    return ok;
}

// Puts source's content at target. source is staged beside target (or used
// as is when already there, and then consumed) and swapped in with ReplaceFileW:
// one volume, never a copy over the original, ACLs and streams kept.
// On failure target keeps its old content.
bool PublishPdfFile(Str target, Str source) {
    bool beside = str::EqI(path::GetDirTemp(source), path::GetDirTemp(target));
    TempStr staged = beside ? str::DupTemp(source) : PdfTempBeside(target);
    if (len(staged) == 0) {
        return false;
    }
    if (!beside && !CopyFileW(CWStrTemp(source), CWStrTemp(staged), FALSE)) {
        file::Delete(staged);
        return false;
    }
    i64 size = file::GetSize(staged);
    if (size <= 0 || !FlushFile(staged)) {
        if (!beside) {
            file::Delete(staged);
        }
        return false;
    }

    if (!file::Exists(target)) {
        bool moved = MoveFileExW(CWStrTemp(staged), CWStrTemp(target), MOVEFILE_WRITE_THROUGH) != 0;
        if (!moved && !beside) {
            file::Delete(staged);
        }
        return moved;
    }

    // with a backup name ReplaceFileW never leaves target missing without a copy
    TempStr backup = PdfTempBeside(target);
    if (len(backup) == 0) {
        if (!beside) {
            file::Delete(staged);
        }
        return false;
    }
    file::Delete(backup);
    DWORD flags = REPLACEFILE_IGNORE_MERGE_ERRORS;
    bool ok = ReplaceFileW(CWStrTemp(target), CWStrTemp(staged), CWStrTemp(backup), flags, nullptr, nullptr) != 0;
    if (!ok) {
        LogLastError();
        if (!file::Exists(target) && file::Exists(backup)) {
            MoveFileExW(CWStrTemp(backup), CWStrTemp(target), MOVEFILE_WRITE_THROUGH);
        }
        if (file::Exists(target)) {
            file::Delete(backup);
        }
        if (!beside) {
            file::Delete(staged);
        }
        return false;
    }
    if (file::GetSize(target) != size) {
        logf("PublishPdfFile: '%s' has the wrong size, previous content kept in '%s'\n", target, backup);
        return false;
    }
    file::Delete(backup);
    return true;
}

// one step Undo Page Change can take back: the file as it was before the change
struct OrganizeUndoStep {
    Str filePath;   // owned, the document
    Str backupPath; // owned, its previous content
    int pageNo = 0;
    i64 size = -1;
    FILETIME modified{};
    bool recovery = false;
};

static Vec<OrganizeUndoStep> gUndoSteps;

// the tab whose PDF can be reorganized: a PDF that is a file on disk
static WindowTab* OrganizableTab(MainWindow* win) {
    if (!win || !win->IsDocLoaded() || gPluginMode || !CanAccessDisk()) {
        return nullptr;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab) || len(tab->filePath) == 0 || !file::Exists(tab->filePath)) {
        return nullptr;
    }
    return tab;
}

static void ShowOrganizeError(MainWindow* win, Str msg) {
    ShowWarningNotification(win->hwndCanvas, msg, kNotifDefaultTimeOut * 2);
}

// Write the document's pages as listed in `pages` (extra PDFs are sources 1..),
// replace the file with the result and reopen it on goToPage.
static bool ApplyPageList(MainWindow* win, const Vec<PdfMergePage>& pages, const Vec<PdfMergeSource>* extra,
                          int goToPage, Str doneMsg, int doneTimeoutMs = kNotif5SecsTimeOut) {
    WindowTab* tab = OrganizableTab(win);
    if (!tab || len(pages) == 0) {
        return false;
    }
    // typed text must be in the snapshot; committing can't change the page count
    if (!CommitOpenEditors(win) || OrganizableTab(win) != tab) {
        return false;
    }
    TempStr path = str::DupTemp(tab->filePath);
    EngineBase* engine = tab->GetEngine();
    if (EngineHasUnsavedAnnotations(engine) &&
        !ConfirmWarning(win->hwndFrame, Tr("This page change saves your unsaved edits to the file. Continue?"),
                        Tr("Page Change"))) {
        return false;
    }
    if (EngineMupdfHasSignatures(engine) &&
        !ConfirmWarning(win->hwndFrame, Tr("Changing pages invalidates the digital signatures of this PDF. Continue?"),
                        Tr("Page Change"))) {
        return false;
    }

    // Reserve a unique snapshot before changing anything. It is both the merge
    // source and the undo state, including annotations not yet saved to disk.
    TempStr backup = GetTempFilePathTemp(StrL("slu"));
    bool hadUnsaved = engine && EngineHasUnsavedAnnotations(engine);
    bool haveBackup = false;
    if (len(backup) > 0) {
        haveBackup = hadUnsaved ? EngineMupdfSaveCopy(engine, backup)
                                : CopyFileW(ToWStrTemp(path).s, ToWStrTemp(backup).s, FALSE) != 0;
    }
    if (!haveBackup) {
        file::Delete(backup);
        ShowOrganizeError(win, Tr("Couldn't create an undo copy. The pages were not changed."));
        return false;
    }

    Vec<PdfMergeSource> srcs;
    VecAppend(srcs, PdfMergeSource{backup, EngineMupdfGetPassword(engine)});
    for (int i = 0; extra && i < len(*extra); i++) {
        const PdfMergeSource& src = (*extra)[i];
        VecAppend(srcs, src);
    }

    TempStr outPath = PdfTempBeside(path);
    bool ok = len(outPath) > 0 && EngineMupdfMergePdfs(srcs, pages, outPath);
    if (!ok) {
        file::Delete(outPath);
        file::Delete(backup);
        ShowOrganizeError(win, Tr("Couldn't change the pages of this PDF"));
        return false;
    }

    int fromPage = win->ctrl ? win->ctrl->CurrentPageNo() : 1;

    bool replaced = ReplaceCurrentDocumentFile(win, outPath, goToPage);
    file::Delete(outPath);
    if (!replaced && !hadUnsaved) {
        // the file is unchanged, so the snapshot holds nothing it doesn't
        file::Delete(backup);
        ShowOrganizeError(win, Tr("Couldn't write the PDF. Is it read-only or open in another program?"));
        return false;
    }

    // after a failed replace the file is unchanged but the reload dropped the
    // unsaved edits: the step brings them back and its file is kept for the user
    OrganizeUndoStep step{str::Dup(path), str::Dup(backup), fromPage};
    step.size = file::GetSize(path);
    step.modified = file::GetModificationTime(path);
    step.recovery = !replaced;
    VecAppend(gUndoSteps, step);
    ToolbarUpdateStateForWindow(win, true);
    if (!IsMainWindowValidAndNotClosing(win)) {
        return replaced;
    }
    if (!replaced) {
        Str msg = fmt(Tr("Couldn't write the PDF. Undo Page Change restores your unsaved edits, also kept in %s").s,
                      backup);
        ShowPlainWarningNotification(win->hwndCanvas, msg, kNotifNoTimeout);
        return false;
    }
    if (len(doneMsg) > 0) {
        NotificationCreateArgs args;
        args.hwndParent = win->hwndCanvas;
        args.msg = str::JoinTemp(doneMsg, StrL(" [Undo](CmdUndoPageChange)"));
        args.timeoutMs = doneTimeoutMs;
        ShowNotification(args);
    }
    return true;
}

// pages 1..n of the base document, in order
static void AllPages(int n, Vec<PdfMergePage>& out) {
    for (int i = 1; i <= n; i++) {
        VecAppend(out, PdfMergePage{0, i, 0});
    }
}

static int CurrentPage(MainWindow* win, int* nPages) {
    *nPages = win->ctrl ? win->ctrl->PageCount() : 0;
    return win->ctrl ? win->ctrl->CurrentPageNo() : 1;
}

void OrganizeRotatePage(MainWindow* win, int degrees) {
    if (!OrganizableTab(win)) {
        return;
    }
    int n = 0;
    int cur = CurrentPage(win, &n);
    Vec<PdfMergePage> pages;
    AllPages(n, pages);
    pages[cur - 1].rotate = degrees;
    ApplyPageList(win, pages, nullptr, cur, Tr("Page rotated"));
}

void OrganizeInsertBlankPage(MainWindow* win) {
    if (!OrganizableTab(win)) {
        return;
    }
    int n = 0;
    int cur = CurrentPage(win, &n);
    Vec<PdfMergePage> pages;
    for (int i = 1; i <= n; i++) {
        VecAppend(pages, PdfMergePage{0, i, 0});
        if (i == cur) {
            VecAppend(pages, PdfMergePage{kPdfMergeBlankPage, cur, 0});
        }
    }
    ApplyPageList(win, pages, nullptr, cur + 1, Tr("Blank page inserted"));
}

static TempStr PickPdfFileTemp(MainWindow* win) {
    WCHAR fileName[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    ofn.lpstrFilter = L"PDF documents\0*.pdf\0All files\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = dimof(fileName);
    Str title = Tr("Insert Pages From File");
    ofn.lpstrTitle = CWStrTemp(ToWStrTemp(title));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    return ToUtf8Temp(WStr(fileName));
}

void OrganizeInsertPagesFromFile(MainWindow* win) {
    if (!OrganizableTab(win)) {
        return;
    }
    TempStr other = PickPdfFileTemp(win);
    if (len(other) == 0) {
        return;
    }
    EngineBase* otherEngine = CreateEngineMupdfFromFile(other, FileType::PDF, 96, nullptr);
    int nOther = otherEngine ? otherEngine->PageCount() : 0;
    TempStr pwd = otherEngine ? str::DupTemp(EngineMupdfGetPassword(otherEngine)) : TempStr{};
    if (otherEngine) {
        otherEngine->Release();
    }
    if (nOther <= 0) {
        ShowOrganizeError(win, fmt(Tr("Couldn't open '%s'").s, path::GetBaseNameTemp(other)));
        return;
    }

    int n = 0;
    int cur = CurrentPage(win, &n);
    Vec<PdfMergePage> pages;
    for (int i = 1; i <= n; i++) {
        VecAppend(pages, PdfMergePage{0, i, 0});
        if (i != cur) {
            continue;
        }
        for (int k = 1; k <= nOther; k++) {
            VecAppend(pages, PdfMergePage{1, k, 0});
        }
    }
    Vec<PdfMergeSource> extra;
    VecAppend(extra, PdfMergeSource{other, pwd});
    ApplyPageList(win, pages, &extra, cur + 1, fmt(Tr("Inserted %d pages").s, nOther));
}

void OrganizeMovePage(MainWindow* win, int delta) {
    if (!OrganizableTab(win)) {
        return;
    }
    int n = 0;
    int cur = CurrentPage(win, &n);
    int to = cur + delta;
    if (to < 1 || to > n) {
        return;
    }
    Vec<PdfMergePage> pages;
    AllPages(n, pages);
    std::swap(pages[cur - 1], pages[to - 1]);
    ApplyPageList(win, pages, nullptr, to, {});
}

// page `from` goes before page `before` (n + 1 for the end), both 1-based;
// what dragging a thumbnail does
void OrganizeMovePageTo(MainWindow* win, int from, int before) {
    if (!OrganizableTab(win)) {
        return;
    }
    int n = 0;
    CurrentPage(win, &n);
    if (from < 1 || from > n || before < 1 || before > n + 1 || before == from || before == from + 1) {
        return;
    }
    Vec<PdfMergePage> pages;
    for (int i = 1; i <= n + 1; i++) {
        if (i == before) {
            VecAppend(pages, PdfMergePage{0, from, 0});
        }
        if (i <= n && i != from) {
            VecAppend(pages, PdfMergePage{0, i, 0});
        }
    }
    int to = before > from ? before - 1 : before;
    ApplyPageList(win, pages, nullptr, to, {});
}

void OrganizeDeleteCurrentPage(MainWindow* win) {
    if (!OrganizableTab(win)) {
        return;
    }
    int n = 0;
    int cur = CurrentPage(win, &n);
    if (n < 2) {
        return;
    }
    Str msg = fmt(Tr("Delete page %d? The file is saved right away; Undo Page Change restores it.").s, cur);
    if (!ConfirmWarning(win->hwndFrame, msg, Tr("Delete Page"))) {
        return;
    }
    Vec<PdfMergePage> pages;
    for (int i = 1; i <= n; i++) {
        if (i != cur) {
            VecAppend(pages, PdfMergePage{0, i, 0});
        }
    }
    // stays until closed: the way back from a deletion must not vanish
    ApplyPageList(win, pages, nullptr, std::min(cur, n - 1), Tr("Page deleted"), kNotifNoTimeout);
}

// Splits `snapshot`, a saved copy of engine's live document, so the parts get
// unsaved edits and redactions too. Writes each part beside its destination
// and swaps it in, after asking once before replacing existing files.
int OrganizeSplitPdf(MainWindow* win, EngineBase* engine, Str snapshot, Str destBase, int pagesPerFile) {
    if (!engine || len(snapshot) == 0 || pagesPerFile < 1) {
        return 0;
    }
    int n = engine->PageCount();
    Str base = destBase;
    if (str::EndsWithI(base, StrL(".pdf"))) {
        base.len -= 4;
    }

    Vec<PdfMergeSource> srcs;
    VecAppend(srcs, PdfMergeSource{snapshot, EngineMupdfGetPassword(engine)});
    bool overwrites = false;
    for (int first = 1, part = 1; first <= n; first += pagesPerFile, part++) {
        TempStr dest = fmt("%s-%d.pdf", base, part);
        if (path::IsSame(dest, engine->FilePath())) {
            ShowOrganizeError(win, Tr("Choose an output name that does not overwrite the source PDF."));
            return 0;
        }
        overwrites |= file::Exists(dest);
    }
    if (overwrites && !ConfirmWarning(win->hwndFrame, Tr("Some output files already exist. Replace them?"),
                                      Tr("Split PDF"))) {
        return 0;
    }
    int nFiles = 0;
    for (int first = 1; first <= n; first += pagesPerFile) {
        Vec<PdfMergePage> pages;
        for (int i = first; i < first + pagesPerFile && i <= n; i++) {
            VecAppend(pages, PdfMergePage{0, i, 0});
        }
        TempStr dest = fmt("%s-%d.pdf", base, nFiles + 1);
        PdfLiveSnapshot output;
        output.path = str::Dup(PdfTempBeside(dest));
        if (len(output.path) == 0 || !EngineMupdfMergePdfs(srcs, pages, output.path) ||
            !PublishPdfFile(dest, output.path)) {
            ShowOrganizeError(win, fmt(Tr("Couldn't write '%s'").s, path::GetBaseNameTemp(dest)));
            break;
        }
        nFiles++;
    }
    if (nFiles > 0) {
        ShowTemporaryNotification(win->hwndCanvas, fmt(Tr("Split into %d files").s, nFiles));
    }
    return nFiles;
}

static int LastUndoStepFor(Str filePath) {
    for (int i = len(gUndoSteps) - 1; i >= 0; i--) {
        if (path::IsSame(gUndoSteps[i].filePath, filePath)) {
            return i;
        }
    }
    return -1;
}

bool OrganizeCanUndo(MainWindow* win) {
    WindowTab* tab = OrganizableTab(win);
    return tab && LastUndoStepFor(tab->filePath) >= 0;
}

void OrganizeUndo(MainWindow* win) {
    WindowTab* tab = OrganizableTab(win);
    if (!tab) {
        return;
    }
    int idx = LastUndoStepFor(tab->filePath);
    if (idx < 0) {
        ShowTemporaryNotification(win->hwndCanvas, Tr("No page changes to undo"));
        return;
    }
    if (!CommitOpenEditors(win) || OrganizableTab(win) != tab) {
        return;
    }
    OrganizeUndoStep step = gUndoSteps[idx];
    EngineBase* engine = tab->GetEngine();
    bool unsaved = EngineHasUnsavedAnnotations(engine);

    // edits since the step: unsaved ones, or a save (or another file) on disk
    FILETIME modified = file::GetModificationTime(tab->filePath);
    bool changed = unsaved || file::GetSize(tab->filePath) != step.size ||
                   CompareFileTime(&modified, &step.modified) != 0;
    if (changed && !ConfirmWarning(win->hwndFrame,
                                   Tr("Undo Page Change discards the edits made since that change. Continue?"),
                                   Tr("Undo Page Change"))) {
        return;
    }

    // replacing closes the document; on failure only this copy has its unsaved edits
    PdfLiveSnapshot current;
    if (unsaved && !current.Create(engine)) {
        ShowOrganizeError(win, Tr("Couldn't save a recovery copy. Nothing was undone."));
        return;
    }
    if (!ReplaceCurrentDocumentFile(win, step.backupPath, step.pageNo)) {
        if (!IsMainWindowValidAndNotClosing(win)) {
            return;
        }
        if (!unsaved) {
            ShowOrganizeError(win, Tr("Couldn't undo the page change"));
            return;
        }
        Str msg = fmt(Tr("Couldn't undo the page change. Your unsaved edits are in %s").s, current.path);
        ShowPlainWarningNotification(win->hwndCanvas, msg, kNotifNoTimeout);
        str::FreePtr(&current.path);
        return;
    }
    VecRemoveAt(gUndoSteps, idx);
    ToolbarUpdateStateForWindow(win, true);
    if (IsMainWindowValidAndNotClosing(win)) {
        ShowTemporaryNotification(win->hwndCanvas, Tr("Page change undone"));
    }
    file::Delete(step.backupPath);
    str::Free(step.filePath);
    str::Free(step.backupPath);
}

// thumbnails can be dragged to reorder the pages of a PDF file on disk
bool OrganizeCanMovePages(MainWindow* win) {
    WindowTab* tab = OrganizableTab(win);
    return tab && win->ctrl && win->ctrl->PageCount() > 1;
}

// a recovery step's file was shown to the user as holding their edits: keep it
static void FreeUndoStep(int idx) {
    OrganizeUndoStep step = gUndoSteps[idx];
    VecRemoveAt(gUndoSteps, idx);
    if (!step.recovery) {
        file::Delete(step.backupPath);
    }
    str::Free(step.filePath);
    str::Free(step.backupPath);
}

// at exit: undo snapshots are full copies of possibly sensitive documents
void OrganizeClearUndo() {
    while (len(gUndoSteps) > 0) {
        FreeUndoStep(len(gUndoSteps) - 1);
    }
}

static bool IsFileOpenInTab(Str filePath) {
    for (MainWindow* win : gWindows) {
        for (int i = 0; i < win->TabCount(); i++) {
            WindowTab* tab = win->GetTab(i);
            if (tab && path::IsSame(tab->filePath, filePath)) {
                return true;
            }
        }
    }
    return false;
}

// after closing tabs or windows: drop the steps of documents no longer open
void OrganizeForgetClosed() {
    for (int i = len(gUndoSteps) - 1; i >= 0; i--) {
        if (!IsFileOpenInTab(gUndoSteps[i].filePath)) {
            FreeUndoStep(i);
        }
    }
}
