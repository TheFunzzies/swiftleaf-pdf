/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#ifndef ACCESSIBILITY_H
#define ACCESSIBILITY_H


struct MainWindow;
struct fz_context;
struct pdf_document;
struct fz_pixmap;

enum class AccessCheckStatus {
    Pass,
    Fail,
    Warning,
};

struct AccessCheckItem {
    Str category;
    Str title;
    Str detail;
    AccessCheckStatus status = AccessCheckStatus::Pass;
    int pageNo = 0;
};

struct AccessReport {
    Vec<AccessCheckItem> items;
    int passCount = 0;
    int failCount = 0;
    int warnCount = 0;
    bool isTagged = false;
    Str language;
    Str docTitle;
    bool displayDocTitle = false;
};

struct FigureAltItem {
    int pageNo = 1;
    int objNum = 0;
    RectF rect{};
    Str altText;
    bool isDecorative = false;
};

struct AccessibilityFixOptions {
    Str language;
    Str title;
    bool setDisplayDocTitle = true;
    bool autoTagBasicStructure = true;
    Vec<FigureAltItem> figures;
};

AccessReport RunAccessibilityCheck(fz_context* ctx, pdf_document* doc);
AccessReport RunAccessibilityCheckFile(Str filePath);
void FreeAccessReport(AccessReport& report);

bool ExtractDocFigures(fz_context* ctx, pdf_document* doc, Vec<FigureAltItem>& outFigures);
void FreeFigureAltItems(Vec<FigureAltItem>& figures);

bool ApplyAccessibilityFixes(Str srcPath, Str destPath, const AccessibilityFixOptions& options);

void ShowAccessibilityCheckerDialog(MainWindow* win);
void ShowAccessibilityFixesDialog(MainWindow* win);

#endif
