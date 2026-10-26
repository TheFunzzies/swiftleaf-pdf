/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#ifndef COMPAREPDF_H
#define COMPAREPDF_H

struct MainWindow;
struct PdfWorkProgress;

bool ComparePdfReport(Str leftPath, Str rightPath, Str reportPath, Str* resultJsonOut = nullptr,
                      PdfWorkProgress* progress = nullptr);
void ShowComparePdfDialog(MainWindow* win);

#endif
