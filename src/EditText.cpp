/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Edit Text: select some text (or click it after Edit Text), and an editor
// opens over the whole paragraph, in about the same font and size, wrapping
// like the page does. Enter writes the new text into the page, re-flowed to
// the paragraph's width (EngineMupdfReplaceText), Shift+Enter starts a new
// paragraph, Esc leaves the page as it was.

#include "base/Base.h"
#include "base/UITask.h"
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
#include "DisplayModel.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Selection.h"
#include "Translations.h"
#include "Notifications.h"
#include "EditText.h"

struct EditTextSession {
    MainWindow* win = nullptr;
    WindowTab* tab = nullptr;
    int pageNo = 0;
    PdfTextRun run;
    HWND hwndEdit = nullptr;
    HFONT font = nullptr;
    bool closing = false;
};

static EditTextSession* gSession = nullptr;
static LRESULT CALLBACK EditTextWndProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
constexpr UINT_PTR kEditTextSubclassId = 0x5f1e;

static void DestroySession(EditTextSession* s) {
    if (s->hwndEdit) {
        RemoveWindowSubclass(s->hwndEdit, EditTextWndProc, kEditTextSubclassId);
        DestroyWindow(s->hwndEdit);
    }
    if (s->font) {
        DeleteObject(s->font);
    }
    str::Free(s->run.text);
    delete s;
}

static void CloseSession(bool commit) {
    EditTextSession* s = gSession;
    if (!s || s->closing) {
        return;
    }
    s->closing = true;
    gSession = nullptr;

    TempStr text;
    if (commit && s->hwndEdit) {
        text = HwndGetTextTemp(s->hwndEdit);
        text = str::ReplaceTemp(text, StrL("\r\n"), StrL("\n"));
    }
    bool changed = commit && !str::Eq(text, s->run.text);
    MainWindow* win = s->win;
    WindowTab* tab = s->tab;
    int pageNo = s->pageNo;
    PdfTextRun run = s->run;
    s->run.text = {};
    // the edit is gone before the page repaints under it
    DestroySession(s);
    if (changed && IsMainWindowValidAndNotClosing(win) && win->CurrentTab() == tab) {
        ReplaceTextInTab(tab, pageNo, run, text);
    }
    str::Free(run.text);
    if (IsMainWindowValidAndNotClosing(win)) {
        HwndSetFocus(win->hwndCanvas);
    }
}

static void PostedCloseSession(void* commit) {
    CloseSession(commit != nullptr);
}

static LRESULT CALLBACK EditTextWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                uitask::Post(MkFunc0(PostedCloseSession, (void*)nullptr), "EditTextCancel");
                return 0;
            }
            if (wp == VK_RETURN && !IsShiftPressed()) {
                uitask::Post(MkFunc0(PostedCloseSession, (void*)1), "EditTextCommit");
                return 0;
            }
            break;
        case WM_CHAR:
            // the Enter / Esc above would otherwise beep or add a line
            if ((wp == '\r' && !IsShiftPressed()) || wp == 27) {
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            // clicking elsewhere keeps the edit, like Foxit
            uitask::Post(MkFunc0(PostedCloseSession, (void*)1), "EditTextBlur");
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static HFONT CreateRunFont(const PdfTextRun& run, int pxHeight) {
    const WCHAR* face = run.mono ? L"Courier New" : (run.serif ? L"Times New Roman" : L"Arial");
    return CreateFontW(-pxHeight, 0, 0, 0, run.bold ? FW_BOLD : FW_NORMAL, run.italic, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

// the selection's box on its first page
static bool FirstSelectionRect(WindowTab* tab, int* pageNo, RectF* rect) {
    Vec<SelectionOnPage>* sel = tab ? tab->selectionOnPage : nullptr;
    if (!sel || len(*sel) == 0) {
        return false;
    }
    *pageNo = (*sel)[0].pageNo;
    RectF r = (*sel)[0].rect;
    for (int i = 1; i < len(*sel); i++) {
        if ((*sel)[i].pageNo == *pageNo) {
            r = r.Union((*sel)[i].rect);
        }
    }
    *rect = r;
    return true;
}

// waiting for a click on the line to edit (Edit Text with nothing selected)
static MainWindow* gPickingWin = nullptr;

static void OpenEditor(MainWindow* win, int pageNo, RectF area);

void StartEditText(MainWindow* win) {
    CloseSession(true);
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    DisplayModel* dm = tab ? tab->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine || !EngineSupportsAnnotations(engine)) {
        return;
    }
    // a second click on the button ends picking
    if (gPickingWin == win) {
        gPickingWin = nullptr;
        return;
    }
    int pageNo = 0;
    RectF area;
    if (FirstSelectionRect(tab, &pageNo, &area)) {
        OpenEditor(win, pageNo, area);
        return;
    }
    gPickingWin = win;
    ShowTemporaryNotification(win->hwndCanvas, Tr("Click the text you want to change"), kNotif5SecsTimeOut);
}

bool EditTextOnLeftDown(MainWindow* win, Point pt) {
    if (!win || win != gPickingWin) {
        return false;
    }
    gPickingWin = nullptr;
    DisplayModel* dm = win->AsFixed();
    int pageNo = dm ? dm->GetPageNoByPoint(pt) : 0;
    if (!dm || !dm->ValidPageNo(pageNo)) {
        return true;
    }
    PointF p = dm->CvtFromScreen(pt, pageNo);
    OpenEditor(win, pageNo, RectF{p.x - 1, p.y - 1, 2, 2});
    return true;
}

bool EditTextOnSetCursor(MainWindow* win) {
    if (!win || win != gPickingWin) {
        return false;
    }
    SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
    return true;
}

static void OpenEditor(MainWindow* win, int pageNo, RectF area) {
    WindowTab* tab = win->CurrentTab();
    DisplayModel* dm = tab ? tab->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine) {
        return;
    }
    auto* s = new EditTextSession();
    if (!EngineMupdfGetTextRun(engine, pageNo, area, TextRunScope::Paragraph, &s->run) || len(s->run.text) == 0) {
        DestroySession(s);
        ShowTemporaryNotification(win->hwndCanvas, Tr("There is no text there that can be edited"));
        return;
    }
    s->win = win;
    s->tab = tab;
    s->pageNo = pageNo;
    DeleteOldSelectionInfo(win, true);
    ScheduleRepaint(win, 0);

    // the editor covers the old paragraph, in its size at the current zoom,
    // with a line to spare for text that grows; it wraps at the same width
    Rect r = dm->CvtToScreen(pageNo, s->run.bbox);
    float scale = s->run.bbox.dy > 0 ? (float)r.dy / s->run.bbox.dy : dm->GetZoomReal(pageNo);
    int fontPx = std::max(8, (int)(s->run.fontSize * scale + 0.5f));
    int lineDy = std::max(fontPx, (int)(s->run.lineGap * scale + 0.5f));
    int pad = DpiScale(4);
    r.x -= pad;
    r.y -= pad;
    r.dx = std::max(r.dx + (2 * pad) + fontPx, DpiScale(220));
    r.dy += (2 * pad) + lineDy;

    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    s->hwndEdit = CreateWindowExW(0, WC_EDITW, L"", style, r.x, r.y, r.dx, r.dy, win->hwndCanvas, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (!s->hwndEdit) {
        DestroySession(s);
        return;
    }
    s->font = CreateRunFont(s->run, fontPx);
    SendMessageW(s->hwndEdit, WM_SETFONT, (WPARAM)s->font, TRUE);
    TempStr shown = str::ReplaceTemp(s->run.text, StrL("\n"), StrL("\r\n"));
    HwndSetText(s->hwndEdit, shown);
    SendMessageW(s->hwndEdit, EM_SETSEL, 0, -1);
    SetWindowSubclass(s->hwndEdit, EditTextWndProc, kEditTextSubclassId, 0);
    gSession = s;
    SetFocus(s->hwndEdit);
}

void CancelEditText() {
    CloseSession(false);
}
