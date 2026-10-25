/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#ifndef BATCHACTIONS_H
#define BATCHACTIONS_H

// Runs a saved batch definition against PDF paths and returns one JSON record
// per input. The caller may run this on a worker thread.
TempStr RunBatchActionsJson(Str definition, const StrVec& files);

#endif
