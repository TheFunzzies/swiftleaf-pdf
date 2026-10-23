/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

constexpr int kToolsMinWidth = 180;
constexpr int kCommentsMinWidth = 220;
constexpr int kPanelMaxWidth = 480;
constexpr int kToolsRailWidth = 52;
constexpr int kPanelHandleWidth = 5;
constexpr int kDocumentMinWidth = 320;

struct PanelWidths {
    int tools = 0;
    int comments = 0;
    bool rail = false;
};

int ClampPanelWidth(int width, int minimum);
PanelWidths CalcPanelWidths(int available, int dpi, int tools, int comments);
