/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
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
    Vec<RibbonButton*> rows;
    VirtText* title = nullptr;
    VirtButton* more = nullptr;
    VirtHost* strip = nullptr;
    Vec<RibbonButton*> stripRows;
};

int ToolsPanelDx() {
    return DpiScale(240);
}

bool ShouldShowToolsPanel(MainWindow* win) {
    return win && win->toolsPanel && gSettings->showToolsPanel && !win->presentation && !win->isFullScreen &&
           !win->isQuickLook;
}

void ToggleToolsPanel(MainWindow* win) {
    gSettings->showToolsPanel = !gSettings->showToolsPanel;
    ScheduleSaveSettings();
    ScheduleUiUpdate(win, kUiForceRelayout);
}

static void OnClose(MainWindow* win, VirtMouseEvent* ev) {
    ToggleToolsPanel(win);
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
    panel->more->SetColor(kColBtnText, RibbonAccentColor());
    panel->more->SetColor(kColBtnBg, kColorTransparent);
    panel->more->SetColor(kColBtnBorder, kColorTransparent);
    panel->more->SetColor(kColBtnBgHover, M3().surfaceContainerHigh);
    for (int i = 0; i < len(panel->rows); i++) {
        auto* b = panel->rows[i];
        b->textColor = ThemeWindowTextColor();
        b->textColorDisabled = ThemeWindowTextDisabledColor();
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
}

void ToolsPanelUpdate(MainWindow* win) {
    if (!win->toolsPanel) return;
    auto ctx = NewAppCommandCtx(win);
    for (auto* b : win->toolsPanel->stripRows) {
        b->SetIsEnabled(GetCommandVisibility(b->id, ctx, CommandSurface::Toolbar) == CommandVisibility::Show);
    }
    for (auto* b : win->toolsPanel->rows) {
        bool enabled = b->id && GetCommandVisibility(b->id, ctx, CommandSurface::Toolbar) == CommandVisibility::Show;
        b->SetIsEnabled(enabled);
    }
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
    panel->host->onPaintBackground = MkFunc1(PaintBackground, panel);
    auto* col = new VBox();
    col->alignCross = CrossAxisAlign::Stretch;
    col->gap = DpiScale(6);
    auto header = NewLabelWithClose(win->hwndFrame, GetAppFont(), MkFunc1(OnClose, win));
    panel->title = header.label;
    header.label->SetText(Tr("All tools"));
    col->AddChild(header.box);
    col->AddChild(new VirtSpacer(0, DpiScale(12)));
    for (const auto& d : tools) {
        auto* b = new RibbonButton();
        b->font = GetAppFont();
        b->alignStart = true;
        b->isRtl = IsUIRtl();
        b->SetLabel(trans::GetTranslation(d.label));
        b->id = d.cmd;
        if (len(d.unavailableTip) > 0) {
            b->SetTooltip(trans::GetTranslation(d.unavailableTip));
        } else {
            b->SetTooltip(trans::GetTranslation(d.label));
        }
        b->onClick = MkFunc1(OnTool, win);
        VecAppend(panel->rows, b);
        col->AddChild(b);
    }
    auto* more = new VirtButton(Tr("View more"), GetAppFont());
    panel->more = more;
    more->id = CmdCommandPalette;
    more->onClick = MkFunc1(OnTool, win);
    col->AddChild(more);
    auto* scroll = new ScrollBox(new Padding(col, DpiScaledInsets(16, 16)));
    panel->host->SetLayout(scroll);
    args.parent = win->hwndCanvas;
    args.className = WStrL(L"SWIFTLEAF_ANNOTATION_STRIP");
    args.initialSize = {DpiScale(44), DpiScale(260)};
    panel->strip = VirtHost::Create(args);
    if (panel->strip) {
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

void PositionAnnotationStrip(MainWindow* win) {
    auto* panel = win->toolsPanel;
    if (!panel || !panel->strip) return;
    bool show = win->CurrentTab() && IsPdfDoc(win->CurrentTab()) && !win->presentation && !win->isFullScreen;
    SetWindowPos(panel->strip->native, HWND_TOP, DpiScale(16), DpiScale(16), DpiScale(44), DpiScale(250),
                 SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}
