/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "gui/Dpi.h"
#include "base/Pixmap.h"

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
#include "DisplayMode.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "Commands.h"
#include "SvgIcons.h"
#include "Theme.h"
#include "Translations.h"
#include "Material.h"
#include "Ribbon.h"
#include "Toolbar.h"
#include "StatusBar.h"

#define TABLER_SVG(body)                                                                                            \
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" stroke-width=\"1\" " \
    "stroke=\"currentColor\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" body "</svg>"

// chevron-left / -right, chevrons-left / -right (Tabler, MIT)
static const char* gIconPrev = TABLER_SVG(R"(<path d="M15 6l-6 6l6 6" />)");
static const char* gIconNext = TABLER_SVG(R"(<path d="M9 6l6 6l-6 6" />)");
static const char* gIconFirst = TABLER_SVG(R"(<path d="M11 7l-5 5l5 5" /><path d="M17 7l-5 5l5 5" />)");
static const char* gIconLast = TABLER_SVG(R"(<path d="M7 7l5 5l-5 5" /><path d="M13 7l5 5l-5 5" />)");
static const char* gIconFacing =
    TABLER_SVG(R"(<rect x="3" y="4" width="18" height="16" rx="2" /><path d="M12 4v16" />)");
static const char* gIconFitWidth = TABLER_SVG(
    R"(<path d="M4 12v-6a2 2 0 0 1 2 -2h12a2 2 0 0 1 2 2v6" /><path d="M10 18h-7" /><path d="M21 18h-7" />)"
    R"(<path d="M6 15l-3 3l3 3" /><path d="M18 15l3 3l-3 3" />)");
static const char* gIconFitPage = TABLER_SVG(
    R"(<path d="M12 20h-6a2 2 0 0 1 -2 -2v-12a2 2 0 0 1 2 -2h6" /><path d="M18 4v17" />)"
    R"(<path d="M15 18l3 3l3 -3" /><path d="M15 7l3 -3l3 3" />)");

struct StatusBarButtonDef {
    const char* icon;
    int cmdId;
    Str tip;
};

// clang-format off
static StatusBarButtonDef gNavButtons[] = {
    {gIconFirst, CmdGoToFirstPage, TrN("First Page")},
    {gIconPrev, CmdGoToPrevPage, TrN("Previous Page")},
    {nullptr, 0, {}}, // the page text
    {gIconNext, CmdGoToNextPage, TrN("Next Page")},
    {gIconLast, CmdGoToLastPage, TrN("Last Page")},
};

static StatusBarButtonDef gViewButtons[] = {
    {nullptr, CmdSinglePageView, TrN("Single Page")},
    {nullptr, CmdFacingView, TrN("Two Pages")},
    {nullptr, CmdToggleContinuousView, TrN("Continuous")},
    {nullptr, 0, {}}, // separator
    {nullptr, CmdZoomFitWidth, TrN("Fit Width")},
    {nullptr, CmdZoomFitPage, TrN("Fit Page")},
    {nullptr, 0, {}}, // separator
    {nullptr, CmdZoomOut, TrN("Zoom Out")},
};
// clang-format on

// the zoom slider runs from 10% to 640% on a log scale
constexpr float kSliderMinZoom = 10.f;
constexpr float kSliderZoomRange = 64.f;
constexpr int kSliderSteps = 100;

struct StatusBarVirt {
    MainWindow* win = nullptr;
    VirtHost* host = nullptr;
    PlatformFont* font = nullptr;
    VirtText* pageText = nullptr;
    VirtButton* zoomText = nullptr;
    VirtSlider* zoomSlider = nullptr;
    Vec<VirtIconButton*> buttons;
    int dy = 0;
};

static const WStr kStatusBarClass = WStrL(L"SWIFTLEAF_STATUS_BAR");

static Color StatusBgColor() {
    return RibbonPanelBgColor();
}

static const char* IconForCmd(int cmdId) {
    switch (cmdId) {
        case CmdSinglePageView:
            return gIconLayoutSinglePage;
        case CmdFacingView:
            return gIconFacing;
        case CmdToggleContinuousView:
            return gIconLayoutContinuous;
        case CmdZoomFitWidth:
            return gIconFitWidth;
        case CmdZoomFitPage:
            return gIconFitPage;
        case CmdZoomOut:
            return gIconZoomOut;
        case CmdZoomIn:
            return gIconZoomIn;
    }
    return nullptr;
}

static void OnButtonClicked(StatusBarVirt* sb, VirtMouseEvent* ev) {
    VirtCtrl* w = ev->target;
    if (!w || !w->IsEnabled() || w->id == 0) {
        return;
    }
    HwndPostCommand(sb->win->hwndFrame, w->id);
    ev->didHandle = true;
}

static VirtIconButton* NewButton(StatusBarVirt* sb, const char* icon, int cmdId, Str tip) {
    int sz = DpiScale(16);
    auto* b = new VirtIconButton();
    int pad = DpiScale(5);
    b->padding = {pad, pad + 1, pad, pad + 1};
    Color bg = StatusBgColor();
    Str svg = Str(icon);
    b->pixmap = RibbonIconPixmap(svg, sz, M3().onSurfaceVariant, bg);
    b->pixmapDisabled = RibbonIconPixmap(svg, sz, M3StateLayer(bg, M3().onSurface, 38), bg);
    b->SetColor(kColIconBtnBgHover, M3StateLayer(bg, M3().onSurface, kM3HoverOpacity));
    b->SetColor(kColIconBtnBgSelected, RibbonSelectedBgColor());
    b->id = cmdId;
    b->SetTooltip(trans::GetTranslation(tip));
    b->onClick = MkFunc1(OnButtonClicked, sb);
    VecAppend(sb->buttons, b);
    return b;
}

static void PaintSeparator(VirtCustom*, VirtPaintCtx* ctx) {
    Rect r = ctx->bounds;
    int inset = DpiScale(6);
    ctx->gfx->FillRect({r.x + (r.dx / 2), r.y + inset, 1, r.dy - (2 * inset)}, RibbonEdgeColor());
}

static VirtCtrl* NewSeparator(int dy) {
    auto* sep = new VirtCustom();
    sep->idealSize = {DpiScale(10), dy};
    sep->onPaint = MkFunc1(PaintSeparator, sep);
    sep->SetFlag(vwfNoHitTest, true);
    return sep;
}

static float SliderToZoom(int v) {
    return kSliderMinZoom * powf(kSliderZoomRange, (float)v / kSliderSteps);
}

static int ZoomToSlider(float zoom) {
    if (zoom <= kSliderMinZoom) {
        return 0;
    }
    float v = logf(zoom / kSliderMinZoom) / logf(kSliderZoomRange) * kSliderSteps;
    return limitValue((int)(v + 0.5f), 0, kSliderSteps);
}

static void OnZoomSliderChanged(StatusBarVirt* sb) {
    MainWindow* win = sb->win;
    if (!win->IsDocLoaded() || !sb->zoomSlider) {
        return;
    }
    float zoom = SliderToZoom(sb->zoomSlider->value);
    win->ctrl->SetZoomVirtual(zoom, nullptr);
    UpdateToolbarState(win);
}

static void OnZoomTextClicked(StatusBarVirt* sb, VirtMouseEvent* ev) {
    HwndPostCommand(sb->win->hwndFrame, CmdZoomCustom);
    ev->didHandle = true;
}

static void PaintBackground(StatusBarVirt*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, StatusBgColor());
    Rect rc = ev->clientRect;
    ev->gfx->FillRect({rc.x, rc.y, rc.dx, 1}, RibbonEdgeColor());
}

static void BuildLayout(StatusBarVirt* sb) {
    VecReset(sb->buttons);
    int rowDy = sb->dy;
    Color fg = M3().onSurfaceVariant;

    auto* left = new HBox();
    left->alignCross = CrossAxisAlign::CrossCenter;
    for (const StatusBarButtonDef& d : gNavButtons) {
        if (!d.icon) {
            sb->pageText = new VirtText(StrL(" "), sb->font);
            sb->pageText->SetColor(kColText, fg);
            sb->pageText->padding = {0, DpiScale(8), 0, DpiScale(8)};
            left->AddChild(sb->pageText);
            continue;
        }
        left->AddChild(NewButton(sb, d.icon, d.cmdId, d.tip));
    }

    auto* right = new HBox();
    right->alignCross = CrossAxisAlign::CrossCenter;
    for (const StatusBarButtonDef& d : gViewButtons) {
        if (d.cmdId == 0) {
            right->AddChild(NewSeparator(rowDy));
            continue;
        }
        right->AddChild(NewButton(sb, IconForCmd(d.cmdId), d.cmdId, d.tip));
    }
    auto* slider = new VirtSlider();
    slider->minVal = 0;
    slider->maxVal = kSliderSteps;
    slider->idealDx = DpiScale(110);
    slider->onValueChanged = MkFunc0(OnZoomSliderChanged, sb);
    slider->SetTooltip(Tr("Zoom"));
    sb->zoomSlider = slider;
    right->AddChild(slider);
    right->AddChild(NewButton(sb, IconForCmd(CmdZoomIn), CmdZoomIn, TrN("Zoom In")));

    auto* zoomText = new VirtButton(StrL("100%"), sb->font);
    zoomText->textPadding = {DpiScale(2), DpiScale(8), DpiScale(2), DpiScale(8)};
    zoomText->SetColor(kColBtnBg, kColorTransparent);
    zoomText->SetColor(kColBtnBorder, kColorTransparent);
    zoomText->SetColor(kColBtnBgHover, M3StateLayer(StatusBgColor(), M3().onSurface, kM3HoverOpacity));
    zoomText->SetColor(kColBtnText, M3().onSurface);
    zoomText->SetTooltip(Tr("Custom zoom"));
    zoomText->onClick = MkFunc1(OnZoomTextClicked, sb);
    sb->zoomText = zoomText;
    right->AddChild(zoomText);

    auto* row = new HBox();
    row->alignCross = CrossAxisAlign::CrossCenter;
    row->AddChild(left);
    row->AddChild(new VirtSpacer(0, 0), 1);
    row->AddChild(right);
    sb->host->SetLayout(new Padding(row, Insets{1, DpiScale(8), 0, DpiScale(8)}));
}

bool ShouldShowStatusBar(MainWindow* win) {
    return win && win->statusBar && !win->presentation && !win->isFullScreen && !win->isQuickLook;
}

void CreateStatusBar(MainWindow* win) {
    if (win->statusBar) {
        return;
    }
    auto* sb = new StatusBarVirt();
    sb->win = win;
    sb->dy = DpiScale(30);
    sb->font = GetScaledPlatformFont(GetAppFont(), 85);

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = kStatusBarClass;
    args.initialSize = {100, sb->dy};
    args.bgColor = StatusBgColor();
    args.isRtl = IsUIRtl();
    args.visible = true;
    args.noActivate = true;
    args.userData = win;
    sb->host = VirtHost::Create(args);
    if (!sb->host) {
        delete sb;
        return;
    }
    sb->host->onPaintBackground = MkFunc1(PaintBackground, sb);
    sb->host->SetFont(sb->font);
    win->statusBar = sb;
    win->hwndStatusBar = sb->host->native;
    BuildLayout(sb);
    StatusBarUpdate(win);
}

// after a theme change: new colors and icons for everything
void StatusBarAfterThemeChange(MainWindow* win) {
    StatusBarVirt* sb = win ? win->statusBar : nullptr;
    if (!sb) {
        return;
    }
    sb->host->bgColor = StatusBgColor();
    BuildLayout(sb);
    sb->host->Relayout();
    StatusBarUpdate(win);
}

void DestroyStatusBar(MainWindow* win) {
    StatusBarVirt* sb = win ? win->statusBar : nullptr;
    if (!sb) {
        return;
    }
    win->statusBar = nullptr;
    win->hwndStatusBar = nullptr;
    delete sb->host;
    delete sb;
}

static bool IsCmdChecked(DocController* ctrl, int cmdId) {
    if (!ctrl) {
        return false;
    }
    DisplayMode dm = ctrl->GetDisplayMode();
    switch (cmdId) {
        case CmdSinglePageView:
            return IsSingle(dm);
        case CmdFacingView:
            return IsFacing(dm);
        case CmdToggleContinuousView:
            return IsContinuous(dm);
        case CmdZoomFitWidth:
            return ctrl->GetZoomVirtual() == kZoomFitWidth;
        case CmdZoomFitPage:
            return ctrl->GetZoomVirtual() == kZoomFitPage;
    }
    return false;
}

void StatusBarUpdate(MainWindow* win) {
    StatusBarVirt* sb = win ? win->statusBar : nullptr;
    if (!sb) {
        return;
    }
    DocController* ctrl = win->IsDocLoaded() ? win->ctrl : nullptr;
    bool relayout = false;

    TempStr page = ctrl ? fmt("%d / %d", ctrl->CurrentPageNo(), ctrl->PageCount()) : TempStr(StrL(" "));
    if (!str::Eq(sb->pageText->s, page)) {
        sb->pageText->SetText(page);
        relayout = true;
    }
    float zoom = ctrl ? ctrl->GetZoomVirtual(true) : 100.f;
    TempStr zoomStr = ctrl ? fmt("%d%%", (int)(zoom + 0.5f)) : TempStr(StrL(" "));
    if (!str::Eq(sb->zoomText->s, zoomStr)) {
        sb->zoomText->SetText(zoomStr);
        relayout = true;
    }
    if (ctrl && !sb->zoomSlider->IsAdjusting()) {
        sb->zoomSlider->SetValue(ZoomToSlider(zoom), false);
    }
    sb->zoomSlider->SetIsEnabled(ctrl != nullptr);
    sb->zoomText->SetIsEnabled(ctrl != nullptr);

    for (VirtIconButton* b : sb->buttons) {
        bool enabled = ctrl != nullptr;
        if (ctrl && b->id == CmdGoToPrevPage) {
            enabled = ctrl->CurrentPageNo() > 1;
        } else if (ctrl && b->id == CmdGoToNextPage) {
            enabled = ctrl->CurrentPageNo() < ctrl->PageCount();
        } else if (ctrl && b->id == CmdGoToFirstPage) {
            enabled = ctrl->CurrentPageNo() > 1;
        } else if (ctrl && b->id == CmdGoToLastPage) {
            enabled = ctrl->CurrentPageNo() < ctrl->PageCount();
        }
        bool checked = IsCmdChecked(ctrl, b->id);
        if (b->IsEnabled() != enabled || b->isSelected != checked) {
            b->SetIsEnabled(enabled);
            b->isSelected = checked;
            b->Invalidate();
        }
    }
    if (relayout) {
        if (sb->host->vroot) {
            sb->host->vroot->RequestLayout();
        }
        sb->host->Relayout();
    }
    sb->host->Invalidate(true);
}
