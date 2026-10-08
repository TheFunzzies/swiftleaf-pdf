/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Swiftleaf status bar, under the document like Foxit / Acrobat: page
// navigation on the left; view modes, fit buttons and zoom on the right.

struct MainWindow;

void CreateStatusBar(MainWindow*);
void DestroyStatusBar(MainWindow*);
void StatusBarUpdate(MainWindow*);
bool ShouldShowStatusBar(MainWindow*);
