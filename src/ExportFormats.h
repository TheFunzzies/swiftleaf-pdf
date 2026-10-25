/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

enum class ExportFormat {
    Docx,
    Pptx,
    Xlsx,
    Html,
    Rtf,
    Markdown,
    Text,
    Json,
    Svg
};

struct ConversionProgress {
    volatile LONG cancelled = 0;
    volatile LONG completed = 0;
    volatile LONG total = 0;
    Str error;
    ~ConversionProgress() { str::Free(error); }
};

bool ExportPdf(Str source, Str destination, ExportFormat format, Str pages, ConversionProgress& progress);
bool CreatePdfFiles(const StrVec& sources, Str destination, Str pages, ConversionProgress& progress);
bool CreateClipboardPdf(HWND owner, Str destination, ConversionProgress& progress);
void ExportFormatsUnitTests();

struct MainWindow;
void ShowExportPdfDialog(MainWindow* win);
void ShowCreatePdfFilesDialog(MainWindow* win, bool clipboard = false);
