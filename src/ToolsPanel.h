/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// PDFCraft's "All tools" panel on the left of the window: one row per task
// (Export a PDF, Fill & Sign, ...), each running an existing command or
// showing the ribbon page with the tools for it. Also the top bar's Menu.

struct MainWindow;

void CreateToolsPanel(MainWindow*);
void DestroyToolsPanel(MainWindow*);
void ToolsPanelUpdate(MainWindow*);
void ToolsPanelAfterThemeChange(MainWindow*);
void ToolsPanelShowPage(MainWindow*);
void ToolsPanelSetRail(MainWindow*, bool rail);
void ShowToolsPanelMenu(MainWindow*, Rect anchor, bool allPages = false);
Rect ToolsPanelButtonRect(MainWindow*, int cmdId);
TempStr ToolsPanelStateTemp(MainWindow*);
bool ShouldShowToolsPanel(MainWindow*);
void ToggleToolsPanel(MainWindow*);
int ToolsPanelDx();
void ShowTopBarMenu(MainWindow*, Rect anchorScreen);

void PositionAnnotationStrip(MainWindow*);
