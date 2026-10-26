/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/AutoWin.h"
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

    IWiaDevMgr2* pDevMgr = nullptr;
    hr = CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2, (void**)&pDevMgr);
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

struct ScanJob {
    LONG refs = 1;
    LONG done = 0;
    LONG pages = 0;
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Str device;
    Str destination;
    int dpi = 300;
    int color = 0;
    int source = 0;
    HRESULT error = S_OK;

    ~ScanJob() {
        if (cancel) CloseHandle(cancel);
        str::Free(device);
        str::Free(destination);
    }
    void Release() {
        if (!InterlockedDecrement(&refs)) delete this;
    }
    bool Cancelled() { return WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0; }
};

static HRESULT SetWiaLong(IWiaPropertyStorage* props, PROPID id, LONG value) {
    PROPSPEC spec{};
    spec.ulKind = PRSPEC_PROPID;
    spec.propid = id;
    PROPVARIANT prop{};
    prop.vt = VT_I4;
    prop.lVal = value;
    return props->WriteMultiple(1, &spec, &prop, WIA_IPA_FIRST);
}

static HRESULT FindWiaSource(IWiaItem2* root, const GUID& category, IWiaItem2** found) {
    AutoReleaseComPtr<IEnumWiaItem2> items;
    HRESULT hr = root->EnumChildItems(nullptr, &items);
    if (FAILED(hr)) return hr;
    for (;;) {
        AutoReleaseComPtr<IWiaItem2> item;
        ULONG count = 0;
        hr = items->Next(1, &item, &count);
        if (hr != S_OK) return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        GUID current{};
        hr = item->GetItemCategory(&current);
        if (FAILED(hr)) return hr;
        if (current == category) {
            *found = item.Get();
            (*found)->AddRef();
            return S_OK;
        }
    }
}

struct ScanTransfer : IWiaTransferCallback {
    LONG refs = 1;
    ScanJob* job;
    PdfCreator* pdf;
    AutoReleaseComPtr<IStream> stream;
    HRESULT error = S_OK;
    ScanTransfer(ScanJob* job, PdfCreator* pdf) : job(job), pdf(pdf) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IWiaTransferCallback) return E_NOINTERFACE;
        *out = static_cast<IWiaTransferCallback*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&refs);
        if (!n) delete this;
        return n;
    }
    HRESULT FinishPage() {
        if (FAILED(error)) return error;
        if (!stream.Get()) return S_OK;
        if (job->Cancelled()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        LARGE_INTEGER start{};
        error = stream->Seek(start, STREAM_SEEK_SET, nullptr);
        if (FAILED(error)) return error;
        // Decode only completed driver streams. Never substitute a blank bitmap.
        Gdiplus::Bitmap bitmap(stream, FALSE);
        if (bitmap.GetLastStatus() != Gdiplus::Ok || !bitmap.GetWidth() || !bitmap.GetHeight() ||
            !pdf->AddPageFromGdiplusBitmap(&bitmap, (float)job->dpi)) {
            error = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (SUCCEEDED(error)) InterlockedIncrement(&job->pages);
        return error;
    }
    HRESULT STDMETHODCALLTYPE GetNextStream(LONG, BSTR, BSTR, IStream** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (job->Cancelled()) return S_FALSE;
        if (FAILED(error)) return error;
        // END_OF_STREAM must finish the preceding page before another begins.
        if (stream.Get()) return error = E_UNEXPECTED;
        error = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        if (FAILED(error)) return error;
        *out = stream.Get();
        (*out)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TransferCallback(LONG, WiaTransferParams* params) override {
        if (job->Cancelled()) return S_FALSE;
        if (!params) return E_POINTER;
        if (FAILED(params->hrErrorStatus)) return error = params->hrErrorStatus;
        if (params->lMessage == WIA_TRANSFER_MSG_END_OF_STREAM) {
            error = FinishPage();
            stream = nullptr;
        }
        return error;
    }
};

static HRESULT AcquireWia(ScanJob* job, PdfCreator& pdf) {
    if (!job->cancel) return E_OUTOFMEMORY;
    if (job->Cancelled()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    AutoReleaseComPtr<IWiaDevMgr2> manager;
    HRESULT hr = CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(hr)) return hr;
    BSTR id = SysAllocString(ToWStrTemp(job->device).s);
    if (!id) return E_OUTOFMEMORY;
    AutoReleaseComPtr<IWiaItem2> root;
    hr = manager->CreateDevice(0, id, &root);
    SysFreeString(id);
    if (FAILED(hr)) return hr;
    AutoReleaseComPtr<IWiaItem2> item;
    hr = FindWiaSource(root, job->source == 0 ? WIA_CATEGORY_FLATBED : WIA_CATEGORY_FEEDER, &item);
    if (FAILED(hr)) return hr;
    AutoReleaseComPtr<IWiaPropertyStorage> props;
    hr = item->QueryInterface(IID_PPV_ARGS(&props));
    if (FAILED(hr)) return hr;
    if (job->source != 0) {
        // WIA 2.0 selects the feeder by item category, not the obsolete FEEDER flag.
        hr = SetWiaLong(props, WIA_IPS_DOCUMENT_HANDLING_SELECT, job->source == 2 ? DUPLEX : FRONT_ONLY);
        if (FAILED(hr)) return hr;
        hr = SetWiaLong(props, WIA_IPS_PAGES, ALL_PAGES);
        if (FAILED(hr)) return hr;
    }
    hr = SetWiaLong(props, WIA_IPS_CUR_INTENT,
                    job->color == 0   ? WIA_INTENT_IMAGE_TYPE_COLOR
                    : job->color == 1 ? WIA_INTENT_IMAGE_TYPE_GRAYSCALE
                                      : WIA_INTENT_IMAGE_TYPE_TEXT);
    if (FAILED(hr)) return hr;
    hr = SetWiaLong(props, WIA_IPS_XRES, job->dpi);
    if (FAILED(hr)) return hr;
    hr = SetWiaLong(props, WIA_IPS_YRES, job->dpi);
    if (FAILED(hr)) return hr;
    hr = SetWiaLong(props, WIA_IPA_DATATYPE,
                    job->color == 0   ? WIA_DATA_COLOR
                    : job->color == 1 ? WIA_DATA_GRAYSCALE
                                      : WIA_DATA_THRESHOLD);
    if (FAILED(hr)) return hr;
    GUID format = WiaImgFmt_BMP;
    PROPSPEC spec{};
    spec.ulKind = PRSPEC_PROPID;
    spec.propid = WIA_IPA_FORMAT;
    PROPVARIANT value{};
    value.vt = VT_CLSID;
    value.puuid = &format;
    hr = props->WriteMultiple(1, &spec, &value, WIA_IPA_FIRST);
    if (FAILED(hr)) return hr;
    AutoReleaseComPtr<IWiaTransfer> transfer;
    hr = item->QueryInterface(IID_PPV_ARGS(&transfer));
    if (FAILED(hr)) return hr;
    auto* callback = new ScanTransfer(job, &pdf);
    hr = transfer->Download(0, callback);
    if (FAILED(callback->error)) hr = callback->error;
    if (callback->stream.Get() && (SUCCEEDED(hr) || hr == WIA_ERROR_PAPER_EMPTY)) hr = E_UNEXPECTED;
    callback->Release();
    if (job->Cancelled() || hr == S_FALSE) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    // PAPER_EMPTY is normal only after at least one complete feeder page.
    if (hr == WIA_ERROR_PAPER_EMPTY && job->source != 0 && job->pages > 0) hr = S_OK;
    if (SUCCEEDED(hr) && !job->pages) hr = HRESULT_FROM_WIN32(ERROR_NO_DATA);
    return hr;
}

static void RunScan(ScanJob* job) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    job->error = init;
    if (SUCCEEDED(init)) {
        PdfCreator pdf;
        job->error = AcquireWia(job, pdf);
        if (SUCCEEDED(job->error)) {
            file::AtomicFile output(job->destination);
            if (job->Cancelled())
                job->error = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            else if (!pdf.SaveToFile(output.tempPath))
                job->error = E_FAIL;
            else if (job->Cancelled())
                job->error = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            else if (!output.Commit())
                job->error = E_FAIL;
        }
        CoUninitialize();
    }
    InterlockedExchange(&job->done, 1);
    job->Release();
}

// Map the output rectangle into a convex source quadrilateral.
Pixmap* PerspectiveCropPixmap(const Pixmap* src, const PerspectiveCorners& corners) {
    if (!src || !src->data || src->width < 10 || src->height < 10) return nullptr;
    if (src->format != PixmapFormat::BGRA8 && src->format != PixmapFormat::BGR8) return nullptr;
    PointF q[] = {corners.tl, corners.tr, corners.br, corners.bl};
    for (int i = 0; i < 4; i++) {
        PointF a = q[i], b = q[(i + 1) % 4], c = q[(i + 2) % 4];
        if (!_finite(a.x) || !_finite(a.y) || a.x < 0 || a.y < 0 || a.x > src->width - 1 || a.y > src->height - 1)
            return nullptr;
        if ((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x) <= 0.01f) return nullptr;
    }
    double dx1 = q[1].x - q[2].x, dx2 = q[3].x - q[2].x;
    double dy1 = q[1].y - q[2].y, dy2 = q[3].y - q[2].y;
    double dx3 = q[0].x - q[1].x + q[2].x - q[3].x;
    double dy3 = q[0].y - q[1].y + q[2].y - q[3].y;
    double det = dx1 * dy2 - dx2 * dy1;
    if (fabs(det) < 0.000001) return nullptr;
    double g = (dx3 * dy2 - dx2 * dy3) / det;
    double h = (dx1 * dy3 - dx3 * dy1) / det;
    if (1 + g <= 0.000001 || 1 + h <= 0.000001 || 1 + g + h <= 0.000001) return nullptr;
    double a = q[1].x - q[0].x + g * q[1].x;
    double b = q[3].x - q[0].x + h * q[3].x;
    double d = q[1].y - q[0].y + g * q[1].y;
    double e = q[3].y - q[0].y + h * q[3].y;
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

    int dstW = std::max(10, (int)roundf(std::max(topW, botW)) + 1);
    int dstH = std::max(10, (int)roundf(std::max(leftH, rightH)) + 1);

    Pixmap* dst = AllocPixmapDIB(dstW, dstH);
    if (!dst) return nullptr;

    auto sampleSrc = [src, bpp](float fx, float fy, u8* outBgra) {
        fx = std::max(0.f, std::min((float)src->width - 1, fx));
        fy = std::max(0.f, std::min((float)src->height - 1, fy));
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

            double denominator = g * s + h * t + 1;
            float sx = (float)((a * s + b * t + q[0].x) / denominator);
            float sy = (float)((d * s + e * t + q[0].y) / denominator);

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
    ScanJob* job = nullptr;
    bool offerOcr = false;
    ~ScanDialog() override {
        if (job) {
            SetEvent(job->cancel);
            job->Release();
        }
        for (auto& device : devices) {
            str::Free(device.id);
            str::Free(device.name);
        }
    }
    void Poll(WindowBase::TimerEvent*);
    void Closed(WindowBase::CloseEvent*);
    void OnScan(VirtMouseEvent* = nullptr);
    void OnCancel(VirtMouseEvent* = nullptr);
};

void ScanDialog::OnCancel(VirtMouseEvent*) {
    if (job && !InterlockedCompareExchange(&job->done, 0, 0)) {
        SetEvent(job->cancel);
        status->SetText(Tr("Cancelling scan..."));
        return;
    }
    Close();
}

void ScanDialog::Closed(WindowBase::CloseEvent* ev) {
    if (job && !InterlockedCompareExchange(&job->done, 0, 0)) {
        ev->e->didHandle = true;
        OnCancel();
        return;
    }
    ScheduleDelete();
}

void ScanDialog::Poll(WindowBase::TimerEvent*) {
    if (!job || !InterlockedCompareExchange(&job->done, 0, 0)) return;
    KillTimer(hwnd, 1);
    if (FAILED(job->error)) {
        status->SetText(
            job->error == HRESULT_FROM_WIN32(ERROR_CANCELLED)
                ? Tr("Scan cancelled. No PDF saved.")
                : fmt(Tr("Scan failed (0x%08x). Check device, paper and supported settings. No PDF saved.").s,
                      (uint)job->error));
        job->Release();
        job = nullptr;
        btnScan->SetIsEnabled(true);
        return;
    }
    Str destination = str::Dup(job->destination);
    Close();
    if (win) {
        LoadArgs args(destination, win);
        LoadDocument(&args);
        if (offerOcr) ShowOcrDialog(win);
    }
    str::Free(destination);
}

void ScanDialog::OnScan(VirtMouseEvent*) {
    if (job) return;
    if (len(devices) == 0) return;
    int devIdx = CbGetCurrentSelection(ddDevice);
    if (devIdx < 0 || devIdx >= len(devices)) devIdx = 0;

    int dpi = 300;
    int resSel = CbGetCurrentSelection(ddResolution);
    if (resSel == 0)
        dpi = 75;
    else if (resSel == 1)
        dpi = 150;
    else if (resSel == 2)
        dpi = 300;
    else if (resSel == 3)
        dpi = 600;

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

    job = new ScanJob();
    job->device = str::Dup(devices[devIdx].id);
    job->destination = str::Dup(ToUtf8Temp(WStr(savePath)));
    job->dpi = dpi;
    job->color = colorSel;
    job->source = sourceSel;
    offerOcr = doOcr;
    InterlockedIncrement(&job->refs);
    ThreadHandle thread = StartThread(MkFunc0(RunScan, job), StrL("WIA scan"));
    if (!thread) {
        job->Release();
        job->error = E_FAIL;
        InterlockedExchange(&job->done, 1);
    }
    SafeCloseThreadHandle(&thread);
    SetTimer(hwnd, 1, 150, nullptr);
}

bool ScanDialog::Create(MainWindow* parentWin) {
    win = parentWin;
    onClose = MkMethod1<ScanDialog, WindowBase::CloseEvent*, &ScanDialog::Closed>(this);
    onTimer = MkMethod1<ScanDialog, WindowBase::TimerEvent*, &ScanDialog::Poll>(this);
    bool hasScanners = EnumerateWiaScanners(devices);
    if (!hasScanners || len(devices) == 0) {
        int res = MessageBox(parentWin ? parentWin->hwndFrame : nullptr,
                             L"No scanner detected. Make sure your scanner is connected, turned on, and Windows WIA "
                             L"drivers are installed.\n\nWould you like to import an image instead?",
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
    Edit* cornerEdits[4]{};
    bool ReadCorners(PerspectiveCorners& corners);
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

bool CameraImportDialog::ReadCorners(PerspectiveCorners& corners) {
    PointF* points[] = {&corners.tl, &corners.tr, &corners.br, &corners.bl};
    for (int i = 0; i < 4; i++) {
        TempStr value = cornerEdits[i]->GetTextTemp();
        char extra = 0;
        if (sscanf(CStrTemp(value), " %f , %f %c", &points[i]->x, &points[i]->y, &extra) != 2) return false;
    }
    return true;
}

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
        if (!ReadCorners(corners)) {
            status->SetText(Tr("Enter four corners as x,y pixels, clockwise from top left."));
            return;
        }
        workPx = PerspectiveCropPixmap(loadedPx, corners);
        if (!workPx) {
            status->SetText(Tr("Invalid corners. Use a convex quadrilateral inside the image."));
            return;
        }
    }
    if (!workPx) {
        workPx = AllocPixmapDIB(loadedPx->width, loadedPx->height);
        if (workPx) CopyPixmapRows(workPx, loadedPx->data, loadedPx->stride);
    }
    if (!workPx) return;

    int enhSel = CbGetCurrentSelection(ddEnhance);
    if (enhSel == 1)
        EnhanceGrayscalePixmap(workPx);
    else if (enhSel == 2)
        EnhanceContrastPixmap(workPx, 1.4f);
    else if (enhSel == 3)
        EnhanceThresholdPixmap(workPx, -1);

    if (cbDeskew && cbDeskew->IsChecked()) {
        DeskewPixmap(workPx);
    }

    PdfCreator creator;
    Gdiplus::Bitmap* bmp = NewGdiplusBitmapFromPixmap(workPx);
    bool added = bmp && creator.AddPageFromGdiplusBitmap(bmp, 300);
    delete bmp;
    // NewGdiplusBitmapFromPixmap takes ownership of workPx.

    Str destUtf8 = ToUtf8Temp(WStr(savePath));
    file::AtomicFile output(destUtf8);
    bool ok = added && creator.SaveToFile(output.tempPath) && output.Commit();
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
        MessageBoxWarning(parentWin ? parentWin->hwndFrame : nullptr, Tr("Could not load the selected image file."),
                          Tr("Import Camera Image"));
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
    status->SetText(fmt(Tr("Image loaded: %d x %d pixels. Apply perspective crop and enhancements.").s, loadedPx->width,
                        loadedPx->height));
    box->AddChild(status);

    cbCrop = new Checkbox();
    Checkbox::CreateArgs cropArgs;
    cropArgs.parent = hwnd;
    cropArgs.text = Tr("Perspective correction using corners below (x,y pixels)");
    cropArgs.font = font;
    cbCrop->Create(cropArgs);
    cbCrop->SetIsChecked(false);
    box->AddChild(cbCrop);
    Str labels[] = {Tr("Top left"), Tr("Top right"), Tr("Bottom right"), Tr("Bottom left")};
    Point defaults[] = {
        {0, 0}, {loadedPx->width - 1, 0}, {loadedPx->width - 1, loadedPx->height - 1}, {0, loadedPx->height - 1}};
    for (int i = 0; i < 4; i++) {
        auto* row = new HBox();
        row->gap = DpiScale(8);
        row->AddChild(NewVirtText({.s = labels[i], .font = font}));
        cornerEdits[i] = new Edit();
        Edit::CreateArgs edit;
        edit.parent = hwnd;
        edit.font = font;
        edit.withBorder = true;
        cornerEdits[i]->Create(edit);
        cornerEdits[i]->SetText(fmt("%d,%d", defaults[i].x, defaults[i].y));
        row->AddChild(cornerEdits[i]);
        box->AddChild(row);
    }

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
    cbDeskew->SetIsChecked(false);
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
    RECT rect = {0, 0, DpiScale(580), DpiScale(580)};
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
    ofn.lpstrFilter =
        L"Image files "
        L"(*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp)\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp\0All files "
        L"(*.*)\0*.*\0\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    auto* dlg = new CameraImportDialog();
    if (!dlg->Create(win, ToUtf8Temp(WStr(filePath)))) delete dlg;
}

#if IS_DEBUG
bool ScannerWia_UnitTests() {
    Pixmap* src = AllocPixmapDIB(101, 101);
    if (!src) return false;
    for (int y = 0; y < 101; y++) {
        for (int x = 0; x < 101; x++) {
            u8* p = src->data + y * src->stride + x * 4;
            p[0] = (u8)x;
            p[1] = (u8)y;
            p[2] = 64;
            p[3] = 255;
        }
    }
    PerspectiveCorners identity{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    Pixmap* copy = PerspectiveCropPixmap(src, identity);
    bool ok = copy && copy->width == 101 && copy->height == 101;
    if (ok)
        for (int y = 0; y < 101; y++) {
            if (memcmp(src->data + y * src->stride, copy->data + y * copy->stride, 404)) ok = false;
        }
    FreePixmap(copy);
    // The trapezoid center must differ from bilinear interpolation.
    PerspectiveCorners trapezoid{{10, 0}, {90, 0}, {70, 100}, {30, 100}};
    copy = PerspectiveCropPixmap(src, trapezoid);
    if (!copy)
        ok = false;
    else {
        const u8* center = copy->data + (copy->height / 2) * copy->stride + (copy->width / 2) * 4;
        // For these corners the projective center is (50, 66.6667).
        ok = ok && abs(center[0] - 50) <= 1 && abs(center[1] - 67) <= 2;
    }
    FreePixmap(copy);
    PerspectiveCorners crossed{{0, 0}, {100, 100}, {100, 0}, {0, 100}};
    copy = PerspectiveCropPixmap(src, crossed);
    ok = ok && !copy;
    FreePixmap(copy);
    FreePixmap(src);
    printf("Camera projective pixels / identity edges / invalid corners: %s\n", ok ? "PASS" : "FAIL");

    ScanJob job;
    PdfCreator pdf;
    ScanTransfer transfer(&job, &pdf);
    AutoReleaseComPtr<IStream> stream;
    ok = ok && transfer.GetNextStream(0, nullptr, nullptr, &stream) == S_OK;
    Gdiplus::Bitmap bitmap(32, 32, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.Clear(Gdiplus::Color(255, 20, 40, 60));
    }
    CLSID encoder = GetGdiPlusEncoderClsid(WStrL(L"image/bmp"));
    ok = ok && bitmap.Save(stream, &encoder) == Gdiplus::Ok;
    WiaTransferParams params{};
    params.lMessage = WIA_TRANSFER_MSG_END_OF_STREAM;
    ok = ok && transfer.TransferCallback(0, &params) == S_OK && job.pages == 1;
    stream = nullptr;
    ok = ok && transfer.GetNextStream(0, nullptr, nullptr, &stream) == S_OK;
    // An empty driver stream must fail, without appending another page.
    ok = ok && FAILED(transfer.TransferCallback(0, &params)) && job.pages == 1;
    SetEvent(job.cancel);
    stream = nullptr;
    ok = ok && transfer.GetNextStream(0, nullptr, nullptr, &stream) == S_FALSE && !stream.Get();
    printf("WIA pixel stream / empty transfer / cancellation: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}
#endif
