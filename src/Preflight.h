/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#ifndef PREFLIGHT_H
#define PREFLIGHT_H


struct MainWindow;
struct fz_context;
struct pdf_document;

struct PreflightFontInfo {
    Str baseFont;
    Str subtype;
    Str encoding;
    bool isEmbedded = false;
    bool isSubset = false;
    Vec<int> pages;
};

struct PreflightImageInfo {
    int pageNo = 1;
    int objNum = 0;
    int width = 0;
    int height = 0;
    float ptWidth = 0.f;
    float ptHeight = 0.f;
    int dpiX = 0;
    int dpiY = 0;
    Str colorSpace;
    int bitsPerComponent = 8;
    Str filter;
};

struct PreflightPageBoxInfo {
    int pageNo = 1;
    RectF mediaBox{};
    RectF cropBox{};
    RectF bleedBox{};
    RectF trimBox{};
    RectF artBox{};
};

struct PreflightReport {
    int pdfMajorVersion = 1;
    int pdfMinorVersion = 7;
    int pageCount = 0;
    bool isEncrypted = false;
    Str encryptionMethod;
    bool hasJavaScript = false;
    StrVec javaScriptNames;
    bool hasTransparency = false;
    StrVec colorSpaces;
    Vec<PreflightFontInfo> fonts;
    Vec<PreflightImageInfo> images;
    Vec<PreflightPageBoxInfo> pageBoxes;
};

PreflightReport RunPreflight(fz_context* ctx, pdf_document* doc);
PreflightReport RunPreflightFile(Str filePath);
void FreePreflightReport(PreflightReport& report);

bool ExportPreflightReport(const PreflightReport& report, Str destPath, bool isCsv);

bool ConvertPdfToPdfA(Str srcPath, Str destPath, int part /* 1 or 2 */);
bool ValidatePdfA(Str filePath, int part, StrVec& violationsOut);

bool PdfConvertColors(Str srcPath, Str destPath, int numComp /* 1 for Gray, 3 for RGB */);

void ShowPreflightDialog(MainWindow* win);
void ShowPdfAConvertDialog(MainWindow* win);
void ShowPdfAValidateDialog(MainWindow* win);
void ShowPdfConvertColorsDialog(MainWindow* win);

#endif
