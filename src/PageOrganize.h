/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Swiftleaf: change the pages of the PDF that is open, in place. Each change
// writes the whole document anew (EngineMupdfMergePdfs), keeps the previous
// file for Undo Page Change and reopens the result in the same tab.

struct MainWindow;

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
