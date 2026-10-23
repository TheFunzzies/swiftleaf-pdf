/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

struct MainWindow;

void ShowOcrDialog(MainWindow*);
#if IS_DEBUG
bool Ocr_UnitTests();
#endif
