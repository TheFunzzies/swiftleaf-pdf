/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#ifndef BATCHACTIONS_H
#define BATCHACTIONS_H

// Runs a saved batch definition against PDF paths and returns one JSON record
// per input. The caller may run this on a worker thread.
struct BatchProgress {
    volatile LONG cancelled = 0;
    volatile LONG completedFiles = 0;
    volatile LONG totalFiles = 0;
    volatile LONG currentFile = 0;
    volatile LONG currentAction = 0;
    volatile LONG totalActions = 0;
};

TempStr RunBatchActionsJson(Str definition, const Vec<Str>& files, BatchProgress* progress = nullptr);
bool SaveBatchActionsJson(Str name, Str definition);
TempStr LoadBatchActionsJsonTemp(Str name);

#endif
