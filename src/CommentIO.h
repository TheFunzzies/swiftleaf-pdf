/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#pragma once

class EngineBase;
struct Annotation;
struct MainWindow;

// Comment review states per PDF specification (ISO 32000-1)
constexpr const char* kStateAccepted = "Accepted";
constexpr const char* kStateRejected = "Rejected";
constexpr const char* kStateCompleted = "Completed";
constexpr const char* kStateCancelled = "Cancelled";
constexpr const char* kStateMarked = "Marked";
constexpr const char* kStateUnmarked = "Unmarked";
constexpr const char* kStateModelReview = "Review";
constexpr const char* kStateModelMarked = "Marked";

// Status / Review
Str GetAnnotationState(Annotation* annot);
Str GetAnnotationStateModel(Annotation* annot);
bool SetAnnotationState(Annotation* annot, Str state, Str stateModel = StrL(kStateModelReview));
Annotation* CreateAnnotationReply(Annotation* parentAnnot, Str replyText, Str author = {});
void GetAnnotationReplies(Annotation* parentAnnot, Vec<Annotation*>& outReplies);

// Import & Export
bool ExportAnnotationsToXfdf(EngineBase* engine, const char* filePath, Str& errOut);
bool ExportAnnotationsToFdf(EngineBase* engine, const char* filePath, Str& errOut);
bool ImportAnnotationsFromXfdf(EngineBase* engine, const char* filePath, Str& errOut);
bool ImportAnnotationsFromFdf(EngineBase* engine, const char* filePath, Str& errOut);

// Summary & Flatten
bool ExportCommentSummary(EngineBase* engine, const char* filePath, const char* format /* "pdf", "txt", "csv" */,
                          Str& errOut);
bool FlattenAnnotations(EngineBase* engine, Str& errOut);

// UI Action handlers
void OnExportXfdf(MainWindow* win);
void OnImportXfdf(MainWindow* win);
void OnExportFdf(MainWindow* win);
void OnImportFdf(MainWindow* win);
void OnCommentSummary(MainWindow* win);
void OnFlattenComments(MainWindow* win);
void OnReplyToComment(MainWindow* win, Annotation* annot);
void OnSetCommentStatus(MainWindow* win, Annotation* annot, const char* status);
