/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Swiftleaf: change the pages of the PDF that is open, in place. Each change
// writes the whole document anew (EngineMupdfMergePdfs), keeps the previous
// file for Undo Page Change and reopens the result in the same tab.

struct MainWindow;
class EngineBase;

void OrganizeRotatePage(MainWindow*, int degrees);
void OrganizeInsertBlankPage(MainWindow*);
void OrganizeInsertPagesFromFile(MainWindow*);
void OrganizeMovePage(MainWindow*, int delta);
void OrganizeMovePageTo(MainWindow*, int from, int before);
bool OrganizeCanMovePages(MainWindow*);
void OrganizeDeleteCurrentPage(MainWindow*);
// splits a snapshot of engine's document into <destBase without .pdf>-1.pdf,
// -2.pdf, ...; returns how many files
int OrganizeSplitPdf(MainWindow*, EngineBase*, Str snapshot, Str destBase, int pagesPerFile);
void OrganizeUndo(MainWindow*);
bool OrganizeCanUndo(MainWindow*);
void OrganizeForgetClosed();
void OrganizeClearUndo();

// TODO(B2): declared in EditText.h once review/b2-edit-text lands
bool CommitOpenEditors(MainWindow*);

// a saved copy of a live document in a temp file, deleted with the struct
struct PdfLiveSnapshot {
    Str path;
    PdfLiveSnapshot() = default;
    PdfLiveSnapshot(const PdfLiveSnapshot&) = delete;
    PdfLiveSnapshot& operator=(const PdfLiveSnapshot&) = delete;
    ~PdfLiveSnapshot();
    bool Create(EngineBase*);
};

TempStr PdfTempBeside(Str target);
bool PublishPdfFile(Str target, Str source);
bool ConfirmWarning(HWND, Str msg, Str title);
