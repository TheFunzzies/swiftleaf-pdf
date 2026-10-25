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
    Str description;
    int cmd;
    int category;
    // why a command-less row is disabled; empty for rows with a command
    Str unavailableTip;
};

enum ToolCategory {
    ToolCategoryRecommended,
    ToolCategoryExport,
    ToolCategoryCreate,
    ToolCategoryEdit,
    ToolCategoryOrganize,
    ToolCategoryReview,
    ToolCategoryForms,
    ToolCategoryProtect,
    ToolCategoryOptimize,
    ToolCategorySign,
    ToolCategoryScan,
    ToolCategoryStandards,
    ToolCategoryCount,
};

static const Str toolCategoryNames[] = {
    TrN("Recommended"),
    TrN("Export"),           TrN("Create"),   TrN("Edit"), TrN("Organize"), TrN("Review"),   TrN("Forms"),
    TrN("Protect & Redact"), TrN("Optimize"), TrN("Sign"), TrN("Scan/OCR"), TrN("Standards")};

static const char* toolCategoryIcons[] = {
    gLucideStar,
    gLucideFileOutput, gLucideFilePlus,          gLucideFilePen,
    gLucideLayoutGrid, gLucideMessageSquareText, gLucideTextCursorInput,
    gLucideShield,     gLucideFileArchive,       gLucideSignature,
    gLucideScanText,   gLucideBadgeCheck};

static const ToolDef recommendedTools[] = {
    {gLucideFilePen, TrN("Edit a PDF"), TrN("Edit text and objects in the document."), CmdToggleEditPDF,
     ToolCategoryRecommended},
    {gLucidePenTool, TrN("Fill & Sign"), TrN("Fill a form or add a drawn signature."), CmdSignWithImage,
     ToolCategoryRecommended},
    {gLucideLayoutGrid, TrN("Organize pages"), TrN("Reorder, rotate, or remove pages."), CmdToggleThumbnails,
     ToolCategoryRecommended},
    {gLucideFileArchive, TrN("Compress a PDF"), TrN("Reduce the document file size."), CmdPdfCompress,
     ToolCategoryRecommended},
    {gLucideFileOutput, TrN("Export a PDF"), TrN("Save a copy of the open document."), CmdExportPdf,
     ToolCategoryRecommended},
};

static const ToolDef tools[] = {
    {gLucideFileOutput, TrN("Export a PDF"), TrN("Save a copy of the open document."), CmdExportPdf,
     ToolCategoryExport},
    {gLucideFileOutput, TrN("To images"), TrN("Export pages as image files."), CmdConvertPdfToImages,
     ToolCategoryExport},
    {gLucidePenTool, TrN("Fill & Sign"), TrN("Fill a form or add a drawn signature."), CmdSignWithImage,
     ToolCategorySign},
    {gLucideFilePen, TrN("Edit a PDF"), TrN("Edit text and objects in the document."), CmdToggleEditPDF,
     ToolCategoryEdit},
    {gLucideFilePlus, TrN("Create a PDF"), TrN("Create a PDF from files or clipboard content."), CmdCreatePdfFiles,
     ToolCategoryCreate},
    {gLucideCombine, TrN("Combine files"), TrN("Join files into one PDF."), CmdMergePDF, ToolCategoryCreate},
    {gLucideLayoutGrid, TrN("Organize pages"), TrN("Reorder, rotate, or remove pages."), CmdToggleThumbnails,
     ToolCategoryOrganize},
    {gLucideMessageSquareText, TrN("Add comments"), TrN("Add notes and markups to pages."), CmdFindAnnotation,
     ToolCategoryReview},
    {gLucideScanText, TrN("Scan & OCR"), TrN("Recognize text in scanned pages."), CmdScanOcr, ToolCategoryScan},
    {gLucideShield, TrN("Protect a PDF"), TrN("Set a password and permissions."), CmdPdfEncrypt, ToolCategoryProtect},
    {gLucideRectangleHorizontal, TrN("Redact a PDF"), TrN("Permanently remove sensitive content."),
     CmdCreateAnnotRedact, ToolCategoryProtect},
    {gLucideFileArchive, TrN("Compress a PDF"), TrN("Reduce the document file size."), CmdPdfCompress,
     ToolCategoryOptimize},
    {gLucideTextCursorInput, TrN("Prepare a form"), TrN("Add fillable form fields."), CmdPrepareForm,
     ToolCategoryForms},
    {gLucideTextCursorInput, TrN("Detect fields"), TrN("Find likely form fields on the page."), CmdDetectFormFields,
     ToolCategoryForms},
    {gLucideStamp, TrN("Add a stamp"), TrN("Place a stamp annotation on a page."), CmdCreateAnnotStamp,
     ToolCategoryReview},
    {gLucideBadgeCheck, TrN("Use a certificate"), TrN("Sign the document with a certificate."), CmdSignDocument,
     ToolCategorySign},
    {gLucideStamp, TrN("Add watermark"), TrN("Place text or an image behind page content."), CmdAddWatermark,
     ToolCategoryEdit},
    {gLucideType, TrN("Add header/footer"), TrN("Add text to page headers or footers."), CmdAddHeaderFooter,
     ToolCategoryEdit},
    {gLucideTextCursorInput, TrN("Bates numbering"), TrN("Add sequential Bates numbers."), CmdBatesNumbering,
     ToolCategoryStandards},
    {gLucideShield, TrN("Sanitize document"), TrN("Remove hidden data and document metadata."), CmdPdfSanitize,
     ToolCategoryProtect},
    {gLucideRectangleHorizontal, TrN("Redact all matches"), TrN("Find and redact matching text."), CmdPdfRedactSearch,
     ToolCategoryProtect},
    {gLucideRuler, TrN("Measure"), TrN("Measure distances on the page."), CmdMeasureDistance, ToolCategoryReview},
    {gLucideMessageSquareText, TrN("Review & comments"), TrN("Review, summarize, and export comments."),
     CmdCommentSummary, ToolCategoryReview},
    {gLucideLayoutGrid, TrN("Crop pages"), TrN("Set page crop boxes and remove margins."), CmdCropPages,
     ToolCategoryOrganize},
    {gLucideLayoutGrid, TrN("N-up pages"), TrN("Arrange multiple pages on each sheet."), CmdPdfNUp,
     ToolCategoryOrganize},
    {gLucideStamp, TrN("Page background"), TrN("Set a page color or image background."), CmdPdfPageBackground,
     ToolCategoryEdit},
    {gLucideFilePlus, TrN("Duplicate pages"), TrN("Copy selected pages in the document."), CmdDuplicatePages,
     ToolCategoryOrganize},
    {gLucideFilePen, TrN("Document properties"), TrN("Edit title, author, and other metadata."),
     CmdEditDocumentProperties, ToolCategoryStandards},
    {gLucidePaperclip, TrN("Attachments"), TrN("View and manage embedded files."), CmdManageAttachments,
     ToolCategoryOrganize},
    {gLucideBookmark, TrN("Edit bookmarks"), TrN("Create and organize document bookmarks."), CmdEditBookmarks,
     ToolCategoryOrganize},
    {gLucideType, TrN("Page numbering"), TrN("Change page labels and numbering."), CmdEditPageLabels,
     ToolCategoryOrganize},
    {gLucideShield, TrN("Repair PDF"), TrN("Repair damaged PDF structure when possible."), CmdPdfRepair,
     ToolCategoryOptimize},
    {gLucideLayers, TrN("Document layers"), TrN("Show and manage optional content layers."), CmdPdfManageLayers,
     ToolCategoryStandards},
    {gLucideBadgeCheck, TrN("Accessibility checker"), TrN("Check tags, reading order, language, and alternative text."),
     CmdAccessibilityChecker, ToolCategoryStandards},
    {gLucideFilePen, TrN("Accessibility fixes"), TrN("Add document language, title, and basic structure tags."),
     CmdAccessibilityFixes, ToolCategoryStandards},
    {gLucideBadgeCheck, TrN("Convert to PDF/A"), TrN("Convert the document to a PDF/A archive format."), CmdPdfAConvert,
     ToolCategoryStandards},
    {gLucideBadgeCheck, TrN("Validate PDF/A"), TrN("Check the document against PDF/A requirements."), CmdPdfAValidate,
     ToolCategoryStandards},
    {gLucideInfo, TrN("Preflight"), TrN("Inspect fonts, colors, images, and page boxes."), CmdPreflight,
     ToolCategoryStandards},
    {gLucideBadgeCheck, TrN("Convert colors"), TrN("Convert document colors to grayscale or RGB."), CmdPdfConvertColors,
     ToolCategoryStandards},
    {gLucideScanText, TrN("Acquire from scanner"), TrN("Scan pages and create a PDF."), CmdScanAcquire,
     ToolCategoryScan},
    {gLucideScanText, TrN("Import camera image"), TrN("Import and straighten a camera photo."), CmdImportCameraImage,
     ToolCategoryScan},
    {gLucideScanText, TrN("Batch OCR"), TrN("Recognize text across multiple documents."), CmdBatchOcr,
     ToolCategoryScan},
    {gLucideBadgeCheck, TrN("Digital signatures"), TrN("Review document signatures and certificate status."),
     CmdSignaturePanel, ToolCategorySign},
    {gLucideBadgeCheck, TrN("Certify document"), TrN("Apply a document certification signature."), CmdCertifyDocument,
     ToolCategorySign},
};

static const ToolDef stripTools[] = {
    {gLucideMousePointer, TrN("Select"), TrN("Select text and objects."), CmdToolSelect, ToolCategoryEdit},
    {gLucideHand, TrN("Hand"), TrN("Pan around the page."), CmdToolHand, ToolCategoryEdit},
    {gLucideStickyNote, TrN("Add a note"), TrN("Add a note annotation."), CmdCreateAnnotText, ToolCategoryReview},
    {gLucideHighlighter, TrN("Highlight"), TrN("Highlight text on the page."), CmdAnnotationHighlightBrush,
     ToolCategoryReview},
    {gLucidePencil, TrN("Draw"), TrN("Draw freehand on the page."), CmdCreateAnnotInk, ToolCategoryReview},
    {gLucideType, TrN("Add text"), TrN("Add text to the page."), CmdCreateAnnotFreeText, ToolCategoryEdit},
};

bool ToolPanelGetDef(int cmdId, Str* labelOut, Str* descOut, Str* pageOut) {
    for (const auto& d : recommendedTools) {
        if (d.cmd == cmdId) {
            if (labelOut) *labelOut = d.label;
            if (descOut) *descOut = d.description;
            if (pageOut) *pageOut = StrL("All tools");
            return true;
        }
    }
    for (const auto& d : tools) {
        if (d.cmd == cmdId) {
            if (labelOut) *labelOut = d.label;
            if (descOut) *descOut = d.description;
            if (pageOut) *pageOut = StrL("All tools");
            return true;
        }
    }
    for (const auto& d : stripTools) {
        if (d.cmd == cmdId) {
            if (labelOut) *labelOut = d.label;
            if (descOut) *descOut = d.description;
            if (pageOut) *pageOut = StrL("All tools");
            return true;
        }
    }
    return false;
}

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
    Vec<RibbonButton*> favoriteButtons;
    Vec<const ToolDef*> rowDefs;
    Vec<VirtCtrl*> sources;
    Vec<VirtText*> headings;
    Vec<RibbonButton*> categoryHeaders;
    bool categoryExpanded[ToolCategoryCount] = {};
    ScrollBox* scroll = nullptr;
    Edit* searchEdit = nullptr;
    Padding* contentPadding = nullptr;
    int page = -1;
    bool rail = false;
    ILayout* header = nullptr;
    VirtText* title = nullptr;
    RibbonButton* more = nullptr;
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

static bool IsToolFavorite(int cmd) {
    Str saved = gSettings->toolsPanelFavorites;
    int i = 0;
    while (i < len(saved)) {
        while (i < len(saved) && saved.s[i] == ',') i++;
        int value = 0;
        bool hasDigit = false;
        while (i < len(saved) && saved.s[i] >= '0' && saved.s[i] <= '9') {
            value = value * 10 + saved.s[i++] - '0';
            hasDigit = true;
        }
        if (hasDigit && value == cmd) return true;
        while (i < len(saved) && saved.s[i] != ',') i++;
    }
    return false;
}

static void OnFavorite(MainWindow* win, VirtMouseEvent* ev) {
    int cmd = ev->target->id;
    bool remove = IsToolFavorite(cmd);
    Str saved = gSettings->toolsPanelFavorites;
    str::Builder next;
    int i = 0;
    while (i < len(saved)) {
        while (i < len(saved) && saved.s[i] == ',') i++;
        int start = i;
        int value = 0;
        bool hasDigit = false;
        while (i < len(saved) && saved.s[i] >= '0' && saved.s[i] <= '9') {
            value = value * 10 + saved.s[i++] - '0';
            hasDigit = true;
        }
        if (hasDigit && value != cmd) {
            if (len(next)) next.AppendChar(',');
            next.Append(Str(saved.s + start, i - start));
        }
        while (i < len(saved) && saved.s[i] != ',') i++;
    }
    if (!remove) {
        if (len(next)) next.AppendChar(',');
        next.Append(fmt("%d", cmd));
    }
    str::ReplaceWithCopy(&gSettings->toolsPanelFavorites, ToStrTemp(next));
    ScheduleSaveSettings();
    auto* panel = win->toolsPanel;
    for (auto* button : panel->favoriteButtons) {
        if (button->id != cmd) continue;
        button->isSelected = !remove;
        button->SetTooltip(Tr(remove ? "Add to favorites" : "Remove from favorites"));
        button->Invalidate();
    }
    ev->didHandle = true;
}

static void OnSearchChanged(ToolsPanelVirt* panel) {
    if (!panel || panel->page != (int)RibbonPage::Home) return;
    Str query = panel->searchEdit->GetTextTemp();
    bool searching = !str::IsEmptyOrWhiteSpace(query);
    bool hasMatches[ToolCategoryCount] = {};
    for (int i = 0; i < len(panel->rows); i++) {
        const ToolDef* d = panel->rowDefs[i];
        bool matches = !searching || str::ContainsI(panel->rows[i]->label, query) ||
                       str::ContainsI(trans::GetTranslation(d->description), query);
        hasMatches[d->category] |= matches;
        auto vis = matches && (searching || panel->categoryExpanded[d->category])
                       ? Visibility::Visible
                       : Visibility::Collapse;
        panel->rows[i]->SetVisibility(vis);
        if (panel->rows[i]->parent) {
            panel->rows[i]->parent->SetVisibility(vis);
        }
    }
    for (int i = 0; i < len(panel->categoryHeaders); i++)
        panel->categoryHeaders[i]->SetVisibility(hasMatches[i] ? Visibility::Visible : Visibility::Collapse);
    panel->host->Relayout();
}

static void OnCategory(MainWindow* win, VirtMouseEvent* ev) {
    auto* panel = win->toolsPanel;
    int category = -100 - ev->target->id;
    if (category < 0 || category >= ToolCategoryCount) return;
    panel->categoryExpanded[category] = !panel->categoryExpanded[category];
    for (int i = 0; i < len(panel->rows); i++) {
        if (panel->rowDefs[i]->category == category) {
            auto vis = panel->categoryExpanded[category] ? Visibility::Visible : Visibility::Collapse;
            panel->rows[i]->SetVisibility(vis);
            if (panel->rows[i]->parent) {
                panel->rows[i]->parent->SetVisibility(vis);
            }
        }
    }
    AsRibbonButton(ev->target)
        ->SetLabel(fmt("%s  %s", trans::GetTranslation(toolCategoryNames[category]),
                       StrL(panel->categoryExpanded[category] ? "−" : "+")));
    panel->host->Relayout();
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
    if (panel->searchEdit) panel->searchEdit->SetColors(ThemeWindowTextColor(), RibbonPanelBgColor());
    if (panel->more) {
        panel->more->textColor = ThemeWindowTextColor();
        panel->more->pixmap =
            RibbonIconPixmap(Str(gLucideLayoutGrid), DpiScale(20), ThemeWindowTextColor(), RibbonPanelBgColor());
    }
    for (auto* heading : panel->headings) heading->SetColor(kColText, M3().onSurfaceVariant);
    for (int i = 0; i < len(panel->categoryHeaders); i++) {
        auto* heading = panel->categoryHeaders[i];
        heading->textColor = ThemeWindowTextColor();
        heading->pixmap =
            RibbonIconPixmap(Str(toolCategoryIcons[i]), DpiScale(18), ThemeWindowTextColor(), RibbonPanelBgColor());
    }
    for (auto* b : panel->favoriteButtons) {
        b->textColor = ThemeWindowTextColor();
        b->pixmap = RibbonIconPixmap(Str(gLucideStar), DpiScale(18), ThemeWindowTextColor(), RibbonPanelBgColor());
        b->SetTooltip(Tr(IsToolFavorite(b->id) ? "Remove from favorites" : "Add to favorites"));
    }
    for (int i = 0; i < len(panel->rows); i++) {
        auto* b = panel->rows[i];
        b->textColor = ThemeWindowTextColor();
        b->textColorDisabled = ThemeWindowTextDisabledColor();
        if (panel->page != (int)RibbonPage::Home) continue;
        auto* d = panel->rowDefs[i];
        b->pixmap = RibbonIconPixmap(Str(d->icon), DpiScale(20), RibbonIconColor(d->cmd), RibbonPanelBgColor());
        b->pixmapDisabled =
            RibbonIconPixmap(Str(d->icon), DpiScale(20), ThemeWindowTextDisabledColor(), RibbonPanelBgColor());
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
        if (source) {
            b->SetTooltip(len(source->tooltip) ? source->tooltip : b->label);
        } else if (i < len(panel->rowDefs) && panel->rowDefs[i]) {
            b->SetTooltip(trans::GetTranslation(panel->rowDefs[i]->description));
        }
        b->Invalidate();
    }
}

static void OnRowFocus(MainWindow* win, VirtFocusEvent* ev) {
    if (!ev->gotFocus) return;\
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
    VecReset(panel->favoriteButtons);
    VecReset(panel->rowDefs);
    VecReset(panel->sources);
    VecReset(panel->headings);
    VecReset(panel->categoryHeaders);
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
        for (int category = 0; category < ToolCategoryCount; category++) {
            bool isRec = (category == ToolCategoryRecommended);
            const ToolDef* catTools = isRec ? recommendedTools : tools;
            int catToolsCount = isRec ? dimofi(recommendedTools) : dimofi(tools);
            bool hasTools = false;
            for (int ti = 0; ti < catToolsCount; ti++) {
                if (catTools[ti].category == category) {
                    hasTools = true;
                    break;
                }
            }
            if (!hasTools) continue;
            auto* heading = new RibbonButton();
            heading->font = GetAppFont();
            heading->alignStart = true;
            heading->SetLabel(fmt("%s  −", trans::GetTranslation(toolCategoryNames[category])));
            heading->SetTooltip(trans::GetTranslation(toolCategoryNames[category]));
            heading->pixmap = RibbonIconPixmap(Str(toolCategoryIcons[category]), DpiScale(18), ThemeWindowTextColor(),
                                               RibbonPanelBgColor());
            heading->id = -100 - category;
            heading->onClick = MkFunc1(OnCategory, win);
            SetupRow(win, heading);
            VecAppend(panel->categoryHeaders, heading);
            panel->categoryExpanded[category] = true;
            col->AddChild(heading);
            for (int ti = 0; ti < catToolsCount; ti++) {
                const auto& d = catTools[ti];
                if (d.category != category) continue;
                auto* b = new RibbonButton();
                b->SetLabel(trans::GetTranslation(d.label));
                b->id = d.cmd;
                b->SetTooltip(trans::GetTranslation(d.description));
                b->onClick = MkFunc1(OnTool, win);
                auto* favorite = new RibbonButton();
                favorite->id = d.cmd;
                favorite->compact = true;
                favorite->isSelected = IsToolFavorite(d.cmd);
                favorite->pixmap =
                    RibbonIconPixmap(Str(gLucideStar), DpiScale(18), ThemeWindowTextColor(), RibbonPanelBgColor());
                favorite->SetTooltip(Tr(favorite->isSelected ? "Remove from favorites" : "Add to favorites"));
                favorite->onClick = MkFunc1(OnFavorite, win);
                SetupRow(win, favorite);
                VecAppend(panel->favoriteButtons, favorite);
                auto* row = new HBox();
                row->alignCross = CrossAxisAlign::Stretch;
                row->gap = DpiScale(2);
                row->AddChild(b, 1);
                row->AddChild(favorite);
                VecAppend(panel->rows, b);
                VecAppend(panel->rowDefs, &d);
                VecAppend(panel->sources, (VirtCtrl*)nullptr);
                col->AddChild(row);
            }
        }
        panel->more = new RibbonButton();
        panel->more->font = GetAppFont();
        panel->more->SetLabel(Tr("View more"));
        panel->more->SetTooltip(Tr("View more"));
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
    Insets insets = page == RibbonPage::Home && !panel->rail ? DpiScaledInsets(52, 8, 8, 8) : DpiScaledInsets(8, 8);
    panel->contentPadding = new Padding(col, insets);
    panel->scroll = new ScrollBox(panel->contentPadding);
    if (page == RibbonPage::Home) {
        if (panel->searchEdit) {
            ShowWindow(panel->searchEdit->hwnd, panel->rail ? SW_HIDE : SW_SHOW);
            SetWindowPos(panel->searchEdit->hwnd, nullptr, DpiScale(8), DpiScale(6),
                         std::max(DpiScale(80), ToolsPanelDx() - DpiScale(16)), DpiScale(34),
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
    } else if (panel->searchEdit) {
        ShowWindow(panel->searchEdit->hwnd, SW_HIDE);
    }
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
    if (panel->contentPadding) {
        panel->contentPadding->insets =
            panel->page == (int)RibbonPage::Home && !rail ? DpiScaledInsets(52, 8, 8, 8) : DpiScaledInsets(8, 8);
    }
    auto vis = rail ? Visibility::Collapse : Visibility::Visible;
    panel->header->SetVisibility(vis);
    for (auto* heading : panel->headings) heading->SetVisibility(vis);
    for (auto* heading : panel->categoryHeaders) heading->SetVisibility(vis);
    if (panel->searchEdit && panel->page == (int)RibbonPage::Home)
        ShowWindow(panel->searchEdit->hwnd, rail ? SW_HIDE : SW_SHOW);
    if (panel->more) panel->more->compact = rail;
    for (auto* row : panel->rows) {
        row->compact = rail;
        row->alignStart = !rail;
    }
    panel->host->Relayout();
}

static void PanelNativeMsg(MainWindow* win, VirtHostNativeMsg* ev) {
    if (RibbonAccessMessage(ev)) return;
    auto* panel = win->toolsPanel;
    if (panel && ev->msg == WM_COMMAND && panel->searchEdit && (HWND)ev->lp == panel->searchEdit->hwnd) {
        if (HIWORD(ev->wp) == EN_CHANGE) {
            OnSearchChanged(panel);
            ev->didHandle = true;
            return;
        }
    }
    auto* root = panel ? panel->host->vroot : nullptr;
    if (!root) return;
    if (ev->msg == WM_SETFOCUS && !root->focused) {
        FocusPanelRow(panel);
    } else if (ev->msg == WM_SIZE && panel->searchEdit && panel->page == (int)RibbonPage::Home) {
        RECT r{};
        GetClientRect(panel->host->native, &r);
        int searchWidth = r.right - DpiScale(16);
        if (searchWidth < DpiScale(80)) searchWidth = DpiScale(80);
        SetWindowPos(panel->searchEdit->hwnd, nullptr, DpiScale(8), DpiScale(6), searchWidth, DpiScale(34),
                     SWP_NOACTIVATE | SWP_NOZORDER);
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
    panel->searchEdit = new Edit();
    Edit::CreateArgs searchArgs;
    searchArgs.parent = panel->host->native;
    searchArgs.font = GetAppFont();
    searchArgs.withBorder = true;
    searchArgs.cueText = Tr("Search tools by name or description");
    searchArgs.isRtl = IsUIRtl();
    panel->searchEdit->Create(searchArgs);
    panel->searchEdit->onTextChanged = MkFunc0(OnSearchChanged, panel);
    ShowWindow(panel->searchEdit->hwnd, SW_HIDE);
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
            b->SetTooltip(trans::GetTranslation(d.description));
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
    delete win->toolsPanel->searchEdit;
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
        if (b->GetVisibility() != Visibility::Visible) continue;
        Rect r = b->BoundsInWindow();
        Str category;
        if (panel->page == (int)RibbonPage::Home && i < len(panel->rowDefs))
            category = trans::GetTranslation(toolCategoryNames[panel->rowDefs[i]->category]);
        out.Append(fmt("panel-row=%d cmd=%d enabled=%d checked=%d rect=%d,%d,%d,%d text=%s tip=%s category=%s\n", i,
                       b->id, b->IsEnabled() ? 1 : 0, b->isSelected ? 1 : 0, r.x, r.y, r.Right(), r.Bottom(), b->label,
                       b->tooltip, category));
    }
    for (int i = 0; i < len(panel->categoryHeaders); i++) {
        if (panel->categoryHeaders[i]->GetVisibility() != Visibility::Visible) continue;
        Rect r = panel->categoryHeaders[i]->BoundsInWindow();
        out.Append(fmt("panel-category=%s expanded=%d rect=%d,%d,%d,%d\n", trans::GetTranslation(toolCategoryNames[i]),
                       panel->categoryExpanded[i] ? 1 : 0, r.x, r.y, r.Right(), r.Bottom()));
    }
    for (auto* b : panel->favoriteButtons) {
        if (b->GetVisibility() != Visibility::Visible) continue;
        Rect r = b->BoundsInWindow();
        out.Append(fmt("panel-favorite cmd=%d active=%d rect=%d,%d,%d,%d\n", b->id, b->isSelected ? 1 : 0, r.x, r.y,
                       r.Right(), r.Bottom()));
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
