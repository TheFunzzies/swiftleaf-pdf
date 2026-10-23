enum PdfMergeWarning : u32 {
    PdfMergeDroppedTarget = 1,
    PdfMergeFormFallback = 2,
    PdfMergeScriptsRemoved = 4,
    PdfMergeSignatureCleared = 8,
    PdfMergeStructureDropped = 16,
};

struct PdfSanitizeOptions {
    bool metadata = true;
    bool hiddenContent = false;
    bool javascript = true;
    bool attachments = true;
    bool comments = true;
    bool forms = true;
    bool thumbnails = true;
};

enum class PdfSearchMode { Text, Regex };

TempStr EngineMupdfMergeWarningTemp(u32 warnings);
bool EngineMupdfSanitizePdf(const PdfMergeSource& src, Str destPath, const PdfSanitizeOptions& options, Str* errorOut);
bool EngineMupdfRedactMatches(const PdfMergeSource& src, Str destPath, Str term, PdfSearchMode mode, int* countOut,
                             Str* errorOut);
