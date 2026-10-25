/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#pragma once

class EngineBase;
struct Annotation;
struct MainWindow;

struct MeasureScale {
    float pointsPerUnit = 72.0f; // 72 pt = 1 inch by default
    char unit[16] = "in";
    char label[64] = "1 in = 1 in";
};

MeasureScale GetPageScale(EngineBase* engine, int pageNo);
void SetPageScale(EngineBase* engine, int pageNo, const MeasureScale& scale);

// Measurement calculations
float ComputeDistance(PointF p1, PointF p2, const MeasureScale& scale);
float ComputePerimeter(const Vec<PointF>& points, const MeasureScale& scale);
float ComputeArea(const Vec<PointF>& points, const MeasureScale& scale);

// Formatting helpers
void FormatDistance(float val, const MeasureScale& scale, str::Builder& out);
void FormatPerimeter(float val, const MeasureScale& scale, str::Builder& out);
void FormatArea(float val, const MeasureScale& scale, str::Builder& out);

TempStr FormatDistance(float val, const MeasureScale& scale);
TempStr FormatPerimeter(float val, const MeasureScale& scale);
TempStr FormatArea(float val, const MeasureScale& scale);

// Creation
Annotation* CreateMeasureAnnotation(EngineBase* engine, int pageNo, int cmdId, const Vec<PointF>& pts, const MeasureScale& scale);

// Per-annotation PDF /Measure dictionary storage
bool AttachMeasureDictToAnnotation(EngineBase* engine, Annotation* annot, const MeasureScale& scale, const char* measureType);

// Content / Endpoint snapping (returns true if snapped)
PointF SnapToPageContent(EngineBase* engine, int pageNo, PointF pt, float radiusPt, bool* outSnapped = nullptr);
bool SnapToPageContent(EngineBase* engine, int pageNo, PointF pt, PointF& outSnapped, float snapRadiusPt = 8.0f);

// Export measurements to CSV
bool ExportMeasurementsToCsv(EngineBase* engine, const char* filePath, Str& errOut);

// UI handlers
void OnCalibrateScale(MainWindow* win);
void OnExportMeasurementsCsv(MainWindow* win);
