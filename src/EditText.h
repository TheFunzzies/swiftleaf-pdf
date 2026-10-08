/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

struct MainWindow;

void StartEditText(MainWindow*);
void CancelEditText();
// the canvas asks first: while Edit Text waits for a click on a line
bool EditTextOnLeftDown(MainWindow*, Point pt);
bool EditTextOnSetCursor(MainWindow*);
