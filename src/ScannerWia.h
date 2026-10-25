/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#ifndef SCANNER_WIA_H
#define SCANNER_WIA_H

struct MainWindow;
struct Pixmap;

enum class ScanColorMode {
    Color = 1,
    Grayscale = 2,
    BlackAndWhite = 4,
};

enum class ScanSource {
    Flatbed = 1,
    Feeder = 2,
    Duplex = 4,
};

struct ScanDeviceInfo {
    Str id;
    Str name;
};

struct PerspectiveCorners {
    PointF tl;
    PointF tr;
    PointF br;
    PointF bl;
};

void ShowScanDialog(MainWindow* win);
void ShowCameraImportDialog(MainWindow* win);

bool EnumerateWiaScanners(Vec<ScanDeviceInfo>& outDevices);

Pixmap* PerspectiveCropPixmap(const Pixmap* src, const PerspectiveCorners& corners);
void EnhanceGrayscalePixmap(Pixmap* px);
void EnhanceContrastPixmap(Pixmap* px, float factor = 1.3f);
void EnhanceThresholdPixmap(Pixmap* px, int threshold = -1);

#endif
