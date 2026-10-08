/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Swiftleaf navigation rail: a Material 3 rail on the left edge of the window
// that opens the side panels (thumbnails, bookmarks, comments, favorites) and
// search, like the left icon strip of Acrobat / Foxit.

struct MainWindow;

void CreateNavRail(MainWindow*);
void DestroyNavRail(MainWindow*);
void NavRailUpdate(MainWindow*);
void NavRailAfterThemeChange(MainWindow*);
bool ShouldShowNavRail(MainWindow*);
int NavRailDx();
