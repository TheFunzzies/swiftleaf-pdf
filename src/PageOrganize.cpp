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
#include "PageOrganize.h"

// one step Undo Page Change can take back: the file as it was before the change
struct OrganizeUndoStep {
    Str filePath;   // owned, the document
    Str backupPath; // owned, its previous content
    int pageNo = 0;
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

static TempStr UndoDirTemp() {
    TempStr dir = path::JoinTemp(GetTempDirTemp(), StrL("Swiftleaf-undo"));
    dir::CreateAll(dir);
    return dir;
}

// Write the document's pages as listed in `pages` (extra PDFs are sources 1..),
// replace the file with the result and reopen it on goToPage.
static bool ApplyPageList(MainWindow* win, const Vec<PdfMergePage>& pages, const Vec<PdfMergeSource>* extra,
                          int goToPage, Str doneMsg) {
    WindowTab* tab = OrganizableTab(win);
    if (!tab || len(pages) == 0) {
        return false;
    }
    TempStr path = str::DupTemp(tab->filePath);
    EngineBase* engine = tab->GetEngine();

    // the document as shown, including annotations not yet saved
    Str basePath = path;
    TempStr unsavedCopy;
    if (engine && EngineHasUnsavedAnnotations(engine)) {
        unsavedCopy = GetTempFilePathTemp(StrL("swiftleaf-base"));
        if (len(unsavedCopy) == 0 || !EngineMupdfSaveCopy(engine, unsavedCopy)) {
            ShowOrganizeError(win, Tr("Couldn't save the document's changes"));
            return false;
        }
        basePath = unsavedCopy;
    }

    Vec<PdfMergeSource> srcs;
    VecAppend(srcs, PdfMergeSource{basePath, EngineMupdfGetPassword(engine)});
    for (int i = 0; extra && i < len(*extra); i++) {
        const PdfMergeSource& src = (*extra)[i];
        VecAppend(srcs, src);
    }

    TempStr outPath = GetTempFilePathTemp(StrL("swiftleaf-pages"));
    bool ok = len(outPath) > 0 && EngineMupdfMergePdfs(srcs, pages, outPath);
    if (unsavedCopy) {
        file::Delete(unsavedCopy);
    }
    if (!ok) {
        file::Delete(outPath);
        ShowOrganizeError(win, Tr("Couldn't change the pages of this PDF"));
        return false;
    }

    // keep the current file so the change can be undone
    TempStr backup = path::JoinTemp(UndoDirTemp(), fmt("%d-%s", len(gUndoSteps), path::GetBaseNameTemp(path)));
    bool haveBackup = CopyFileW(ToWStrTemp(path).s, ToWStrTemp(backup).s, FALSE);
    int fromPage = win->ctrl ? win->ctrl->CurrentPageNo() : 1;

    if (!ReplaceCurrentDocumentFile(win, outPath, goToPage)) {
        file::Delete(outPath);
        if (haveBackup) {
            file::Delete(backup);
        }
        ShowOrganizeError(win, Tr("Couldn't write the PDF. Is it read-only or open in another program?"));
        return false;
    }
    if (haveBackup) {
        VecAppend(gUndoSteps, OrganizeUndoStep{str::Dup(path), str::Dup(backup), fromPage});
    }
    if (len(doneMsg) > 0 && IsMainWindowValidAndNotClosing(win)) {
        ShowTemporaryNotification(win->hwndCanvas, doneMsg);
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
    Vec<PdfMergePage> pages;
    for (int i = 1; i <= n; i++) {
        if (i != cur) {
            VecAppend(pages, PdfMergePage{0, i, 0});
        }
    }
    ApplyPageList(win, pages, nullptr, std::min(cur, n - 1), Tr("Page deleted. Undo Page Change brings it back."));
}

int OrganizeSplitPdf(MainWindow* win, Str destBase, int pagesPerFile) {
    WindowTab* tab = OrganizableTab(win);
    if (!tab || pagesPerFile < 1) {
        return 0;
    }
    int n = 0;
    CurrentPage(win, &n);
    Str base = destBase;
    if (str::EndsWithI(base, StrL(".pdf"))) {
        base.len -= 4;
    }

    Vec<PdfMergeSource> srcs;
    VecAppend(srcs, PdfMergeSource{tab->filePath, EngineMupdfGetPassword(tab->GetEngine())});
    int nFiles = 0;
    for (int first = 1; first <= n; first += pagesPerFile) {
        Vec<PdfMergePage> pages;
        for (int i = first; i < first + pagesPerFile && i <= n; i++) {
            VecAppend(pages, PdfMergePage{0, i, 0});
        }
        TempStr dest = fmt("%s-%d.pdf", base, nFiles + 1);
        if (!EngineMupdfMergePdfs(srcs, pages, dest)) {
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
    OrganizeUndoStep step = gUndoSteps[idx];
    VecRemoveAt(gUndoSteps, idx);
    if (!ReplaceCurrentDocumentFile(win, step.backupPath, step.pageNo)) {
        ShowOrganizeError(win, Tr("Couldn't undo the page change"));
    } else if (IsMainWindowValidAndNotClosing(win)) {
        ShowTemporaryNotification(win->hwndCanvas, Tr("Page change undone"));
    }
    str::Free(step.filePath);
    str::Free(step.backupPath);
}

// thumbnails can be dragged to reorder the pages of a PDF file on disk
bool OrganizeCanMovePages(MainWindow* win) {
    WindowTab* tab = OrganizableTab(win);
    return tab && win->ctrl && win->ctrl->PageCount() > 1;
}
