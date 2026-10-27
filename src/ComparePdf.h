/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#ifndef COMPAREPDF_H
#define COMPAREPDF_H

// Base.h is included first by every translation unit (project convention); only
// the Vec template needs a forward declaration.
template <typename T>
struct Vec;
struct MainWindow;
struct PdfWorkProgress;

// One word-level diff. html is the del/ins markup of the whole text; edits are
// maximal runs of changed words, so two separated edits are reported as two runs.
struct PdfWordDiff {
    TempStr html;
    int changedWords = 0;
    int editRuns = 0;
    // True when the LCS table would exceed its cap and the changed span is
    // reported as one coarse run instead of an exact, grained script.
    bool coarse = false;
};

// One aligned page pair. left/right are 0-based; -1 means the page exists only
// on the other side. moved is set for pages matched out of order by similarity.
struct PdfPageMatch {
    int left = -1;
    int right = -1;
    int similarity = 0;
    bool moved = false;
};

PdfWordDiff ComparePdfWordDiff(Str leftText, Str rightText);
void ComparePdfAlignPages(const Vec<Str>& leftTexts, const Vec<Str>& rightTexts, Vec<PdfPageMatch>& out);

bool ComparePdfReport(Str leftPath, Str rightPath, Str reportPath, Str* resultJsonOut = nullptr,
                      PdfWorkProgress* progress = nullptr);
void ShowComparePdfDialog(MainWindow* win);

#endif
