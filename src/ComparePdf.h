/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#ifndef COMPAREPDF_H
#define COMPAREPDF_H

struct MainWindow;

struct CompareProgress {
    volatile LONG cancelled = 0;
    volatile LONG completed = 0;
    volatile LONG total = 0;
};

bool ComparePdfReport(Str leftPath, Str rightPath, Str reportPath, Str* resultJsonOut = nullptr,
                      CompareProgress* progress = nullptr);
void ShowComparePdfDialog(MainWindow* win);

#endif
