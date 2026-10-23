/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/AutoWin.h"
#include "base/File.h"
#include "base/GuessFileType.h"
#include "base/Pixmap.h"
#include "base/Win.h"
#include <roapi.h>
#include <robuffer.h>
#include <windows.media.ocr.h>
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "Translations.h"
#include "DarkMode.h"
#include "Theme.h"
#include "SumatraConfig.h"
#include "Ocr.h"
#include "SumatraLog.h"

#pragma comment(lib, "runtimeobject.lib")

namespace imaging = ABI::Windows::Graphics::Imaging;
namespace ocr = ABI::Windows::Media::Ocr;
namespace streams = ABI::Windows::Storage::Streams;

struct OcrString {
    HSTRING value = nullptr;
    ~OcrString() { WindowsDeleteString(value); }
};

template <typename T>
static HRESULT OcrFactory(const WCHAR* name, AutoReleaseComPtr<T>& out) {
    OcrString s;
    HRESULT hr = WindowsCreateString(name, (UINT32)wcslen(name), &s.value);
    if (FAILED(hr)) return hr;
    return RoGetActivationFactory(s.value, __uuidof(T), (void**)&out);
}

// Keep WinRT interfaces and their apartment entirely on the worker thread.
struct OcrReader {
    HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
    AutoReleaseComPtr<ocr::IOcrEngine> engine;
    AutoReleaseComPtr<imaging::ISoftwareBitmapStatics> bitmaps;
    AutoReleaseComPtr<streams::IBufferFactory> buffers;
    UINT32 maxDimension = 0;
    bool noLanguage = false;

    ~OcrReader() {
        engine = nullptr;
        bitmaps = nullptr;
        buffers = nullptr;
        if (SUCCEEDED(init)) RoUninitialize();
    }

    HRESULT Open() {
        if (FAILED(init)) return init;
        AutoReleaseComPtr<ocr::IOcrEngineStatics> factory;
        HRESULT hr = OcrFactory(RuntimeClass_Windows_Media_Ocr_OcrEngine, factory);
        if (FAILED(hr)) return hr;
        hr = factory->get_MaxImageDimension(&maxDimension);
        if (FAILED(hr)) return hr;
        hr = factory->TryCreateFromUserProfileLanguages(&engine);
        if (FAILED(hr)) return hr;
        // A pack can be installed without being in the user's preferred languages.
        if (!engine.Get()) {
            AutoReleaseComPtr<
                ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::Globalization::Language*>>
                languages;
            hr = factory->get_AvailableRecognizerLanguages(&languages);
            if (FAILED(hr)) return hr;
            UINT32 count = 0;
            hr = languages->get_Size(&count);
            if (FAILED(hr)) return hr;
            if (count) {
                AutoReleaseComPtr<ABI::Windows::Globalization::ILanguage> language;
                hr = languages->GetAt(0, &language);
                if (FAILED(hr)) return hr;
                hr = factory->TryCreateFromLanguage(language, &engine);
                if (FAILED(hr)) return hr;
            }
        }
        noLanguage = !engine.Get();
        if (noLanguage) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        hr = OcrFactory(RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap, bitmaps);
        if (FAILED(hr)) return hr;
        return OcrFactory(RuntimeClass_Windows_Storage_Streams_Buffer, buffers);
    }

    HRESULT Recognize(Pixmap* px, HANDLE cancel, str::Builder& text) {
        if (!px || !px->data || px->format != PixmapFormat::BGRA8 || px->width <= 0 || px->height <= 0 ||
            (UINT32)px->width > maxDimension || (UINT32)px->height > maxDimension)
            return E_INVALIDARG;
        u64 bytes = (u64)px->width * px->height * 4;
        if (bytes > UINT_MAX) return E_INVALIDARG;
        AutoReleaseComPtr<streams::IBuffer> buffer;
        HRESULT hr = buffers->Create((UINT32)bytes, &buffer);
        if (FAILED(hr)) return hr;
        AutoReleaseComPtr<Windows::Storage::Streams::IBufferByteAccess> access;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&access));
        if (FAILED(hr)) return hr;
        BYTE* dst = nullptr;
        hr = access->Buffer(&dst);
        if (FAILED(hr)) return hr;
        for (int y = 0; y < px->height; y++) {
            memcpy(dst + (size_t)y * px->width * 4, px->data + (size_t)y * px->stride, (size_t)px->width * 4);
        }
        hr = buffer->put_Length((UINT32)bytes);
        if (FAILED(hr)) return hr;
        AutoReleaseComPtr<imaging::ISoftwareBitmap> bitmap;
        hr = bitmaps->CreateCopyWithAlphaFromBuffer(buffer, imaging::BitmapPixelFormat_Bgra8, px->width, px->height,
                                                    imaging::BitmapAlphaMode_Ignore, &bitmap);
        if (FAILED(hr)) return hr;
        AutoReleaseComPtr<ABI::Windows::Foundation::IAsyncOperation<ocr::OcrResult*>> operation;
        hr = engine->RecognizeAsync(bitmap, &operation);
        if (FAILED(hr)) return hr;
        AutoReleaseComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
        hr = operation->QueryInterface(IID_PPV_ARGS(&info));
        if (FAILED(hr)) return hr;
        ABI::Windows::Foundation::AsyncStatus status;
        ULONGLONG started = GetTickCount64();
        constexpr ULONGLONG kOcrTimeoutMs = 120000;
        for (;;) {
            hr = info->get_Status(&status);
            if (FAILED(hr) || status != ABI::Windows::Foundation::AsyncStatus::Started) break;
            if (GetTickCount64() - started > kOcrTimeoutMs) {
                info->Cancel();
                return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
            if (WaitForSingleObject(cancel, 50) == WAIT_OBJECT_0) {
                info->Cancel();
                return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            }
        }
        if (FAILED(hr)) return hr;
        AutoReleaseComPtr<ocr::IOcrResult> result;
        hr = operation->GetResults(&result);
        if (FAILED(hr)) return hr;
        AutoReleaseComPtr<ABI::Windows::Foundation::Collections::IVectorView<ocr::OcrLine*>> lines;
        hr = result->get_Lines(&lines);
        if (FAILED(hr)) return hr;
        UINT32 count = 0;
        hr = lines->get_Size(&count);
        if (FAILED(hr)) return hr;
        for (UINT32 i = 0; i < count; i++) {
            AutoReleaseComPtr<ocr::IOcrLine> line;
            hr = lines->GetAt(i, &line);
            if (FAILED(hr)) return hr;
            OcrString value;
            hr = line->get_Text(&value.value);
            if (FAILED(hr)) return hr;
            UINT32 n = 0;
            const WCHAR* s = WindowsGetStringRawBuffer(value.value, &n);
            text.Append(ToUtf8Temp(WStr(s, (int)n)));
            text.Append(StrL("\r\n"));
        }
        return S_OK;
    }
};

static float OcrZoom(RectF page, float dpi, UINT32 maxDimension) {
    float side = std::max(page.dx, page.dy);
    if (!_finite(side) || side <= 0 || !maxDimension || !_finite(dpi) || dpi <= 0) return 0;
    // Leave room for the renderer rounding outward to integral pixels.
    return std::min(300.f / dpi, (float)(maxDimension - 1) / side);
}

struct OcrJob {
    LONG refs = 1;
    LONG done = 0;
    LONG page = 0;
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    EngineBase* source = nullptr;
    int first = 1;
    int last = 1;
    int rotation = 0;
    HRESULT error = S_OK;
    bool noLanguage = false;
    str::Builder text;

    ~OcrJob() {
        if (source) source->Release();
        if (cancel) CloseHandle(cancel);
    }
    void Release() {
        if (!InterlockedDecrement(&refs)) delete this;
    }
};

static void RunOcr(OcrJob* job) {
    OcrReader reader;
    job->error = reader.Open();
    job->noLanguage = reader.noLanguage;
    EngineBase* engine = nullptr;
    if (SUCCEEDED(job->error)) {
        engine = job->source->Clone();
        if (!engine) job->error = E_FAIL;
    }
    for (int page = job->first; SUCCEEDED(job->error) && page <= job->last; page++) {
        if (WaitForSingleObject(job->cancel, 0) == WAIT_OBJECT_0) {
            job->error = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            break;
        }
        InterlockedExchange(&job->page, page);
        float zoom = OcrZoom(engine->PageMediabox(page), engine->fileDPI, reader.maxDimension);
        if (zoom <= 0) {
            job->error = E_INVALIDARG;
            break;
        }
        RenderPageArgs args(page, zoom, job->rotation, nullptr, RenderTarget::Export);
        Pixmap* px = PixmapToBgra(engine->RenderPage(args));
        str::Builder pageText;
        job->error = reader.Recognize(px, job->cancel, pageText);
        FreePixmap(px);
        if (FAILED(job->error)) break;
        if (job->first != job->last) job->text.Append(fmt("--- %s %d ---\r\n", StrL("Page"), page));
        job->text.Append(ToStr(pageText));
        job->text.Append(StrL("\r\n"));
    }
    if (engine) engine->Release();
    logf("OCR finished: pages %d-%d, HRESULT 0x%x, bytes %d\n", job->first, job->last, (uint)job->error,
         len(ToStr(job->text)));
    InterlockedExchange(&job->done, 1);
    job->Release();
}

struct OcrDialog : WindowBase {
    OcrJob* job = new OcrJob();
    VirtFixedLinesText* status = nullptr;
    Edit* result = nullptr;
    VirtButton* start = nullptr;
    VirtButton* copy = nullptr;
    VirtButton* save = nullptr;
    VirtButton* all = nullptr;
    VirtButton* cancel = nullptr;
    int current = 1;
    int pageCount = 0;
    bool running = false;
    bool saving = false;
    bool closing = false;

    ~OcrDialog() override {
        SetEvent(job->cancel);
        job->Release();
    }
    bool Create(MainWindow*);
    void Start(VirtMouseEvent* = nullptr);
    void Poll(WindowBase::TimerEvent*);
    void Copy(VirtMouseEvent*);
    void Save(VirtMouseEvent*);
    void Cancel(VirtMouseEvent*);
    void Closed(WindowBase::CloseEvent*);
    void Destroyed(WindowBase::DestroyEvent*);
};

void OcrDialog::Closed(WindowBase::CloseEvent*) {
    if (saving) {
        closing = true;
        return;
    }
    SetEvent(job->cancel);
    ScheduleDelete();
}

void OcrDialog::Destroyed(WindowBase::DestroyEvent*) {
    SetEvent(job->cancel);
    if (!saving && !deleteScheduled) ScheduleDelete();
}

void OcrDialog::Cancel(VirtMouseEvent*) {
    if (!running) {
        Close();
        return;
    }
    SetEvent(job->cancel);
    status->SetText(Tr("Cancelling OCR..."));
    cancel->SetIsEnabled(false);
}

void OcrDialog::Start(VirtMouseEvent* ev) {
    if (running) return;
    bool allPages = ev && ev->target == all;
    job->first = allPages ? 1 : current;
    job->last = allPages ? pageCount : current;
    job->error = S_OK;
    job->noLanguage = false;
    ResetEvent(job->cancel);
    InterlockedExchange(&job->done, 0);
    InterlockedExchange(&job->page, 0);
    job->text.Reset();
    result->SetText({});
    copy->SetIsEnabled(false);
    save->SetIsEnabled(false);
    all->SetIsEnabled(false);
    start->SetIsEnabled(false);
    cancel->SetText(Tr("Cancel"));
    running = true;
    status->SetText(Tr("Starting OCR..."));
    InterlockedIncrement(&job->refs);
    ThreadHandle thread = StartThread(MkFunc0(RunOcr, job), StrL("OCR"));
    if (!thread) {
        job->Release();
        job->error = E_FAIL;
        InterlockedExchange(&job->done, 1);
    }
    SafeCloseThreadHandle(&thread);
    SetTimer(hwnd, 1, 100, nullptr);
}

void OcrDialog::Poll(WindowBase::TimerEvent*) {
    if (!InterlockedCompareExchange(&job->done, 0, 0)) {
        int page = InterlockedCompareExchange(&job->page, 0, 0);
        if (page) status->SetText(fmt(Tr("OCR: page %d of %d").s, page - job->first + 1, job->last - job->first + 1));
        return;
    }
    KillTimer(hwnd, 1);
    running = false;
    start->SetIsEnabled(true);
    all->SetIsEnabled(pageCount > 1);
    cancel->SetText(Tr("Close"));
    cancel->SetIsEnabled(true);
    if (job->noLanguage) {
        status->SetText(
            Tr("No Windows OCR language pack is installed. In Windows Settings > Time & language > Language & region, "
               "add a language and install its Basic typing / OCR feature, then try again."));
    } else if (job->error == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        status->SetText(Tr("OCR cancelled. Completed pages are available below."));
    } else if (FAILED(job->error)) {
        status->SetText(
            fmt(Tr("OCR failed (0x%x). Windows OCR may be unavailable. Completed pages are available below.").s,
                (uint)job->error));
    } else {
        status->SetText(str::IsEmptyOrWhiteSpace(ToStr(job->text))
                            ? Tr("No text was recognized.")
                            : Tr("OCR complete. Review the text before using it."));
    }
    Str text = ToStr(job->text);
    result->SetText(text);
    bool hasText = !str::IsEmptyOrWhiteSpace(text);
    copy->SetIsEnabled(hasText && HasPermission(Perm::CopySelection));
    save->SetIsEnabled(hasText && CanAccessDisk());
}

void OcrDialog::Copy(VirtMouseEvent*) {
    if (HasPermission(Perm::CopySelection) && !CopyTextToClipboard(ToStr(job->text))) {
        MessageBoxWarning(hwnd, Tr("Couldn't copy text to the clipboard."), Tr("Scan & OCR"));
    }
}

void OcrDialog::Save(VirtMouseEvent*) {
    if (!CanAccessDisk() || saving) return;
    WCHAR path[MAX_PATH] = L"OCR.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.lpstrFilter = L"Text files (*.txt)\0*.txt\0\0";
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    saving = true;
    bool picked = GetSaveFileNameW(&ofn);
    saving = false;
    if (closing || !IsWindow(hwnd)) {
        ScheduleDelete();
        return;
    }
    if (!picked) return;
    if (!file::WriteFile(ToUtf8Temp(path), ToStr(job->text))) {
        MessageBoxWarning(hwnd, Tr("Couldn't save the OCR text."), Tr("Scan & OCR"));
    }
}

bool OcrDialog::Create(MainWindow* win) {
    auto* dm = win->AsFixed();
    auto* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine || (!gDisableDocumentRestrictions && !engine->allowsCopyingText) || !job->cancel) return false;
    engine->AddRef();
    job->source = engine;
    pageCount = engine->PageCount();
    job->last = pageCount;
    job->rotation = dm->GetRotation();
    current = dm->CurrentPageNo();
    if (current < 1 || current > job->last) return false;
    closeOnEsc = true;
    onClose = MkMethod1<OcrDialog, WindowBase::CloseEvent*, &OcrDialog::Closed>(this);
    onDestroy = MkMethod1<OcrDialog, WindowBase::DestroyEvent*, &OcrDialog::Destroyed>(this);
    onTimer = MkMethod1<OcrDialog, WindowBase::TimerEvent*, &OcrDialog::Poll>(this);
    CreateCustomArgs args;
    args.owner = win->hwndFrame;
    args.title = Tr("Scan & OCR");
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
    status->nLines = 3;
    status->SetText(Tr("Recognize text using installed Windows OCR languages."));
    box->AddChild(status);
    Edit::CreateArgs edit;
    edit.parent = hwnd;
    edit.font = font;
    edit.isMultiLine = true;
    edit.withBorder = true;
    edit.idealSizeLines = 5;
    result = new Edit();
    result->Create(edit);
    SendMessageW(result->hwnd, EM_SETREADONLY, TRUE, 0);
    SendMessageW(result->hwnd, EM_SETLIMITTEXT, 0, 0);
    box->AddChild(result, 1);
    box->AddChild(NewVirtText(
        {.s = Tr("Text can be selected and copied, or saved as UTF-8. The PDF is unchanged."), .font = font}));
    auto* buttons = new HBox();
    buttons->gap = DpiScale(8);
    box->AddChild(buttons);
    start = NewThemedButton(hwnd, Tr("Current page"), font, true);
    start->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Start>(this);
    buttons->AddChild(start);
    all = NewThemedButton(hwnd, Tr("All pages"), font, false);
    all->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Start>(this);
    all->SetIsEnabled(job->last > 1);
    buttons->AddChild(all);
    copy = NewThemedButton(hwnd, Tr("Copy text"), font, false);
    copy->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Copy>(this);
    copy->SetIsEnabled(false);
    buttons->AddChild(copy);
    save = NewThemedButton(hwnd, Tr("Save as .txt..."), font, false);
    save->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Save>(this);
    save->SetIsEnabled(false);
    buttons->AddChild(save);
    cancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
    cancel->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Cancel>(this);
    buttons->AddChild(cancel);
    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(760), DpiScale(540)};
    AdjustWindowRectEx(&rect, args.style, FALSE, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    UpdateTheme();
    HwndCenterDialog(hwnd, win->hwndFrame);
    SetIsVisible(true);
    SetFocusTo(start);
    return true;
}

void ShowOcrDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded() || !HasPermission(Perm::CopySelection)) return;
    auto* dm = win->AsFixed();
    if (dm && EngineHasUnsavedAnnotations(dm->GetEngine())) {
        MessageBoxWarning(win->hwndFrame, Tr("Save your document changes before running OCR."), Tr("Scan & OCR"));
        return;
    }
    auto* dlg = new OcrDialog();
    if (!dlg->Create(win)) delete dlg;
}

#if IS_DEBUG
static void OcrSmokeTest(bool* ok) {
    OcrReader reader;
    HRESULT hr = reader.Open();
    if (reader.noLanguage || hr == REGDB_E_CLASSNOTREG) {
        printf("OCR smoke: SKIP (Windows OCR or language pack unavailable)\n");
        *ok = true;
        return;
    }
    if (FAILED(hr)) return;
    Pixmap* px = AllocPixmapDIB(900, 180);
    if (!px) return;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBitmap = SelectObject(dc, px->hbmp);
    RECT rect = {0, 0, px->width, px->height};
    FillRect(dc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HFONT font = CreateFontW(-64, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 30, 40, L"12345 67890", 11);
    GdiFlush();
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteObject(font);
    DeleteDC(dc);
    AutoCloseHandle cancel(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    str::Builder text;
    hr = reader.Recognize(px, cancel, text);
    FreePixmap(px);
    *ok = SUCCEEDED(hr) && str::Contains(ToStr(text), StrL("12345"));
    printf("OCR smoke: HRESULT 0x%x, text: %.*s\n", (uint)hr, len(ToStr(text)), ToStr(text).s);
}

bool Ocr_UnitTests() {
    if (OcrZoom({0, 0, 0, 0}, 72, 4096) != 0) return false;
    float zoom = OcrZoom({0, 0, 72000, 36000}, 72, 4096);
    if (zoom <= 0 || ceilf(zoom * 72000) > 4096) return false;
    if (OcrZoom({0, 0, 612, 792}, 72, 4096) != 300.f / 72) return false;
    bool ok = false;
    ThreadHandle thread = StartThread(MkFunc0(OcrSmokeTest, &ok), StrL("OCR smoke"));
    if (!thread) return false;
    JoinThread(&thread, INFINITE);
    return ok;
}
#endif
