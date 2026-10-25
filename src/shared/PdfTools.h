/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

namespace gpui {
struct Ctx;
struct El;
} // namespace gpui

struct MainWindow;

void ShowPdfBakeDialog(MainWindow* win);
void ShowPdfSanitizeDialog(MainWindow* win);
void ShowPdfRedactSearchDialog(MainWindow* win);
void ShowPdfReplaceTextDialog(MainWindow* win);
void ShowPdfFontManager(MainWindow* win);
void ShowPdfExtractTextDialog(MainWindow* win);
void ShowPdfCompressDialog(MainWindow* win);
enum class PdfOptimizerPreset {
    Web,
    Print,
    Archive,
    MinSize
};
struct PdfOptimizationAudit {
    i64 beforeBytes = 0;
    i64 afterBytes = 0;
};
bool PdfOptimizeFile(Str srcPath, Str destPath, PdfOptimizerPreset preset, PdfOptimizationAudit* auditOut = nullptr);
void ShowPdfDecompressDialog(MainWindow* win);
void ShowPdfDeletePageDialog(MainWindow* win);
void ShowPdfExtractPagesDialog(MainWindow* win);
void ShowPdfSplitDialog(MainWindow* win);
void ShowMergePdfDialog(MainWindow* win);
TempStr MergePdfResultTemp(Str action, Str arg, int n, int* exitCodeOut);
void ShowPdfEncryptDialog(MainWindow* win);
void ShowPdfDecryptDialog(MainWindow* win);
// comic books / image folders / single images → multi-page PDF (issue #4118)
void ShowConvertToPdfDialog(MainWindow* win);
TempStr ConvertImageCollectionToPdfResultTemp(Str srcPath, Str destPath, int* exitCodeOut);
TempStr ExtractPdfPagesResultTemp(Str destPath, Str pagesSpec, int annotsOnly, int* exitCodeOut);
// PDF pages → PNG / JPEG / BMP files (issue #5991)
void ShowConvertPdfToImagesDialog(MainWindow* win);
TempStr ConvertPagesToImagesResultTemp(Str templatePath, Str pagesSpec, int dpi, int* exitCodeOut);
void ShowSaveSelectionAsImageDialog(MainWindow* win);
TempStr SaveSelectionAsImageResultTemp(Str destPath, int dpi, int pageNo, int x, int y, int dx, int dy,
                                       int* exitCodeOut);

enum class PdfWatermarkPos {
    Center,
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

struct PdfWatermarkParams {
    Str text;
    float opacity = 0.5f;
    float angle = 45.0f;
    PdfWatermarkPos position = PdfWatermarkPos::Center;
    float fontSize = 48.0f;
    COLORREF color = RGB(128, 128, 128);
    Str pagesSpec;
};

struct PdfHeaderFooterParams {
    Str headerLeft;
    Str headerCenter;
    Str headerRight;
    Str footerLeft;
    Str footerCenter;
    Str footerRight;
    float fontSize = 10.0f;
    COLORREF color = RGB(0, 0, 0);
    Str pagesSpec;
};

enum class PdfBatesPos {
    BottomRight,
    BottomCenter,
    BottomLeft,
    TopRight,
    TopCenter,
    TopLeft,
};

struct PdfBatesParams {
    Str prefix;
    int startNumber = 1;
    int digitCount = 6;
    PdfBatesPos position = PdfBatesPos::BottomRight;
    float fontSize = 10.0f;
    COLORREF color = RGB(0, 0, 0);
    Str pagesSpec;
};

void ShowPdfAddWatermarkDialog(MainWindow* win);
void ShowPdfAddHeaderFooterDialog(MainWindow* win);
void ShowPdfBatesNumberingDialog(MainWindow* win);

bool PdfApplyWatermark(Str srcPath, Str destPath, const PdfWatermarkParams& params);
bool PdfApplyHeaderFooter(Str srcPath, Str destPath, const PdfHeaderFooterParams& params);
bool PdfApplyBatesNumbering(Str srcPath, Str destPath, const PdfBatesParams& params);

// ng: orig's tool dialogs are windows of their own; here each one is a gpui
// dialog the shell asks for once a frame, so they share one Show / Close /
// IsVisible / Build quadruple (only one can be up at a time)
void ClosePdfToolDialog();
bool IsPdfToolDialogVisible();
bool PdfToolDialogOnEnter();
// the Merge PDF dialog's keys (arrows, Delete, Ctrl + A) and dropped PDFs
// the Apps key in the Merge PDF grid: its context menu at the cursor
bool PdfToolDialogContextMenuFromKey(MainWindow* win, gpui::Ctx* cx);
// the delete / extract pages dialogs
bool PdfToolDialogIsPagesKind();
// Esc during a Merge PDF drag: ends the drag, the dialog stays
bool PdfToolDialogEndDrag();
bool PdfToolDialogOnKeyDown(MainWindow* win, int vk, bool ctrl, bool shift);
// which window a drag is over: the frame, or a tool window of its own
enum class DropHost {
    Frame,
    ToolWindow,
};
// orig's MergeDropTarget::DragOver / DragLeave: files are being dragged over
// the window (`pt` in dips of that window; null when the drag left or ended).
// False when Merge PDF isn't up in that window; `accept` says whether a drop
// at `pt` would be taken, and the grid shows the bar where the pages would go.
bool PdfToolDialogOnDragOver(MainWindow* win, const PointF* pt, bool hasPdf, bool* accept,
                             DropHost host = DropHost::Frame);
// Merge PDF is up as a dialog in the frame
bool PdfToolDialogIsMerge(MainWindow* win);
bool PdfToolDialogOnDropFiles(MainWindow* win, const Str* paths, int n, const PointF* pt,
                              DropHost host = DropHost::Frame);
void PdfToolDialogTick(MainWindow* win, int ms);
gpui::El* PdfToolDialogBuild(MainWindow* win, gpui::Ctx* cx);
// the open dialog's fields as one line, for the log and the tests
TempStr PdfToolStateTemp();
