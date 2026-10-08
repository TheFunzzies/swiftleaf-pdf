/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Swiftleaf ribbon: the Foxit / Acrobat style replacement for the classic
// single-row toolbar. Toolbar.cpp owns the host window and the per-button
// state; this file owns the ribbon's look (controls, colors, icons) and its
// content (which pages exist and which commands each one shows).

struct PlatformFont;
struct Pixmap;

// a page (tab) of the ribbon
enum class RibbonPage {
    Home = 0,
    Comment,
    Edit,
    Organize,
    FormsSign,
    View,
    Count,
};

// one button on a ribbon page; a null icon with cmdId 0 is a group separator
struct RibbonItemDef {
    const char* icon = nullptr;
    int cmdId = 0;
    Str label; // short, shown under the icon (translated)
    Str tip;   // tooltip; label when empty (translated)
};

int RibbonPageCount();
Str RibbonPageName(RibbonPage);
// buttons of the pages built purely from RibbonItemDef (Edit, Organize, ...)
void RibbonPageItems(RibbonPage, const RibbonItemDef** items, int* count);
// pages whose tools edit the PDF: switching to them turns on edit mode
bool RibbonPageEditsPdf(RibbonPage);
// short label for a button of the Home / Comment pages, which reuse the
// toolbar's own button tables
Str RibbonLabelForCmd(int cmdId);

// the ribbon's own icon for a command, or fallback when it has none
const char* RibbonIconForCmd(int cmdId, const char* fallback);

Color RibbonAccentColor();
Color RibbonTabRowBgColor();
Color RibbonPanelBgColor();
Color RibbonSelectedBgColor();
Color RibbonEdgeColor();
Color RibbonIconColor(int cmdId);
int RibbonLargeIconSize();
// height of a RibbonButton / RibbonTab drawn in this font
int RibbonButtonDy(PlatformFont*);
int RibbonTabDy(PlatformFont*);
// an SVG icon drawn with a heavier stroke, which reads better at ribbon size
Pixmap* RibbonIconPixmap(Str svg, int size, Color fg, Color bg);

// a large button: icon on top, short label underneath
struct RibbonButton : VirtIconButton {
    Str label; // owned
    PlatformFont* font = nullptr;
    Color textColor = kColorUnset;
    Color textColorDisabled = kColorUnset;
    // too little room for the page: icon only (the label is in the tooltip)
    bool compact = false;

    RibbonButton();
    ~RibbonButton() override;

    void SetLabel(Str);
    Size GetIdealSize() override;
    void Paint(VirtPaintCtx&) override;
};

RibbonButton* AsRibbonButton(ILayout*);

// Wraps a ribbon page's row of buttons. When the row doesn't fit the width it
// is given, its buttons drop their labels (RibbonButton::compact).
struct RibbonPanelFit : Padding {
    explicit RibbonPanelFit(ILayout* row);
    Size Layout(Constraints bc) override;
};

// a page tab in the ribbon's tab row
struct RibbonTab : VirtCtrl {
    Str label; // owned
    PlatformFont* font = nullptr;
    bool isActive = false;
    Color textColor = kColorUnset;

    RibbonTab(Str label, PlatformFont*);
    ~RibbonTab() override;

    Size GetIdealSize() override;
    void Paint(VirtPaintCtx&) override;
    void OnMouseEnter();
    void OnMouseLeave();
};

RibbonTab* AsRibbonTab(ILayout*);
