/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/Pixmap.h"
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "gui/VirtHost.h"
#include "Settings.h"
#include "PanelLayout.h"
#include "Animation.h"
#include "AppSettings.h"
#include "DocController.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Commands.h"
#include "CommandAvailability.h"
#include "Translations.h"
#include "Theme.h"
#include "Material.h"
#include "LucideIcons.h"
#include "Ribbon.h"
#include "Toolbar.h"
#include "Menu.h"
#include "ToolsPanel.h"
#include "RibbonAccess.h"

struct ToolDef {
    const char* icon;
    Str label;
    int cmd;
    // why a command-less row is disabled; empty for rows with a command
    Str unavailableTip;
};
static const ToolDef tools[] = {
    {gLucideFileOutput, TrN("Export a PDF"), CmdConvertPdfToImages},
    {gLucidePenTool, TrN("Fill & Sign"), CmdSignWithImage},
    {gLucideFilePen, TrN("Edit a PDF"), CmdToggleEditPDF},
    {gLucideFilePlus, TrN("Create a PDF"), CmdConvertToPDF},
    {gLucideCombine, TrN("Combine files"), CmdMergePDF},
    {gLucideLayoutGrid, TrN("Organize pages"), CmdToggleThumbnails},
    {gLucideMessageSquareText, TrN("Add comments"), CmdFindAnnotation},
    {gLucideScanText, TrN("Scan & OCR"), CmdScanOcr},
    {gLucideShield, TrN("Protect a PDF"), CmdPdfEncrypt},
    {gLucideRectangleHorizontal, TrN("Redact a PDF"), CmdCreateAnnotRedact},
    {gLucideFileArchive, TrN("Compress a PDF"), CmdPdfCompress},
    {gLucideTextCursorInput, TrN("Prepare a form"), CmdPrepareForm},
    {gLucideStamp, TrN("Add a stamp"), CmdCreateAnnotStamp},
    {gLucideBadgeCheck, TrN("Use a certificate"), CmdSignDocument},
};

static const ToolDef stripTools[] = {
    {gLucideMousePointer, TrN("Select"), CmdToolSelect},
    {gLucideHand, TrN("Hand"), CmdToolHand},
    {gLucideStickyNote, TrN("Add a note"), CmdCreateAnnotText},
    {gLucideHighlighter, TrN("Highlight"), CmdAnnotationHighlightBrush},
    {gLucidePencil, TrN("Draw"), CmdCreateAnnotInk},
    {gLucideType, TrN("Add text"), CmdCreateAnnotFreeText},
};

struct ToolsPanelVirt {
    VirtHost* host = nullptr;
    Animation pageFade;
    Pixmap* oldRows = nullptr;
    Pixmap* fadeRows = nullptr;
    ~ToolsPanelVirt() {
        FreePixmap(oldRows);
        FreePixmap(fadeRows);
    }
    Vec<RibbonButton*> rows;
    Vec<VirtCtrl*> sources;
    Vec<VirtText*> headings;
    ScrollBox* scroll = nullptr;
    int page = -1;
    bool rail = false;
    ILayout* header = nullptr;
    VirtText* title = nullptr;
    VirtButton* more = nullptr;
    VirtHost* strip = nullptr;
    Vec<RibbonButton*> stripRows;
};

int ToolsPanelDx() {
    return DpiScale(ClampPanelWidth(gSettings->toolsPanelWidth, kToolsMinWidth));
}

bool ShouldShowToolsPanel(MainWindow* win) {
    return win && win->toolsPanel && gSettings->showToolsPanel && !win->presentation && !win->isFullScreen &&
           !win->isQuickLook;
}

void ToggleToolsPanel(MainWindow* win) {
    if (win->toolsPanel && win->toolsPanel->host->HasFocus()) HwndSetFocus(win->hwndFrame);
    gSettings->showToolsPanel = !gSettings->showToolsPanel;
    ScheduleSaveSettings();
    ScheduleUiUpdate(win, kUiForceRelayout);
}

static void OnClose(MainWindow* win, VirtMouseEvent* ev) {
    ToggleToolsPanel(win);
    ev->didHandle = true;
}

static void OnMore(MainWindow* win, VirtMouseEvent* ev) {
    ShowToolsPanelMenu(win, win->toolsPanel->host->ToScreen(ev->target->BoundsInWindow()), true);
    ev->didHandle = true;
}

static void OnTool(MainWindow* win, VirtMouseEvent* ev) {
    if (!ev->target || !ev->target->IsEnabled()) return;
    int cmd = ev->target->id;
    if ((cmd >= CmdCreateAnnotFirst && cmd <= CmdCreateAnnotLast) || cmd == CmdAnnotationHighlightBrush)
        EnablePdfAnnotationsToolbar(win);
    HwndPostCommand(win->hwndFrame, cmd);
    ev->didHandle = true;
}

static void PaintBackground(ToolsPanelVirt*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, RibbonPanelBgColor());
    Rect r = ev->clientRect;
    ev->gfx->FillRect({r.Right() - 1, r.y, 1, r.dy}, RibbonEdgeColor());
}

void ToolsPanelAfterThemeChange(MainWindow* win) {
    auto* panel = win->toolsPanel;
    if (!panel) return;
    panel->host->bgColor = RibbonPanelBgColor();
    panel->title->SetColor(kColText, ThemeWindowTextColor());
    if (panel->more) panel->more->SetColor(kColBtnText, RibbonAccentColor());
    if (panel->more) panel->more->SetColor(kColBtnBg, kColorTransparent);
    if (panel->more) panel->more->SetColor(kColBtnBorder, kColorTransparent);
    if (panel->more) panel->more->SetColor(kColBtnBgHover, M3().surfaceContainerHigh);
    for (auto* heading : panel->headings) heading->SetColor(kColText, M3().onSurfaceVariant);
    for (int i = 0; i < len(panel->rows); i++) {
        auto* b = panel->rows[i];
        b->textColor = ThemeWindowTextColor();
        b->textColorDisabled = ThemeWindowTextDisabledColor();
        if (panel->page != (int)RibbonPage::Home) continue;
        b->pixmap =
            RibbonIconPixmap(Str(tools[i].icon), DpiScale(20), RibbonIconColor(tools[i].cmd), RibbonPanelBgColor());
        b->pixmapDisabled =
            RibbonIconPixmap(Str(tools[i].icon), DpiScale(20), ThemeWindowTextDisabledColor(), RibbonPanelBgColor());
    }
    for (int i = 0; i < len(panel->stripRows); i++) {
        auto* b = panel->stripRows[i];
        b->pixmap =
            RibbonIconPixmap(Str(stripTools[i].icon), DpiScale(22), ThemeWindowTextColor(), RibbonPanelBgColor());
        b->pixmapDisabled = RibbonIconPixmap(Str(stripTools[i].icon), DpiScale(22), ThemeWindowTextDisabledColor(),
                                             RibbonPanelBgColor());
    }
    if (panel->strip) {
        panel->strip->bgColor = RibbonPanelBgColor();
        panel->strip->Invalidate(true);
    }
    panel->host->Invalidate(true);
    ToolsPanelUpdate(win);
}

void ToolsPanelUpdate(MainWindow* win) {
    auto* panel = win->toolsPanel;
    if (!panel) return;
    auto ctx = NewAppCommandCtx(win);
    for (auto* b : panel->stripRows) {
        b->SetIsEnabled(GetCommandVisibility(b->id, ctx, CommandSurface::Toolbar) == CommandVisibility::Show);
    }
    for (int i = 0; i < len(panel->rows); i++) {
        auto* b = panel->rows[i];
        auto* source = panel->sources[i];
        bool enabled =
            source ? source->IsEnabled() && source->GetVisibility() == Visibility::Visible
                   : b->id && GetCommandVisibility(b->id, ctx, CommandSurface::Toolbar) == CommandVisibility::Show;
        b->SetIsEnabled(enabled);
        if (auto* icon = AsVirtIconButton(source)) {
            b->isSelected = icon->isSelected;
            b->pixmap = icon->pixmap;
            b->pixmapDisabled = icon->pixmapDisabled;
        }
        if (source) b->SetTooltip(len(source->tooltip) ? source->tooltip : b->label);
        b->Invalidate();
    }
}

static void OnRowFocus(MainWindow* win, VirtFocusEvent* ev) {
    if (!ev->gotFocus) return;
    RibbonAccessFocus(win, ev);
    auto* panel = win->toolsPanel;
    auto* scroll = panel->scroll;
    Rect r = ev->w->BoundsInWindow();
    int top = scroll->bounds.y;
    int bottom = scroll->bounds.Bottom();
    if (r.y < top)
        scroll->ScrollBy(r.y - top);
    else if (r.Bottom() > bottom)
        scroll->ScrollBy(r.Bottom() - bottom);
}

static void OnRowKey(MainWindow* win, VirtKeyEvent* ev) {
    auto* panel = win->toolsPanel;
    auto* w = ev->target;
    if (ev->vkey == VK_ESCAPE) {
        HideToolbarHoverDropdown(win);
        HwndSetFocus(win->hwndFrame);
    } else if (ev->vkey == VK_RIGHT || ev->vkey == VK_APPS || (ev->isAlt && ev->vkey == VK_DOWN)) {
        ToolbarToolOptions(win, w->id);
    } else if (ev->vkey == VK_RETURN || ev->vkey == VK_SPACE) {
        VirtMouseEvent click;
        click.target = w;
        w->onClick.Call(&click);
    } else if (ev->vkey == VK_DOWN || ev->vkey == VK_UP || ev->vkey == VK_HOME || ev->vkey == VK_END) {
        Vec<TabStop> stops;
        CollectTabStops(panel->scroll->child, stops);
        int idx = -1;
        for (int i = 0; i < len(stops); i++)
            if (stops[i].vwnd == w) idx = i;
        int count = len(stops);
        if (!count) return;
        int next = ev->vkey == VK_HOME  ? 0
                   : ev->vkey == VK_END ? count - 1
                                        : (idx + (ev->vkey == VK_UP ? count - 1 : 1)) % count;
        panel->host->vroot->SetFocus(stops[next].vwnd);
    } else
        return;
    ev->didHandle = true;
}

static void SetupRow(MainWindow* win, VirtCtrl* row) {
    row->SetFlag(vwfFocusable, true);
    row->onKeyDown = MkFunc1(OnRowKey, win);
    row->onFocusChanged = MkFunc1(OnRowFocus, win);
}

static void ClearPageFade(void* context) {
    auto* panel = (ToolsPanelVirt*)context;
    if (panel->pageFade.running) return;
    FreePixmap(panel->oldRows);
    FreePixmap(panel->fadeRows);
    panel->oldRows = panel->fadeRows = nullptr;
}

static void CapturePage(ToolsPanelVirt* panel) {
    panel->pageFade.Snap(1);
    ClearPageFade(panel);
    auto* host = panel->host;
    auto* buffer = host->gfxBuf;
    Rect r = host->ClientRect();
    if (!AnimationAllowed(host->native) || !buffer || !buffer->hdc || r.dx != buffer->dx || r.dy != buffer->dy ||
        r.IsEmpty())
        return;
    panel->oldRows = AllocPixmapDIB(r.dx, r.dy);
    panel->fadeRows = AllocPixmapDIB(r.dx, r.dy);
    if (!panel->oldRows || !panel->fadeRows) {
        ClearPageFade(panel);
        return;
    }
    HDC dc = CreateCompatibleDC(buffer->hdc);
    HGDIOBJ old = SelectObject(dc, panel->oldRows->hbmp);
    BOOL ok = BitBlt(dc, 0, 0, r.dx, r.dy, buffer->hdc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteDC(dc);
    if (!ok) {
        ClearPageFade(panel);
        return;
    }
    panel->fadeRows->hasAlpha = true;
    panel->fadeRows->premultiplied = true;
    panel->pageFade.Snap(0);
    panel->pageFade.To(host->native, 1);
    if (!panel->pageFade.running) ClearPageFade(panel);
}

static void PaintPageFade(ToolsPanelVirt* panel, VirtHostPaintEvent* ev) {
    if (!panel->oldRows || !panel->fadeRows) return;
    if (!AnimationAllowed(ev->host->native)) {
        panel->pageFade.Snap(1);
        ClearPageFade(panel);
        return;
    }
    auto* src = panel->oldRows;
    auto* dst = panel->fadeRows;
    int alpha = (int)(255 * (1 - panel->pageFade.value));
    for (int y = 0; y < src->height; y++) {
        u8* s = src->data + y * src->stride;
        u8* d = dst->data + y * dst->stride;
        for (int x = 0; x < src->width; x++, s += 4, d += 4) {
            d[0] = (u8)(s[0] * alpha / 255);
            d[1] = (u8)(s[1] * alpha / 255);
            d[2] = (u8)(s[2] * alpha / 255);
            d[3] = (u8)alpha;
        }
    }
    ev->gfx->DrawPixmap(dst, {0, 0, dst->width, dst->height});
}

static void FocusPanelRow(ToolsPanelVirt* panel) {
    auto* root = panel->host->vroot;
    if (!root) return;
    for (auto* row : panel->rows) {
        if (!row->IsEnabled()) continue;
        root->SetFocus(row);
        if (root->focused) return;
    }
    Vec<TabStop> stops;
    CollectTabStops(panel->scroll->child, stops);
    if (len(stops)) root->SetFocus(stops[0].vwnd);
}

void ToolsPanelShowPage(MainWindow* win) {
    auto* panel = win->toolsPanel;
    if (!panel || panel->page == win->ribbonPage) return;
    bool hadFocus = panel->host->HasFocus();
    CapturePage(panel);
    panel->page = win->ribbonPage;
    VecReset(panel->rows);
    VecReset(panel->sources);
    VecReset(panel->headings);
    panel->more = nullptr;
    auto* col = new VBox();
    col->alignCross = CrossAxisAlign::Stretch;
    col->gap = DpiScale(2);
    auto header = NewLabelWithClose(win->hwndFrame, GetAppFont(), MkFunc1(OnClose, win));
    panel->title = header.label;
    auto page = (RibbonPage)panel->page;
    int mode = RibbonModeForPage(page);
    Str title =
        mode >= 0 && RibbonModePage((RibbonMode)mode) == page ? RibbonModeName((RibbonMode)mode) : RibbonPageName(page);
    panel->title->SetText(title);
    SetWindowTextW(panel->host->native, CWStrTemp(title));
    header.closeBtn->SetTooltip(Tr("Close tools panel"));
    SetupRow(win, header.closeBtn);
    panel->header = header.box;
    col->AddChild(header.box);
    if (page == RibbonPage::Home) {
        for (const auto& d : tools) {
            auto* b = new RibbonButton();
            b->SetLabel(trans::GetTranslation(d.label));
            b->id = d.cmd;
            b->SetTooltip(trans::GetTranslation(len(d.unavailableTip) ? d.unavailableTip : d.label));
            b->onClick = MkFunc1(OnTool, win);
            VecAppend(panel->rows, b);
            VecAppend(panel->sources, (VirtCtrl*)nullptr);
            col->AddChild(b);
        }
        panel->more = new VirtButton(Tr("View more"), GetAppFont());
        panel->more->onClick = MkFunc1(OnMore, win);
        SetupRow(win, panel->more);
        col->AddChild(panel->more);
    } else {
        Vec<ToolbarPageTool> defs;
        ToolbarPageTools(win, panel->page, defs);
        for (auto& d : defs) {
            if (len(d.group)) {
                auto* heading = new VirtText(trans::GetTranslation(d.group), GetScaledPlatformFont(GetAppFont(), 90));
                heading->padding = DpiScaledInsets(4, 8);
                VecAppend(panel->headings, heading);
                col->AddChild(heading);
            }
            auto* b = new RibbonButton();
            auto* source = AsRibbonButton(d.source);
            auto* text = AsVirtButton(d.source);
            b->SetLabel(source ? source->label : text ? text->s : d.source->tooltip);
            b->id = d.source->id;
            ToolbarSetupToolRow(win, b);
            VecAppend(panel->rows, b);
            VecAppend(panel->sources, d.source);
            col->AddChild(b);
        }
    }
    for (auto* b : panel->rows) {
        b->font = GetAppFont();
        b->alignStart = true;
        b->isRtl = IsUIRtl();
        SetupRow(win, b);
    }
    panel->scroll = new ScrollBox(new Padding(col, DpiScaledInsets(8, 8)));
    panel->scroll->lineDy = RibbonButtonDy(GetAppFont());
    panel->host->SetLayout(panel->scroll);
    bool rail = panel->rail;
    panel->rail = !rail;
    ToolsPanelSetRail(win, rail);
    ToolsPanelAfterThemeChange(win);
    ToolsPanelUpdate(win);
    if (hadFocus) FocusPanelRow(panel);
}

void ToolsPanelSetRail(MainWindow* win, bool rail) {
    auto* panel = win->toolsPanel;
    if (!panel || panel->rail == rail) return;
    panel->rail = rail;
    auto vis = rail ? Visibility::Collapse : Visibility::Visible;
    panel->header->SetVisibility(vis);
    for (auto* heading : panel->headings) heading->SetVisibility(vis);
    if (panel->more) panel->more->SetVisibility(vis);
    for (auto* row : panel->rows) {
        row->compact = rail;
        row->alignStart = !rail;
    }
    panel->host->Relayout();
}

static void PanelNativeMsg(MainWindow* win, VirtHostNativeMsg* ev) {
    if (RibbonAccessMessage(ev)) return;
    auto* panel = win->toolsPanel;
    auto* root = panel->host->vroot;
    if (!root) return;
    if (ev->msg == WM_SETFOCUS && !root->focused) {
        FocusPanelRow(panel);
    } else if (ev->msg == WM_KEYDOWN && ev->wp == VK_TAB) {
        Vec<TabStop> stops;
        CollectTabStops(panel->scroll->child, stops);
        bool back = IsShiftPressed();
        if (len(stops) && root->focused == stops[back ? 0 : len(stops) - 1].vwnd) {
            AdvanceFocus(win);
            ev->didHandle = true;
        }
    } else if (ev->msg == WM_VSCROLL && panel->scroll) {
        panel->scroll->OnVScroll(ev->wp);
        ev->res = 0;
        ev->didHandle = true;
    } else if (ev->msg == WM_MOUSEWHEEL && panel->scroll) {
        VirtMouseEvent mev;
        mev.wheelDelta = GET_WHEEL_DELTA_WPARAM(ev->wp);
        panel->scroll->OnMouseWheel(&mev);
        ev->res = 0;
        ev->didHandle = true;
    }
}

static void StripNativeMsg(MainWindow*, VirtHostNativeMsg* ev) {
    RibbonAccessMessage(ev);
}

void CreateToolsPanel(MainWindow* win) {
    if (win->toolsPanel) return;
    auto* panel = new ToolsPanelVirt();
    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = WStrL(L"SWIFTLEAF_TOOLS_PANEL");
    args.initialSize = {ToolsPanelDx(), 600};
    args.bgColor = RibbonPanelBgColor();
    panel->host = VirtHost::Create(args);
    if (!panel->host) {
        delete panel;
        return;
    }
    win->toolsPanel = panel;
    win->hwndToolsPanel = panel->host->native;
    panel->host->onNativeMsg = MkFunc1(PanelNativeMsg, win);
    panel->host->onPaintBackground = MkFunc1(PaintBackground, panel);
    panel->host->onPaint = MkFunc1(PaintPageFade, panel);
    panel->pageFade.onTick = ClearPageFade;
    panel->pageFade.context = panel;
    ToolsPanelShowPage(win);
    args.parent = win->hwndCanvas;
    args.className = WStrL(L"SWIFTLEAF_ANNOTATION_STRIP");
    args.initialSize = {DpiScale(44), DpiScale(260)};
    panel->strip = VirtHost::Create(args);
    if (panel->strip) {
        panel->strip->onNativeMsg = MkFunc1(StripNativeMsg, win);
        auto* stripCol = new VBox();
        stripCol->alignCross = CrossAxisAlign::Stretch;
        stripCol->gap = DpiScale(6);
        for (const auto& d : stripTools) {
            auto* b = new RibbonButton();
            b->font = GetAppFont();
            b->id = d.cmd;
            b->SetTooltip(trans::GetTranslation(d.label));
            b->onClick = MkFunc1(OnTool, win);
            VecAppend(panel->stripRows, b);
            stripCol->AddChild(b);
        }
        panel->strip->SetLayout(new Padding(stripCol, DpiScaledInsets(6, 6)));
    }
    ToolsPanelAfterThemeChange(win);
    ToolsPanelUpdate(win);
}

void DestroyToolsPanel(MainWindow* win) {
    if (!win->toolsPanel) return;
    delete win->toolsPanel->strip;
    delete win->toolsPanel->host;
    delete win->toolsPanel;
    win->toolsPanel = nullptr;
    win->hwndToolsPanel = nullptr;
}

void ShowTopBarMenu(MainWindow* win, Rect anchor) {
    HMENU menu = BuildMenu(win);
    UpdateAppMenu(win, menu);
    int cmd =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, anchor.x, anchor.Bottom(), 0, win->hwndFrame, nullptr);
    FreeMenuOwnerDrawInfoData(menu);
    DestroyMenu(menu);
    if (cmd) HwndPostCommand(win->hwndFrame, cmd);
}

Rect ToolsPanelButtonRect(MainWindow* win, int cmdId) {
    auto* panel = win ? win->toolsPanel : nullptr;
    if (!panel || !panel->host->IsVisible()) return {};
    for (auto* b : panel->rows) {
        if (b->id != cmdId) continue;
        Rect r = b->VisibleRectInWindow();
        return r.IsEmpty() ? Rect{} : panel->host->ToScreen(r);
    }
    return {};
}

static void AppendToolMenu(HMENU menu, Str label, int cmd, bool enabled, bool selected) {
    UINT flags = MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED) | (selected ? MF_CHECKED : MF_UNCHECKED);
    TempStr escaped = str::ReplaceTemp(label, StrL("&"), StrL("&&"));
    AppendMenuW(menu, flags, cmd, CWStrTemp(escaped));
}

static HMENU PageToolMenu(MainWindow* win, int page) {
    HMENU menu = CreatePopupMenu();
    Vec<ToolbarPageTool> defs;
    ToolbarPageTools(win, page, defs);
    for (auto& d : defs) {
        if (len(d.group) && GetMenuItemCount(menu) > 0) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        auto* b = AsRibbonButton(d.source);
        auto* text = AsVirtButton(d.source);
        auto* icon = AsVirtIconButton(d.source);
        Str label = b ? b->label : text ? text->s : d.source->tooltip;
        bool enabled = d.source->IsEnabled() && d.source->GetVisibility() == Visibility::Visible;
        AppendToolMenu(menu, label, d.source->id, enabled, icon && icon->isSelected);
    }
    return menu;
}

void ShowToolsPanelMenu(MainWindow* win, Rect anchor, bool allPages) {
    ToolbarUpdateStateForWindow(win, true);
    HideToolbarHoverDropdown(win);
    HMENU menu;
    if (win->ribbonPage != (int)RibbonPage::Home && !allPages) {
        menu = PageToolMenu(win, win->ribbonPage);
    } else {
        menu = CreatePopupMenu();
        auto ctx = NewAppCommandCtx(win);
        for (const auto& d : tools) {
            bool enabled = GetCommandVisibility(d.cmd, ctx, CommandSurface::Toolbar) == CommandVisibility::Show;
            AppendToolMenu(menu, trans::GetTranslation(d.label), d.cmd, enabled, false);
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        for (int page = 0; page < RibbonPageCount(); page++) {
            HMENU sub = PageToolMenu(win, page);
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, CWStrTemp(RibbonPageName((RibbonPage)page)));
        }
    }
    int cmd =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, anchor.x, anchor.Bottom(), 0, win->hwndFrame, nullptr);
    DestroyMenu(menu);
    ToolbarNoteDropdownClosed();
    if (cmd) ToolbarPostCommand(win, cmd);
}

TempStr ToolsPanelStateTemp(MainWindow* win) {
    str::Builder out;
    auto* panel = win->toolsPanel;
    if (!panel) return {};
    out.Append(fmt("toolsPanel page=%d visible=%d title=%s\n", panel->page, panel->host->IsVisible() ? 1 : 0,
                   panel->title->s));
    out.Append(
        fmt("panel-layout rail=%d toolsPref=%d commentsPref=%d toolsWidth=%d commentsWidth=%d canvasWidth=%d dpi=%d\n",
            panel->rail, gSettings->toolsPanelWidth, gSettings->commentsPanelWidth, win->toolsPanelSlot->dx,
            win->commentsPanelSlot->dx, win->canvasSlot->lastBounds.dx, DpiGet()));
    const bool sides[] = {true, false};
    for (bool isTools : sides) {
        auto* splitter = isTools ? win->toolsPanelSplitter : win->commentsPanelSplitter;
        Rect r = splitter->BoundsInWindow();
        out.Append(fmt("panel-handle side=%s rect=%d,%d,%d,%d\n", isTools ? StrL("tools") : StrL("comments"), r.x, r.y,
                       r.Right(), r.Bottom()));
    }
    out.Append(fmt("motion allowed=%d toolsRunning=%d commentsRunning=%d pageRunning=%d active=%d\n",
                   AnimationAllowed(win->hwndFrame), win->toolsWidthAnimation->running,
                   win->commentsWidthAnimation->running, panel->pageFade.running, AnimationCount(win->hwndFrame)));
    out.Append(fmt("ribbon height=%d stripVisible=0\n", win->toolbarVirt->ribbonDy));
    for (int i = 0; i < len(win->toolbarVirt->ribbonTabs); i++) {
        auto* tab = win->toolbarVirt->ribbonTabs[i];
        Rect r = tab->BoundsInWindow();
        out.Append(fmt("ribbon-tab=%d active=%d rect=%d,%d,%d,%d text=%s\n", i, tab->isActive ? 1 : 0, r.x, r.y,
                       r.Right(), r.Bottom(), tab->label));
    }
    for (int i = 0; i < len(panel->rows); i++) {
        auto* b = panel->rows[i];
        Rect r = b->BoundsInWindow();
        out.Append(fmt("panel-row=%d cmd=%d enabled=%d checked=%d rect=%d,%d,%d,%d text=%s tip=%s\n", i, b->id,
                       b->IsEnabled() ? 1 : 0, b->isSelected ? 1 : 0, r.x, r.y, r.Right(), r.Bottom(), b->label,
                       b->tooltip));
    }
    return ToStrTemp(out);
}

void PositionAnnotationStrip(MainWindow* win) {
    auto* panel = win->toolsPanel;
    if (!panel || !panel->strip) return;
    bool show = win->CurrentTab() && IsPdfDoc(win->CurrentTab()) && !win->presentation && !win->isFullScreen &&
                !HwndIsVisible(win->hwndToolsPanel);
    SetWindowPos(panel->strip->native, HWND_TOP, DpiScale(16), DpiScale(16), DpiScale(44), DpiScale(250),
                 SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}
