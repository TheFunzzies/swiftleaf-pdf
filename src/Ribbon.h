/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Swiftleaf ribbon: the Foxit / Acrobat style replacement for the classic
// single-row toolbar. Toolbar.cpp owns the host window and the per-button
// state; this file owns the ribbon's look (controls, colors, icons) and its
// content (which pages exist and which commands each one shows).

struct PlatformFont;
struct Pixmap;

// a page (tab) of the ribbon. The values index the panels; the tab row shows
// them in Foxit's order (RibbonPageAtTab)
enum class RibbonPage {
    Home = 0,
    Comment,
    Edit,
    Organize,
    FormsSign,
    View,
    Protect,
    Help,
    File,
    Count,
};

// PDFCraft's mode tabs in the top bar. Each shows one ribbon page; All tools
// shows none (its tools are in the All tools panel). The other pages are in
// the Menu button's drop-down
enum class RibbonMode {
    AllTools = 0,
    Read,
    Edit,
    Convert,
    ESign,
    Count,
};

int RibbonModeCount();
Str RibbonModeName(RibbonMode);
RibbonPage RibbonModePage(RibbonMode);
// the mode whose tab is active while a page is shown, -1 for none
int RibbonModeForPage(RibbonPage);
// the page's tools row is shown under the top bar
bool RibbonPageHasPanel(RibbonPage);

// one button on a ribbon page; a null icon with cmdId 0 is a group separator
struct RibbonItemDef {
    const char* icon = nullptr;
    int cmdId = 0;
    Str label; // short, shown under the icon (translated)
    Str tip;   // tooltip; label when empty (translated)
};

int RibbonPageCount();
Str RibbonPageName(RibbonPage);
TempStr RibbonCommandHelpTemp(int cmdId, Str* pageOut);
RibbonPage RibbonPageAtTab(int tabPos);
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
int RibbonSmallIconSize();
// height of a RibbonButton / RibbonTab drawn in this font
int RibbonButtonDy(PlatformFont*);
int RibbonTabDy(PlatformFont*);
// an SVG icon drawn with a heavier stroke, which reads better at ribbon size
Pixmap* RibbonIconPixmap(Str svg, int size, Color fg, Color bg);

// a tool button: icon with a short label beside it
struct AnimatedColor;
struct Animation;

struct RibbonButton : VirtIconButton {
    AnimatedColor* animatedBg = nullptr;
    Str label; // owned
    PlatformFont* font = nullptr;
    Color textColor = kColorUnset;
    Color textColorDisabled = kColorUnset;
    // too little room for the page: icon only (the label is in the tooltip)
    bool compact = false;
    bool alignStart = false;
    bool isRtl = false;

    RibbonButton();
    ~RibbonButton() override;

    void SetLabel(Str);
    Size GetIdealSize() override;
    void Paint(VirtPaintCtx&) override;
};

RibbonButton* AsRibbonButton(ILayout*);

// Retains as many button labels as the available width allows.
struct RibbonPanelFit : Padding {
    explicit RibbonPanelFit(ILayout* row);
    Size Layout(Constraints bc) override;
};

// a page tab in the ribbon's tab row
struct RibbonTab : VirtCtrl {
    AnimatedColor* animatedBg = nullptr;
    Str label; // owned
    PlatformFont* font = nullptr;
    bool isActive = false;
    Color textColor = kColorUnset;
    int paddingDx = 0;

    RibbonTab(Str label, PlatformFont*);
    ~RibbonTab() override;

    Size GetIdealSize() override;
    void Paint(VirtPaintCtx&) override;
    void OnMouseEnter();
    void OnMouseLeave();
};

RibbonTab* AsRibbonTab(ILayout*);

struct RibbonTabRow : HBox {
    Animation* underlineX = nullptr;
    Animation* underlineWidth = nullptr;
    Size paintedSize;
    VirtCtrl* overflowBtn = nullptr;
    ILayout* searchBox = nullptr;
    ILayout* navigationBox = nullptr;
    VirtCtrl* propsBtn = nullptr;
    VirtCtrl* saveBtn = nullptr;
    VirtCtrl* printBtn = nullptr;
    RibbonTabRow();
    ~RibbonTabRow() override;
    Size Layout(Constraints bc) override;
    void PaintUnderline(Gfx*, HWND);
};

// PDFCraft's "Find tools and commands" field. It only looks like an edit: a
// click opens the command palette, which does the searching
struct RibbonSearchBox : VirtCtrl {
    Str placeholder; // owned
    Str shortcut;    // owned, e.g. "Ctrl+K"
    PlatformFont* font = nullptr;
    Pixmap* icon = nullptr;

    RibbonSearchBox(Str placeholder, Str shortcut, PlatformFont*);
    ~RibbonSearchBox() override;

    Size GetIdealSize() override;
    void Paint(VirtPaintCtx&) override;
    void OnHoverChanged();
};

RibbonSearchBox* AsRibbonSearchBox(ILayout*);
