/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#ifndef OCR_H
#define OCR_H

struct MainWindow;
struct Pixmap;
struct fz_context;
struct fz_page;

struct OcrExportWord {
    Str text;
    RectF box;
};

bool OcrExportPage(fz_context* ctx, fz_page* page, Vec<OcrExportWord>& words);

void ShowOcrDialog(MainWindow*);
void ShowBatchOcrDialog(MainWindow*);

void DeskewPixmap(Pixmap* px);
void DespecklePixmap(Pixmap* px);
void RemoveBordersPixmap(Pixmap* px);
void PreprocessOcrPixmap(Pixmap* px);
double DetectDeskewAngle(const Pixmap* px);

typedef bool (*BatchOcrProgressFn)(int curFile, int totalFiles, int curPage, int totalPages, void* user);
bool BatchOcrPdf(Str inputPdfPath, Str outputPdfPath, Str language = {}, BatchOcrProgressFn progress = nullptr,
                 void* user = nullptr, Str* errOut = nullptr);

#if IS_DEBUG
bool Ocr_UnitTests();
#endif

#endif
