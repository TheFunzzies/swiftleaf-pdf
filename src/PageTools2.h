/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#ifndef PAGETOOLS2_H
#define PAGETOOLS2_H

struct MainWindow;

// (1) Crop & Page Boxes
struct PdfPageBoxesParams {
    Str pagesSpec;
    int boxType = 1; // 0=Media, 1=Crop, 2=Trim, 3=Bleed, 4=Art
    float marginLeft = 0.f;
    float marginBottom = 0.f;
    float marginRight = 0.f;
    float marginTop = 0.f;
    bool removeWhiteMargins = false;
};
void ShowCropPagesDialog(MainWindow* win);
bool PdfApplyPageBoxes(Str srcPath, Str destPath, const PdfPageBoxesParams& params);
bool PdfRotatePages(Str srcPath, Str destPath, Str pagesSpec, int degrees);

// (2) N-up / Grid, Booklet, Posterize, Divide, Combine Long Page
struct PdfNUpParams {
    int pagesPerSheet = 2; // 2, 4, 6, 8, 9, 16
    bool drawBorders = false;
};
void ShowPdfNUpDialog(MainWindow* win);
bool PdfApplyNUp(Str srcPath, Str destPath, const PdfNUpParams& params);

struct PdfBookletParams {
    bool fold = true;
};
void ShowPdfBookletDialog(MainWindow* win);
bool PdfApplyBooklet(Str srcPath, Str destPath, const PdfBookletParams& params);

struct PdfPosterizeParams {
    int xTiles = 2;
    int yTiles = 2;
    float overlapPoints = 0.f;
};
void ShowPdfPosterizeDialog(MainWindow* win);
bool PdfApplyPosterize(Str srcPath, Str destPath, const PdfPosterizeParams& params);

struct PdfDividePagesParams {
    int horizontalDivisions = 2;
    int verticalDivisions = 1;
};
void ShowPdfDividePagesDialog(MainWindow* win);
bool PdfApplyDividePages(Str srcPath, Str destPath, const PdfDividePagesParams& params);

void ShowPdfCombineLongPageDialog(MainWindow* win);
bool PdfApplyCombineLongPage(Str srcPath, Str destPath);

// (3) Page Background Color / Image
struct PdfBackgroundParams {
    COLORREF color = RGB(255, 255, 255);
    bool useColor = true;
    Str imagePath;
    float opacity = 1.0f;
    Str pagesSpec;
};
void ShowPdfPageBackgroundDialog(MainWindow* win);
bool PdfApplyPageBackground(Str srcPath, Str destPath, const PdfBackgroundParams& params);

// (4) Duplicate, Reverse, Insert Blank, Remove Blank, Deskew
void ShowDuplicatePagesDialog(MainWindow* win);
bool PdfDuplicatePages(Str srcPath, Str destPath, Str pagesSpec);

void ShowReversePageOrderDialog(MainWindow* win);
bool PdfReversePageOrder(Str srcPath, Str destPath);

void ShowInsertBlankPagesDialog(MainWindow* win);
bool PdfInsertBlankPages(Str srcPath, Str destPath, int afterPage, int count);

void ShowRemoveBlankPagesDialog(MainWindow* win);
bool PdfRemoveBlankPages(Str srcPath, Str destPath, int* removedCountOut = nullptr);

void ShowDeskewPagesDialog(MainWindow* win);
bool PdfDeskewPages(Str srcPath, Str destPath, double minAngleThreshold = 0.2);

// (5) Document Properties Editor
struct PdfDocPropsParams {
    Str title;
    Str author;
    Str subject;
    Str keywords;
    Str xmp;
};
void ShowEditDocumentPropertiesDialog(MainWindow* win);
bool PdfGetDocProps(Str srcPath, PdfDocPropsParams& outProps);
bool PdfSetDocProps(Str srcPath, Str destPath, const PdfDocPropsParams& params);

// (6) Attachments Manager
struct PdfAttachmentInfo {
    Str name;
    int size = 0;
};
void ShowManageAttachmentsDialog(MainWindow* win);
bool PdfListAttachments(Str srcPath, Vec<PdfAttachmentInfo>& outList);
bool PdfAddAttachment(Str srcPath, Str destPath, Str attachFilePath);
bool PdfExtractAttachment(Str srcPath, Str attachName, Str extractDestPath);
bool PdfDeleteAttachment(Str srcPath, Str destPath, Str attachName);

// (7) Bookmark / Outline Editor
struct PdfBookmarkItem {
    Str title;
    int pageNo = 1;
    int level = 0;
};
void ShowEditBookmarksDialog(MainWindow* win);
bool PdfListBookmarks(Str srcPath, Vec<PdfBookmarkItem>& outList);
bool PdfSaveBookmarks(Str srcPath, Str destPath, const Vec<PdfBookmarkItem>& items);
bool PdfAutoGenerateBookmarksFromHeadings(Str srcPath, Str destPath);

// (8) Page Labels / Numbering Ranges
struct PdfPageLabelRange {
    int startPageIndex = 0;
    char style = 'D'; // 'D', 'R', 'r', 'A', 'a'
    Str prefix;
    int startNum = 1;
};
void ShowEditPageLabelsDialog(MainWindow* win);
bool PdfSetPageLabelRanges(Str srcPath, Str destPath, const Vec<PdfPageLabelRange>& ranges);
bool PdfGetPageLabels(Str srcPath, Vec<Str>& outLabels);

// (9) Linearize, Repair, Rasterize, Grayscale, Invert, Remove Annotations, Remove Links
void ShowPdfLinearizeDialog(MainWindow* win);
bool PdfLinearize(Str srcPath, Str destPath);

void ShowPdfRepairDialog(MainWindow* win);
bool PdfRepair(Str srcPath, Str destPath);

void ShowPdfRasterizeDialog(MainWindow* win);
bool PdfRasterize(Str srcPath, Str destPath, int dpi = 150);

void ShowPdfGrayscaleDialog(MainWindow* win);
bool PdfConvertToGrayscale(Str srcPath, Str destPath);

void ShowPdfInvertColorsDialog(MainWindow* win);
bool PdfInvertColors(Str srcPath, Str destPath);

void ShowPdfRemoveAnnotationsDialog(MainWindow* win);
bool PdfRemoveAnnotations(Str srcPath, Str destPath);

void ShowPdfRemoveLinksDialog(MainWindow* win);
bool PdfRemoveAllLinks(Str srcPath, Str destPath);

// (10) Layers (OCG) Panel
struct PdfLayerInfo {
    Str name;
    bool isVisible = true;
    int index = 0;
};
void ShowPdfManageLayersDialog(MainWindow* win);
bool PdfListLayers(Str srcPath, Vec<PdfLayerInfo>& outLayers);
bool PdfSetLayersVisibility(Str srcPath, Str destPath, const Vec<bool>& visibility);
bool PdfFlattenLayers(Str srcPath, Str destPath);

#endif
