/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

struct TextReplaceProgress {
    volatile LONG cancelled = 0;
    volatile LONG completed = 0;
    volatile LONG total = 0;
};

bool ReplaceTextWithProgress(EngineBase*, Str destination, Str find, Str replacement, int* countOut, Str* errorOut,
                             TextReplaceProgress* progress);
