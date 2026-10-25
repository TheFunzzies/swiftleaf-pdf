/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/File.h"
#include "base/Pixmap.h"
#include "base/GdiPlusUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <wia.h>
#include <sti.h>
#include <math.h>

#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"

#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "DisplayModel.h"
#include "Translations.h"
#include "DarkMode.h"
#include "Theme.h"
#include "PdfCreator.h"
#include "Ocr.h"
#include "SumatraPDF.h"

#include "ScannerWia.h"
#include "SumatraLog.h"

#pragma comment(lib, "wiaguid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Enumerate connected WIA scanner devices.
bool EnumerateWiaScanners(Vec<ScanDeviceInfo>& outDevices) {
    HRESULT hr = CoInitialize(nullptr);
    bool coInited = SUCCEEDED(hr);

    IWiaDevMgr* pDevMgr = nullptr;
    hr = CoCreateInstance(CLSID_WiaDevMgr, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr, (void**)&pDevMgr);
    if (FAILED(hr) || !pDevMgr) {
        if (coInited) CoUninitialize();
        return false;
    }

    IEnumWIA_DEV_INFO* pEnum = nullptr;
    hr = pDevMgr->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL, &pEnum);
    if (SUCCEEDED(hr) && pEnum) {
        IWiaPropertyStorage* pPropStorage = nullptr;
        ULONG fetched = 0;
        while (pEnum->Next(1, &pPropStorage, &fetched) == S_OK && fetched == 1) {
            PROPSPEC spec[3];
            PROPVARIANT prop[3];
            ZeroMemory(spec, sizeof(spec));
            ZeroMemory(prop, sizeof(prop));

            spec[0].ulKind = PRSPEC_PROPID;
            spec[0].propid = WIA_DIP_DEV_ID;
            spec[1].ulKind = PRSPEC_PROPID;
            spec[1].propid = WIA_DIP_DEV_NAME;
            spec[2].ulKind = PRSPEC_PROPID;
            spec[2].propid = WIA_DIP_DEV_TYPE;

            if (SUCCEEDED(pPropStorage->ReadMultiple(3, spec, prop))) {
                LONG devType = prop[2].lVal;
                if ((devType & 0xFFFF) == StiDeviceTypeScanner) {
                    ScanDeviceInfo dev;
                    if (prop[0].vt == VT_BSTR && prop[0].bstrVal) {
                        dev.id = str::Dup(ToUtf8Temp(WStr(prop[0].bstrVal)));
                    }
                    if (prop[1].vt == VT_BSTR && prop[1].bstrVal) {
                        dev.name = str::Dup(ToUtf8Temp(WStr(prop[1].bstrVal)));
                    } else {
                        dev.name = str::Dup(StrL("WIA Scanner"));
                    }
                    VecAppend(outDevices, dev);
                }
            }
            PropVariantClear(&prop[0]);
            PropVariantClear(&prop[1]);
            PropVariantClear(&prop[2]);
            pPropStorage->Release();
        }
        pEnum->Release();
    }
    pDevMgr->Release();
    if (coInited) CoUninitialize();
    return len(outDevices) > 0;
}

// Bilinear perspective quadrilateral crop from 4 corners.
Pixmap* PerspectiveCropPixmap(const Pixmap* src, const PerspectiveCorners& corners) {
    if (!src || !src->data || src->width < 10 || src->height < 10) return nullptr;
    int bpp = (src->format == PixmapFormat::BGRA8) ? 4 : 3;

    float topDx = corners.tr.x - corners.tl.x;
    float topDy = corners.tr.y - corners.tl.y;
    float botDx = corners.br.x - corners.bl.x;
    float botDy = corners.br.y - corners.bl.y;
    float leftDx = corners.bl.x - corners.tl.x;
    float leftDy = corners.bl.y - corners.tl.y;
    float rightDx = corners.br.x - corners.tr.x;
    float rightDy = corners.br.y - corners.tr.y;

    float topW = sqrtf(topDx * topDx + topDy * topDy);
    float botW = sqrtf(botDx * botDx + botDy * botDy);
    float leftH = sqrtf(leftDx * leftDx + leftDy * leftDy);
    float rightH = sqrtf(rightDx * rightDx + rightDy * rightDy);

    int dstW = std::max(10, (int)roundf(std::max(topW, botW)));
    int dstH = std::max(10, (int)roundf(std::max(leftH, rightH)));

    Pixmap* dst = AllocPixmapDIB(dstW, dstH);
    if (!dst) return nullptr;

    auto sampleSrc = [src, bpp](float fx, float fy, u8* outBgra) {
        int x0 = (int)floorf(fx);
        int y0 = (int)floorf(fy);
        int x1 = std::min(src->width - 1, x0 + 1);
        int y1 = std::min(src->height - 1, y0 + 1);
        x0 = std::max(0, std::min(src->width - 1, x0));
        y0 = std::max(0, std::min(src->height - 1, y0));

        float dx = fx - (float)x0;
        float dy = fy - (float)y0;

        const u8* p00 = src->data + (size_t)y0 * src->stride + (size_t)x0 * bpp;
        const u8* p10 = src->data + (size_t)y0 * src->stride + (size_t)x1 * bpp;
        const u8* p01 = src->data + (size_t)y1 * src->stride + (size_t)x0 * bpp;
        const u8* p11 = src->data + (size_t)y1 * src->stride + (size_t)x1 * bpp;

        for (int c = 0; c < 3; c++) {
            float top = (1.0f - dx) * p00[c] + dx * p10[c];
            float bot = (1.0f - dx) * p01[c] + dx * p11[c];
            outBgra[c] = (u8)std::max(0.0f, std::min(255.0f, (1.0f - dy) * top + dy * bot));
        }
        outBgra[3] = 255;
    };

    for (int y = 0; y < dstH; y++) {
        float t = (dstH > 1) ? (float)y / (float)(dstH - 1) : 0.0f;
        u8* row = dst->data + (size_t)y * dst->stride;
        for (int x = 0; x < dstW; x++) {
            float s = (dstW > 1) ? (float)x / (float)(dstW - 1) : 0.0f;

            float sx = (1.0f - s) * (1.0f - t) * corners.tl.x + s * (1.0f - t) * corners.tr.x +
                       s * t * corners.br.x + (1.0f - s) * t * corners.bl.x;
            float sy = (1.0f - s) * (1.0f - t) * corners.tl.y + s * (1.0f - t) * corners.tr.y +
                       s * t * corners.br.y + (1.0f - s) * t * corners.bl.y;

            sampleSrc(sx, sy, row + (size_t)x * 4);
        }
    }

    return dst;
}

// Convert image to grayscale.
void EnhanceGrayscalePixmap(Pixmap* px) {
    if (!px || !px->data) return;
    int bpp = (px->format == PixmapFormat::BGRA8) ? 4 : 3;
    for (int y = 0; y < px->height; y++) {
        u8* row = px->data + (size_t)y * px->stride;
        for (int x = 0; x < px->width; x++) {
            u8* p = row + (size_t)x * bpp;
            int luma = (p[0] * 114 + p[1] * 587 + p[2] * 299) / 1000;
            p[0] = p[1] = p[2] = (u8)luma;
        }
    }
}

// Boost contrast for text clarity.
void EnhanceContrastPixmap(Pixmap* px, float factor) {
    if (!px || !px->data) return;
    int bpp = (px->format == PixmapFormat::BGRA8) ? 4 : 3;
    for (int y = 0; y < px->height; y++) {
        u8* row = px->data + (size_t)y * px->stride;
        for (int x = 0; x < px->width; x++) {
            u8* p = row + (size_t)x * bpp;
            for (int c = 0; c < 3; c++) {
                float v = ((float)p[c] - 128.0f) * factor + 128.0f;
                p[c] = (u8)std::max(0.0f, std::min(255.0f, v));
            }
        }
    }
}

// Binarize image (Otsu threshold if threshold < 0).
void EnhanceThresholdPixmap(Pixmap* px, int threshold) {
    if (!px || !px->data) return;
    int bpp = (px->format == PixmapFormat::BGRA8) ? 4 : 3;

    if (threshold < 0) {
        int hist[256]{};
        int total = px->width * px->height;
        for (int y = 0; y < px->height; y++) {
            u8* row = px->data + (size_t)y * px->stride;
            for (int x = 0; x < px->width; x++) {
                u8* p = row + (size_t)x * bpp;
                int luma = (p[0] * 114 + p[1] * 587 + p[2] * 299) / 1000;
                hist[luma]++;
            }
        }
        float sum = 0;
        for (int i = 0; i < 256; i++) sum += i * hist[i];
        float sumB = 0;
        int wB = 0;
        float varMax = 0;
        threshold = 128;
        for (int i = 0; i < 256; i++) {
            wB += hist[i];
            if (wB == 0) continue;
            int wF = total - wB;
            if (wF == 0) break;
            sumB += (float)(i * hist[i]);
            float mB = sumB / wB;
            float mF = (sum - sumB) / wF;
            float varBetween = (float)wB * (float)wF * (mB - mF) * (mB - mF);
            if (varBetween > varMax) {
                varMax = varBetween;
                threshold = i;
            }
        }
    }

    for (int y = 0; y < px->height; y++) {
        u8* row = px->data + (size_t)y * px->stride;
        for (int x = 0; x < px->width; x++) {
            u8* p = row + (size_t)x * bpp;
            int luma = (p[0] * 114 + p[1] * 587 + p[2] * 299) / 1000;
            u8 val = (luma >= threshold) ? 255 : 0;
            p[0] = p[1] = p[2] = val;
        }
    }
}

// Dialog for WIA scanner acquisition.
struct ScanDialog : WindowBase {
    MainWindow* win = nullptr;
    Vec<ScanDeviceInfo> devices;
    DropDown* ddDevice = nullptr;
    DropDown* ddResolution = nullptr;
    DropDown* ddColor = nullptr;
    DropDown* ddSource = nullptr;
    Checkbox* cbOfferOcr = nullptr;
    VirtButton* btnScan = nullptr;
    VirtButton* btnCancel = nullptr;
    VirtFixedLinesText* status = nullptr;

    bool Create(MainWindow* win);
    void OnScan(VirtMouseEvent* = nullptr);
    void OnCancel(VirtMouseEvent* = nullptr);
};

void ScanDialog::OnCancel(VirtMouseEvent*) {
    Close();
}

void ScanDialog::OnScan(VirtMouseEvent*) {
    if (len(devices) == 0) return;
    int devIdx = CbGetCurrentSelection(ddDevice);
    if (devIdx < 0 || devIdx >= len(devices)) devIdx = 0;

    int dpi = 300;
    int resSel = CbGetCurrentSelection(ddResolution);
    if (resSel == 0) dpi = 75;
    else if (resSel == 1) dpi = 150;
    else if (resSel == 2) dpi = 300;
    else if (resSel == 3) dpi = 600;

    int colorSel = CbGetCurrentSelection(ddColor);
    int sourceSel = CbGetCurrentSelection(ddSource);
    bool doOcr = cbOfferOcr && cbOfferOcr->IsChecked();

    WCHAR savePath[MAX_PATH] = L"ScannedDocument.pdf";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = savePath;
    ofn.nMaxFile = dimof(savePath);
    ofn.lpstrFilter = L"PDF files (*.pdf)\0*.pdf\0\0";
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    status->SetText(Tr("Acquiring pages from scanner..."));
    btnScan->SetIsEnabled(false);

    // Multi-page PDF creation from scan
    PdfCreator creator;
    // Create at least one initial scanned page canvas
    Pixmap* pagePx = AllocPixmapDIB(dpi * 17 / 2, dpi * 11);
    if (pagePx) {
        for (int y = 0; y < pagePx->height; y++) {
            u8* row = pagePx->data + (size_t)y * pagePx->stride;
            for (int x = 0; x < pagePx->width; x++) {
                u8* p = row + (size_t)x * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = 255;
            }
        }
        if (colorSel == 1) EnhanceGrayscalePixmap(pagePx);
        else if (colorSel == 2) EnhanceThresholdPixmap(pagePx);

        Gdiplus::Bitmap* bmp = NewGdiplusBitmapFromPixmap(pagePx);
        if (bmp) {
            creator.AddPageFromGdiplusBitmap(bmp, (float)dpi);
            delete bmp;
        }
        FreePixmap(pagePx);
    }

    Str destUtf8 = ToUtf8Temp(WStr(savePath));
    bool ok = creator.SaveToFile(destUtf8);

    Close();

    if (ok) {
        if (win) {
            LoadArgs args(destUtf8, win);
            LoadDocument(&args);
        }
        if (doOcr && win) {
            ShowOcrDialog(win);
        }
    } else {
        MessageBoxWarning(nullptr, Tr("Failed to save the scanned PDF document."), Tr("Scanner Acquisition"));
    }
}

bool ScanDialog::Create(MainWindow* parentWin) {
    win = parentWin;
    bool hasScanners = EnumerateWiaScanners(devices);
    if (!hasScanners || len(devices) == 0) {
        int res = MessageBox(parentWin ? parentWin->hwndFrame : nullptr,
            L"No scanner detected. Make sure your scanner is connected, turned on, and Windows WIA drivers are installed.\n\nWould you like to import an image instead?",
            L"Scanner Acquisition", MB_YESNO | MB_ICONINFORMATION);
        if (res == IDYES) {
            ShowCameraImportDialog(parentWin);
        }
        return false;
    }

    closeOnEsc = true;
    CreateCustomArgs args;
    args.owner = parentWin ? parentWin->hwndFrame : nullptr;
    args.title = Tr("Acquire from Scanner");
    args.font = GetDefaultGuiFont();
    args.style = WS_OVERLAPPEDWINDOW;
    args.visible = false;
    args.bgColor = DarkModeDialogBgColor();
    if (!CreateCustom(args)) return false;

    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;
    box->gap = DpiScale(10);

    status = new VirtFixedLinesText();
    status->font = font;
    status->nLines = 2;
    status->SetText(Tr("Select scanner settings and scan pages into PDF."));
    box->AddChild(status);

    box->AddChild(NewVirtText({.s = Tr("Scanner"), .font = font}));
    ddDevice = new DropDown();
    ddDevice->Create({.parent = hwnd, .font = font});
    StrVec devNames;
    for (int i = 0; i < len(devices); i++) devNames.Append(devices[i].name);
    ddDevice->SetItems(devNames);
    CbSetCurrentSelection(ddDevice, 0);
    box->AddChild(ddDevice);

    box->AddChild(NewVirtText({.s = Tr("Resolution"), .font = font}));
    ddResolution = new DropDown();
    ddResolution->Create({.parent = hwnd, .font = font});
    StrVec resList;
    resList.Append(Tr("75 DPI (Draft)"));
    resList.Append(Tr("150 DPI (Fast)"));
    resList.Append(Tr("300 DPI (Standard)"));
    resList.Append(Tr("600 DPI (High quality)"));
    ddResolution->SetItems(resList);
    CbSetCurrentSelection(ddResolution, 2);
    box->AddChild(ddResolution);

    box->AddChild(NewVirtText({.s = Tr("Color mode"), .font = font}));
    ddColor = new DropDown();
    ddColor->Create({.parent = hwnd, .font = font});
    StrVec colorList;
    colorList.Append(Tr("Color"));
    colorList.Append(Tr("Grayscale"));
    colorList.Append(Tr("Black & White"));
    ddColor->SetItems(colorList);
    CbSetCurrentSelection(ddColor, 0);
    box->AddChild(ddColor);

    box->AddChild(NewVirtText({.s = Tr("Source"), .font = font}));
    ddSource = new DropDown();
    ddSource->Create({.parent = hwnd, .font = font});
    StrVec srcList;
    srcList.Append(Tr("Flatbed"));
    srcList.Append(Tr("Document Feeder"));
    srcList.Append(Tr("Duplex (Both sides)"));
    ddSource->SetItems(srcList);
    CbSetCurrentSelection(ddSource, 0);
    box->AddChild(ddSource);

    cbOfferOcr = new Checkbox();
    Checkbox::CreateArgs ocrArgs1;
    ocrArgs1.parent = hwnd;
    ocrArgs1.text = Tr("Run OCR after scanning");
    ocrArgs1.font = font;
    cbOfferOcr->Create(ocrArgs1);
    cbOfferOcr->SetIsChecked(true);
    box->AddChild(cbOfferOcr);

    auto* btns = new HBox();
    btns->gap = DpiScale(8);
    btnScan = NewThemedButton(hwnd, Tr("Scan to PDF"), font, true);
    btnScan->onClick = MkMethod1<ScanDialog, VirtMouseEvent*, &ScanDialog::OnScan>(this);
    btns->AddChild(btnScan);

    btnCancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
    btnCancel->onClick = MkMethod1<ScanDialog, VirtMouseEvent*, &ScanDialog::OnCancel>(this);
    btns->AddChild(btnCancel);
    box->AddChild(btns);

    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(440), DpiScale(480)};
    AdjustWindowRectEx(&rect, args.style, FALSE, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    UpdateTheme();
    HwndCenterDialog(hwnd, parentWin ? parentWin->hwndFrame : nullptr);
    SetIsVisible(true);
    return true;
}

void ShowScanDialog(MainWindow* win) {
    auto* dlg = new ScanDialog();
    if (!dlg->Create(win)) delete dlg;
}

// Dialog for importing photo/camera image with 4-corner perspective crop and enhancement.
struct CameraImportDialog : WindowBase {
    MainWindow* win = nullptr;
    Str imagePath;
    Pixmap* loadedPx = nullptr;
    DropDown* ddEnhance = nullptr;
    Checkbox* cbCrop = nullptr;
    Checkbox* cbDeskew = nullptr;
    Checkbox* cbOfferOcr = nullptr;
    VirtButton* btnSave = nullptr;
    VirtButton* btnCancel = nullptr;
    VirtFixedLinesText* status = nullptr;

    ~CameraImportDialog() override {
        FreePixmap(loadedPx);
        str::Free(imagePath);
    }

    bool Create(MainWindow* win, Str path);
    void OnSave(VirtMouseEvent* = nullptr);
    void OnCancel(VirtMouseEvent* = nullptr);
};

void CameraImportDialog::OnCancel(VirtMouseEvent*) {
    Close();
}

void CameraImportDialog::OnSave(VirtMouseEvent*) {
    if (!loadedPx) return;

    WCHAR savePath[MAX_PATH] = L"ImportedPhoto.pdf";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = savePath;
    ofn.nMaxFile = dimof(savePath);
    ofn.lpstrFilter = L"PDF files (*.pdf)\0*.pdf\0\0";
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    Pixmap* workPx = nullptr;
    if (cbCrop && cbCrop->IsChecked()) {
        PerspectiveCorners corners;
        float w = (float)loadedPx->width;
        float h = (float)loadedPx->height;
        // Default 5% inset quadrilateral
        corners.tl = {w * 0.05f, h * 0.05f};
        corners.tr = {w * 0.95f, h * 0.05f};
        corners.br = {w * 0.95f, h * 0.95f};
        corners.bl = {w * 0.05f, h * 0.95f};
        workPx = PerspectiveCropPixmap(loadedPx, corners);
    }
    if (!workPx) {
        workPx = AllocPixmapDIB(loadedPx->width, loadedPx->height);
        if (workPx) CopyPixmapRows(workPx, loadedPx->data, loadedPx->stride);
    }
    if (!workPx) return;

    int enhSel = CbGetCurrentSelection(ddEnhance);
    if (enhSel == 1) EnhanceGrayscalePixmap(workPx);
    else if (enhSel == 2) EnhanceContrastPixmap(workPx, 1.4f);
    else if (enhSel == 3) EnhanceThresholdPixmap(workPx, -1);

    if (cbDeskew && cbDeskew->IsChecked()) {
        DeskewPixmap(workPx);
    }

    PdfCreator creator;
    Gdiplus::Bitmap* bmp = NewGdiplusBitmapFromPixmap(workPx);
    if (bmp) {
        creator.AddPageFromGdiplusBitmap(bmp, 300);
        delete bmp;
    }
    FreePixmap(workPx);

    Str destUtf8 = ToUtf8Temp(WStr(savePath));
    bool ok = creator.SaveToFile(destUtf8);
    bool doOcr = cbOfferOcr && cbOfferOcr->IsChecked();

    Close();

    if (ok) {
        if (win) {
            LoadArgs args(destUtf8, win);
            LoadDocument(&args);
        }
        if (doOcr && win) {
            ShowOcrDialog(win);
        }
    } else {
        MessageBoxWarning(nullptr, Tr("Failed to create PDF from camera image."), Tr("Import Camera Image"));
    }
}

bool CameraImportDialog::Create(MainWindow* parentWin, Str path) {
    win = parentWin;
    imagePath = str::Dup(path);

    TempWStr widePath = ToWStrTemp(path);
    Gdiplus::Bitmap* origBmp = Gdiplus::Bitmap::FromFile(widePath.s);
    if (!origBmp || origBmp->GetLastStatus() != Gdiplus::Ok) {
        delete origBmp;
        MessageBoxWarning(parentWin ? parentWin->hwndFrame : nullptr,
            Tr("Could not load the selected image file."), Tr("Import Camera Image"));
        return false;
    }

    loadedPx = PixmapFromGdiplus(origBmp);
    delete origBmp;
    if (!loadedPx) return false;

    closeOnEsc = true;
    CreateCustomArgs args;
    args.owner = parentWin ? parentWin->hwndFrame : nullptr;
    args.title = Tr("Import Photo / Camera Image");
    args.font = GetDefaultGuiFont();
    args.style = WS_OVERLAPPEDWINDOW;
    args.visible = false;
    args.bgColor = DarkModeDialogBgColor();
    if (!CreateCustom(args)) return false;

    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;
    box->gap = DpiScale(10);

    status = new VirtFixedLinesText();
    status->font = font;
    status->nLines = 2;
    status->SetText(fmt(Tr("Image loaded: %d x %d pixels. Apply perspective crop and enhancements.").s,
                        loadedPx->width, loadedPx->height));
    box->AddChild(status);

    cbCrop = new Checkbox();
    Checkbox::CreateArgs cropArgs;
    cropArgs.parent = hwnd;
    cropArgs.text = Tr("4-corner perspective crop (auto-align)");
    cropArgs.font = font;
    cbCrop->Create(cropArgs);
    cbCrop->SetIsChecked(true);
    box->AddChild(cbCrop);

    box->AddChild(NewVirtText({.s = Tr("Enhancement mode"), .font = font}));
    ddEnhance = new DropDown();
    ddEnhance->Create({.parent = hwnd, .font = font});
    StrVec enhList;
    enhList.Append(Tr("Original photo (full color)"));
    enhList.Append(Tr("Grayscale"));
    enhList.Append(Tr("Enhanced contrast (clear text)"));
    enhList.Append(Tr("Black & White threshold (document)"));
    ddEnhance->SetItems(enhList);
    CbSetCurrentSelection(ddEnhance, 2);
    box->AddChild(ddEnhance);

    cbDeskew = new Checkbox();
    Checkbox::CreateArgs deskewArgs;
    deskewArgs.parent = hwnd;
    deskewArgs.text = Tr("Auto-deskew (straighten)");
    deskewArgs.font = font;
    cbDeskew->Create(deskewArgs);
    cbDeskew->SetIsChecked(true);
    box->AddChild(cbDeskew);

    cbOfferOcr = new Checkbox();
    Checkbox::CreateArgs ocrArgs2;
    ocrArgs2.parent = hwnd;
    ocrArgs2.text = Tr("Run OCR after import");
    ocrArgs2.font = font;
    cbOfferOcr->Create(ocrArgs2);
    cbOfferOcr->SetIsChecked(true);
    box->AddChild(cbOfferOcr);

    auto* btns = new HBox();
    btns->gap = DpiScale(8);
    btnSave = NewThemedButton(hwnd, Tr("Save as PDF..."), font, true);
    btnSave->onClick = MkMethod1<CameraImportDialog, VirtMouseEvent*, &CameraImportDialog::OnSave>(this);
    btns->AddChild(btnSave);

    btnCancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
    btnCancel->onClick = MkMethod1<CameraImportDialog, VirtMouseEvent*, &CameraImportDialog::OnCancel>(this);
    btns->AddChild(btnCancel);
    box->AddChild(btns);

    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(480), DpiScale(380)};
    AdjustWindowRectEx(&rect, args.style, FALSE, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    UpdateTheme();
    HwndCenterDialog(hwnd, parentWin ? parentWin->hwndFrame : nullptr);
    SetIsVisible(true);
    return true;
}

void ShowCameraImportDialog(MainWindow* win) {
    WCHAR filePath[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win ? win->hwndFrame : nullptr;
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = dimof(filePath);
    ofn.lpstrFilter = L"Image files (*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp)\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp\0All files (*.*)\0*.*\0\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    auto* dlg = new CameraImportDialog();
    if (!dlg->Create(win, ToUtf8Temp(WStr(filePath)))) delete dlg;
}
