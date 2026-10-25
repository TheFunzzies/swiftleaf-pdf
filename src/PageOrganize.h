/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

struct MainWindow;

struct PdfToolEdit {
    MainWindow* win = nullptr;
    Str inputPath;
    Str outputPath;
    Str originalPath;
    PdfToolEdit(MainWindow*, Str original);
    ~PdfToolEdit();
    bool IsReady();
    bool Apply(const char* label);
};

void OrganizeRotatePage(MainWindow*, int degrees);
void OrganizeInsertBlankPage(MainWindow*);
void OrganizeInsertPagesFromFile(MainWindow*);
void OrganizeMovePage(MainWindow*, int delta);
void OrganizeMovePageTo(MainWindow*, int from, int before);
bool OrganizeCanMovePages(MainWindow*);
void OrganizeDeleteCurrentPage(MainWindow*);
// writes <destBase without .pdf>-1.pdf, -2.pdf, ...; returns how many files
int OrganizeSplitPdf(MainWindow*, Str destBase, int pagesPerFile);
void OrganizeUndo(MainWindow*);
bool OrganizeCanUndo(MainWindow*);
