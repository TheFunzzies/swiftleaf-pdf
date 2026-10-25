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

PdfToolEdit::PdfToolEdit(MainWindow* w, Str original) : win(w) {
    WindowTab* tab = w ? w->CurrentTab() : nullptr;
    if (!tab || !path::IsSame(tab->filePath, original)) return;
    originalPath = str::Dup(original);
    inputPath = str::Dup(GetTempFilePathTemp(StrL("tool-input")));
    outputPath = str::Dup(GetTempFilePathTemp(StrL("tool-result")));
    if (!len(inputPath) || !EngineMupdfSaveCopy(tab->GetEngine(), inputPath)) {
        file::Delete(inputPath);
        str::FreePtr(&inputPath);
    }
}

PdfToolEdit::~PdfToolEdit() {
    file::Delete(inputPath);
    file::Delete(outputPath);
    str::Free(inputPath);
    str::Free(outputPath);
    str::Free(originalPath);
}

bool PdfToolEdit::IsReady() {
    return len(inputPath) && len(outputPath);
}

bool PdfToolEdit::Apply(const char* label) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    return IsReady() && tab && path::IsSame(tab->filePath, originalPath) && ApplyPdfEdit(win, outputPath, label);
}

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
// import the result as one journal step and show goToPage.
static bool ApplyPageList(MainWindow* win, const Vec<PdfMergePage>& pages, const Vec<PdfMergeSource>* extra,
                          int goToPage, Str label, Str doneMsg) {
    WindowTab* tab = OrganizableTab(win);
    if (!tab || len(pages) == 0) {
        return false;
    }
    TempStr path = str::DupTemp(tab->filePath);
    EngineBase* engine = tab->GetEngine();

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

    TempStr outPath = GetTempFilePathTemp(StrL("swiftleaf-pages"));
    u32 warnings = 0;
    bool ok = len(outPath) > 0 && EngineMupdfMergePdfs(srcs, pages, outPath, &warnings);
    if (!ok) {
        file::Delete(outPath);
        file::Delete(backup);
        ShowOrganizeError(win, Tr("Couldn't change the pages of this PDF"));
        return false;
    }

    bool applied = ApplyPdfEdit(win, outPath, CStrTemp(label), goToPage);
    file::Delete(outPath);
    file::Delete(backup);
    if (!applied) {
        ShowOrganizeError(win, Tr("Couldn't change the pages of this PDF"));
        return false;
    }
    if (warnings && IsMainWindowValidAndNotClosing(win)) {
        ShowOrganizeError(win, EngineMupdfMergeWarningTemp(warnings));
    } else if (len(doneMsg) > 0 && IsMainWindowValidAndNotClosing(win)) {
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
    ApplyPageList(win, pages, nullptr, cur, StrL("Rotate pages"), Tr("Page rotated"));
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
    ApplyPageList(win, pages, nullptr, cur + 1, StrL("Insert blank page"), Tr("Blank page inserted"));
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
    ApplyPageList(win, pages, &extra, cur + 1, StrL("Insert pages"), fmt(Tr("Inserted %d pages").s, nOther));
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
    ApplyPageList(win, pages, nullptr, to, StrL("Reorder pages"), {});
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
    ApplyPageList(win, pages, nullptr, to, StrL("Reorder pages"), {});
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
    ApplyPageList(win, pages, nullptr, std::min(cur, n - 1), StrL("Delete pages"),
                  Tr("Page deleted. Undo brings it back."));
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

bool OrganizeCanUndo(MainWindow* win) {
    WindowTab* tab = OrganizableTab(win);
    return tab && EngineMupdfCanUndo(tab->GetEngine());
}

void OrganizeUndo(MainWindow* win) {
    SendMessageW(win->hwndFrame, WM_COMMAND, CmdUndo, 0);
}

// thumbnails can be dragged to reorder the pages of a PDF file on disk
bool OrganizeCanMovePages(MainWindow* win) {
    WindowTab* tab = OrganizableTab(win);
    return tab && win->ctrl && win->ctrl->PageCount() > 1;
}
