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
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Commands.h"
#include "SvgIcons.h"
#include "Theme.h"
#include "Translations.h"
#include "SidebarPanel.h"
#include "AnnotFilterToolbar.h"
#include "FindBar.h"
#include "Material.h"
#include "Ribbon.h"
#include "NavRail.h"

#define TABLER_SVG(body)                                                                                            \
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" stroke-width=\"1\" " \
    "stroke=\"currentColor\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" body "</svg>"

// Tabler icons (MIT): layout-grid, bookmark, message, star, settings
static const char* gIconThumbs = TABLER_SVG(
    R"(<rect x="4" y="4" width="6" height="6" rx="1" /><rect x="14" y="4" width="6" height="6" rx="1" />)"
    R"(<rect x="4" y="14" width="6" height="6" rx="1" /><rect x="14" y="14" width="6" height="6" rx="1" />)");
static const char* gIconBookmark =
    TABLER_SVG(R"(<path d="M18 7v14l-6 -4l-6 4v-14a4 4 0 0 1 4 -4h4a4 4 0 0 1 4 4z" />)");
static const char* gIconComments = TABLER_SVG(
    R"(<path d="M8 9h8" /><path d="M8 13h6" />)"
    R"(<path d="M18 4a3 3 0 0 1 3 3v8a3 3 0 0 1 -3 3h-5l-5 3v-3h-2a3 3 0 0 1 -3 -3v-8a3 3 0 0 1 3 -3h12z" />)");
static const char* gIconStar = TABLER_SVG(
    R"(<path d="M12 17.75l-6.172 3.245l1.179 -6.873l-5 -4.867l6.9 -1l3.086 -6.253l3.086 6.253l6.9 1l-5 4.867l1.179 6.873z" />)");
static const char* gIconSettings = TABLER_SVG(
    R"(<path d="M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 -2.924 1.756 -3.35 0a1.724 1.724 0 0 0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 -1.065 -2.572c-1.756 -.426 -1.756 -2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 -2.37c1 .608 2.296 .07 2.572 -1.065z" />)"
    R"(<path d="M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0" />)");

enum class NavItemKind {
    Thumbnails,
    Bookmarks,
    Comments,
    Search,
    Favorites,
    Settings,
};

struct NavItemDef {
    NavItemKind kind;
    const char* icon;
    int cmdId;
    Str label;
    Str tip;
    bool needsDoc;
    bool atBottom;
};

// clang-format off
// Foxit order: bookmarks first, then pages; icons only, the name is the tooltip
static const NavItemDef gNavItems[] = {
    {NavItemKind::Bookmarks, gIconBookmark, CmdToggleBookmarks, TrN("Bookmarks"), TrN("Bookmarks"), true, false},
    {NavItemKind::Thumbnails, gIconThumbs, CmdToggleThumbnails, TrN("Pages"), TrN("Page thumbnails"), true, false},
    {NavItemKind::Comments, gIconComments, CmdFindAnnotation, TrN("Comments"), TrN("All comments in the PDF"), true, false},
    {NavItemKind::Search, nullptr, CmdFindFirst, TrN("Search"), TrN("Find text"), true, false},
    {NavItemKind::Favorites, gIconStar, CmdFavoriteToggle, TrN("Favorites"), TrN("Favorite documents and pages"), false, false},
    {NavItemKind::Settings, gIconSettings, CmdOptions, TrN("Settings"), TrN("Options"), false, true},
};
// clang-format on

// a compact icon rail like Foxit's, with Material icon-button shapes, in dp
constexpr int kRailDx = 52;
constexpr int kIndicatorDx = 40;
constexpr int kIndicatorDy = 40;
constexpr int kIndicatorRadius = 24; // FillRoundedRect wants the diameter
constexpr int kIconSize = 22;
constexpr int kItemDy = 48;
constexpr int kRailTopPad = 8;

static Kind kindNavRailItem = "navRailItem";

struct NavRailItem : VirtCtrl {
    const NavItemDef* def = nullptr;
    PlatformFont* font = nullptr;
    Pixmap* icon = nullptr;
    Pixmap* iconActive = nullptr;
    Pixmap* iconDisabled = nullptr;
    bool isActive = false;

    NavRailItem() {
        kind = kindNavRailItem;
        cursor = CursorId::Hand;
        onMouseEnter = MkMethod0<NavRailItem, &NavRailItem::OnHoverChanged>(this);
        onMouseLeave = MkMethod0<NavRailItem, &NavRailItem::OnHoverChanged>(this);
    }

    void OnHoverChanged() {
        Invalidate();
    }

    Size GetIdealSize() override {
        return {DpiScale(kRailDx), DpiScale(kItemDy)};
    }

    void Paint(VirtPaintCtx& ctx) override {
        const M3Scheme& m3 = M3();
        Rect r = ctx.bounds;
        bool enabled = IsEnabled();
        int indDx = DpiScale(kIndicatorDx);
        int indDy = DpiScale(kIndicatorDy);
        Rect ind{r.x + (r.dx - indDx) / 2, r.y + (r.dy - indDy) / 2, indDx, indDy};

        // the active item's pill, with hover / press as state layers on it
        Color base = RibbonPanelBgColor();
        Color fill = isActive ? m3.secondaryContainer : base;
        if (enabled && HasFlag(vwfPressed)) {
            fill = M3StateLayer(fill, m3.onSurface, kM3PressedOpacity);
        } else if (enabled && HasFlag(vwfHovered)) {
            fill = M3StateLayer(fill, m3.onSurface, kM3HoverOpacity);
        }
        if (fill != base) {
            ctx.gfx->FillRoundedRect(ind, DpiScale(kIndicatorRadius), fill);
        }

        Pixmap* px = !enabled ? iconDisabled : (isActive ? iconActive : icon);
        if (px) {
            ctx.gfx->DrawPixmap(px, {ind.x + (ind.dx - px->width) / 2, ind.y + (ind.dy - px->height) / 2, px->width,
                                     px->height});
        }
    }
};

struct NavRailVirt {
    MainWindow* win = nullptr;
    VirtHost* host = nullptr;
    PlatformFont* font = nullptr;
    Vec<NavRailItem*> items;
};

static const WStr kNavRailClass = WStrL(L"SWIFTLEAF_NAV_RAIL");

int NavRailDx() {
    return DpiScale(kRailDx);
}

bool ShouldShowNavRail(MainWindow* win) {
    return win && win->navRail && !win->presentation && !win->isFullScreen && !win->isQuickLook;
}

static void OnItemClicked(NavRailVirt* rail, VirtMouseEvent* ev) {
    VirtCtrl* w = ev->target;
    if (!w || !w->IsEnabled()) {
        return;
    }
    HwndPostCommand(rail->win->hwndFrame, w->id);
    ev->didHandle = true;
}

static void PaintBackground(NavRailVirt*, VirtHostPaintEvent* ev) {
    Rect rc = ev->clientRect;
    ev->gfx->FillRect(rc, RibbonPanelBgColor());
    // a divider between the rail and the panels / document
    ev->gfx->FillRect({rc.Right() - 1, rc.y, 1, rc.dy}, RibbonEdgeColor());
}

static void SetItemIcons(NavRailItem* item) {
    const M3Scheme& m3 = M3();
    int sz = DpiScale(kIconSize);
    Str svg = Str(item->def->icon ? item->def->icon : gIconSearch);
    item->icon = RibbonIconPixmap(svg, sz, m3.onSurfaceVariant, RibbonPanelBgColor());
    item->iconActive = RibbonIconPixmap(svg, sz, m3.onSecondaryContainer, m3.secondaryContainer);
    item->iconDisabled = RibbonIconPixmap(svg, sz, M3StateLayer(RibbonPanelBgColor(), m3.onSurface, 38),
                                          RibbonPanelBgColor());
}

static void BuildLayout(NavRailVirt* rail) {
    VecReset(rail->items);
    auto* top = new VBox();
    top->alignCross = CrossAxisAlign::CrossCenter;
    auto* bottom = new VBox();
    bottom->alignCross = CrossAxisAlign::CrossCenter;
    for (const NavItemDef& d : gNavItems) {
        auto* item = new NavRailItem();
        item->def = &d;
        item->font = rail->font;
        item->id = d.cmdId;
        item->SetTooltip(trans::GetTranslation(d.label));
        item->onClick = MkFunc1(OnItemClicked, rail);
        SetItemIcons(item);
        VecAppend(rail->items, item);
        (d.atBottom ? bottom : top)->AddChild(item);
    }
    auto* col = new VBox();
    col->alignCross = CrossAxisAlign::Stretch;
    col->AddChild(new Padding(top, Insets{DpiScale(kRailTopPad), 0, 0, 0}));
    col->AddChild(new VirtSpacer(0, 0), 1);
    col->AddChild(new Padding(bottom, Insets{0, 0, DpiScale(8), 0}));
    rail->host->SetLayout(col);
}

void CreateNavRail(MainWindow* win) {
    if (win->navRail) {
        return;
    }
    auto* rail = new NavRailVirt();
    rail->win = win;
    // Material's label-medium: a notch smaller than the body text
    rail->font = GetScaledPlatformFont(GetAppFont(), 80);

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = kNavRailClass;
    args.initialSize = {NavRailDx(), 100};
    args.bgColor = RibbonPanelBgColor();
    args.isRtl = IsUIRtl();
    args.visible = true;
    args.noActivate = true;
    args.userData = win;
    rail->host = VirtHost::Create(args);
    if (!rail->host) {
        delete rail;
        return;
    }
    rail->host->onPaintBackground = MkFunc1(PaintBackground, rail);
    rail->host->SetFont(rail->font);
    win->navRail = rail;
    win->hwndNavRail = rail->host->native;
    BuildLayout(rail);
    NavRailUpdate(win);
}

// after a theme change: new colors and icons for every item
void NavRailAfterThemeChange(MainWindow* win) {
    NavRailVirt* rail = win ? win->navRail : nullptr;
    if (!rail) {
        return;
    }
    rail->host->bgColor = RibbonPanelBgColor();
    for (NavRailItem* item : rail->items) {
        SetItemIcons(item);
    }
    rail->host->Invalidate(true);
    NavRailUpdate(win);
}

void DestroyNavRail(MainWindow* win) {
    NavRailVirt* rail = win ? win->navRail : nullptr;
    if (!rail) {
        return;
    }
    win->navRail = nullptr;
    win->hwndNavRail = nullptr;
    delete rail->host;
    delete rail;
}

static bool IsItemActive(MainWindow* win, NavItemKind kind) {
    switch (kind) {
        case NavItemKind::Thumbnails:
            return IsSidebarViewShown(win, SidebarView::Thumbnails);
        case NavItemKind::Bookmarks:
            return IsSidebarViewShown(win, SidebarView::Bookmarks);
        case NavItemKind::Favorites:
            return IsSidebarViewShown(win, SidebarView::Favorites);
        case NavItemKind::Comments:
            return IsFloatingAnnotListVisible(win);
        case NavItemKind::Search:
            return IsFindBarVisible(win);
        case NavItemKind::Settings:
            return false;
    }
    return false;
}

void NavRailUpdate(MainWindow* win) {
    NavRailVirt* rail = win ? win->navRail : nullptr;
    if (!rail) {
        return;
    }
    bool docLoaded = win->IsDocLoaded();
    for (NavRailItem* item : rail->items) {
        bool active = IsItemActive(win, item->def->kind);
        bool enabled = !item->def->needsDoc || docLoaded;
        if (item->def->kind == NavItemKind::Comments) {
            enabled = enabled && win->CurrentTab() && IsPdfDoc(win->CurrentTab());
        }
        if (item->isActive == active && item->IsEnabled() == enabled) {
            continue;
        }
        item->isActive = active;
        item->SetIsEnabled(enabled);
        item->Invalidate();
    }
}
