/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/AutoWin.h"
#include "base/File.h"
#include "base/GuessFileType.h"
#include "base/Pixmap.h"
#include "base/GdiPlusUtil.h"
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
#include "AppSettings.h"
#include "Flags.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "PdfCreator.h"
#include "ProgressUpdateUI.h"
#include "TextSelection.h"
#include "TextSearch.h"
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

    HRESULT Open(Str tag = {}) {
        if (FAILED(init)) return init;
        AutoReleaseComPtr<ocr::IOcrEngineStatics> factory;
        HRESULT hr = OcrFactory(RuntimeClass_Windows_Media_Ocr_OcrEngine, factory);
        if (FAILED(hr)) return hr;
        hr = factory->get_MaxImageDimension(&maxDimension);
        if (FAILED(hr)) return hr;
        if (len(tag)) {
            AutoReleaseComPtr<ABI::Windows::Globalization::ILanguageFactory> languages;
            hr = OcrFactory(RuntimeClass_Windows_Globalization_Language, languages);
            if (FAILED(hr)) return hr;
            OcrString languageTag;
            TempWStr wide = ToWStrTemp(tag);
            hr = WindowsCreateString(wide.s, (UINT32)len(wide), &languageTag.value);
            if (FAILED(hr)) return hr;
            AutoReleaseComPtr<ABI::Windows::Globalization::ILanguage> language;
            hr = languages->CreateLanguage(languageTag.value, &language);
            if (FAILED(hr)) return hr;
            hr = factory->TryCreateFromLanguage(language, &engine);
            if (FAILED(hr)) return hr;
            if (!engine.Get()) {
                noLanguage = true;
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            }
        } else {
            hr = factory->TryCreateFromUserProfileLanguages(&engine);
        }
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

    HRESULT Recognize(Pixmap* px, HANDLE cancel, str::Builder& text, Vec<PdfOcrWord>& words) {
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
            AutoReleaseComPtr<ABI::Windows::Foundation::Collections::IVectorView<ocr::OcrWord*>> lineWords;
            hr = line->get_Words(&lineWords);
            if (FAILED(hr)) return hr;
            UINT32 wordCount = 0;
            hr = lineWords->get_Size(&wordCount);
            if (FAILED(hr)) return hr;
            for (UINT32 j = 0; j < wordCount; j++) {
                AutoReleaseComPtr<ocr::IOcrWord> word;
                hr = lineWords->GetAt(j, &word);
                if (FAILED(hr)) return hr;
                ABI::Windows::Foundation::Rect box;
                hr = word->get_BoundingRect(&box);
                if (FAILED(hr)) return hr;
                OcrString wordText;
                hr = word->get_Text(&wordText.value);
                if (FAILED(hr)) return hr;
                const WCHAR* chars = WindowsGetStringRawBuffer(wordText.value, &n);
                PdfOcrWord item{str::Dup(ToUtf8Temp(WStr(chars, (int)n))), {box.X, box.Y, box.Width, box.Height}};
                VecAppend(words, item);
            }
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
    Vec<int> pages;
    Str language;
    Str destination;
    EngineBase* output = nullptr;
    bool saveOnly = false;
    HRESULT error = S_OK;
    bool noLanguage = false;
    str::Builder text;

    ~OcrJob() {
        if (source) source->Release();
        if (output) output->Release();
        str::Free(language);
        str::Free(destination);
        if (cancel) CloseHandle(cancel);
    }
    void Release() {
        if (!InterlockedDecrement(&refs)) delete this;
    }
};

static void RunOcr(OcrJob* job) {
    if (job->saveOnly) {
        // Write beside the destination, then publish atomically. Cancellation or
        // a failed write never replaces the user's existing file.
        TempStr dir = path::GetDirTemp(job->destination);
        WCHAR temporary[MAX_PATH];
        bool made = GetTempFileNameW(ToWStrTemp(dir).s, L"ocr", 0, temporary) != 0;
        bool ok = made && EngineMupdfSaveCopy(job->output, ToUtf8Temp(temporary));
        bool cancelled = WaitForSingleObject(job->cancel, 0) == WAIT_OBJECT_0;
        if (ok && !cancelled) {
            ok = MoveFileExW(temporary, ToWStrTemp(job->destination).s,
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        }
        if (made) DeleteFileW(temporary);
        job->error = cancelled ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : ok ? S_OK : E_FAIL;
        InterlockedExchange(&job->done, 1);
        job->Release();
        return;
    }
    OcrReader reader;
    job->error = reader.Open(job->language);
    job->noLanguage = reader.noLanguage;
    EngineBase* engine = nullptr;
    if (SUCCEEDED(job->error)) {
        engine = job->source->Clone();
        if (!engine) job->error = E_FAIL;
    }
    for (int index = 0; SUCCEEDED(job->error) && index < len(job->pages); index++) {
        int page = job->pages[index];
        if (WaitForSingleObject(job->cancel, 0) == WAIT_OBJECT_0) {
            job->error = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            break;
        }
        InterlockedExchange(&job->page, index + 1);
        float zoom = OcrZoom(engine->PageMediabox(page), engine->fileDPI, reader.maxDimension);
        if (zoom <= 0) {
            job->error = E_INVALIDARG;
            break;
        }
        RenderPageArgs args(page, zoom, job->rotation, nullptr, RenderTarget::Export);
        Pixmap* px = PixmapToBgra(engine->RenderPage(args));
        str::Builder pageText;
        Vec<PdfOcrWord> words;
        job->error = reader.Recognize(px, job->cancel, pageText, words);
        FreePixmap(px);
        if (SUCCEEDED(job->error) && EngineMupdfIsPdf(engine) &&
            !EngineMupdfAddOcrText(engine, page, zoom, job->rotation, words))
            job->error = E_FAIL;
        for (auto& word : words) str::Free(word.text);
        if (FAILED(job->error)) break;
        if (job->first != job->last) job->text.Append(fmt("--- %s %d ---\r\n", StrL("Page"), page));
        job->text.Append(ToStr(pageText));
        job->text.Append(StrL("\r\n"));
    }
    job->output = engine;
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
    VirtButton* selected = nullptr;
    Edit* range = nullptr;
    DropDown* language = nullptr;
    StrVec languageTags;
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
    VecReset(job->pages);
    if (ev && ev->target == selected) {
        Vec<PageRange> ranges;
        bool valid = ParsePageRanges(range->GetTextTemp(), ranges) && len(ranges) > 0;
        for (auto& r : ranges) {
            int end = r.end == INT_MAX ? pageCount : r.end;
            if (r.start < 1 || end > pageCount || r.start > end) {
                valid = false;
                break;
            }
            for (int page = r.start; page <= end; page++) {
                if (VecFind(job->pages, page) < 0) VecAppend(job->pages, page);
            }
        }
        if (!valid || len(job->pages) == 0) {
            status->SetText(Tr("Enter valid page numbers, for example 1-3, 5."));
            return;
        }
    } else {
        for (int page = allPages ? 1 : current; page <= (allPages ? pageCount : current); page++)
            VecAppend(job->pages, page);
    }
    job->first = job->pages[0];
    job->last = job->pages[len(job->pages) - 1];
    int choice = CbGetCurrentSelection(language);
    Str tag = choice >= 0 && choice < len(languageTags) ? languageTags[choice] : Str();
    str::ReplacePtr(&job->language, str::Dup(tag));
    if (gSettings && !str::Eq(gSettings->ocrLanguage, tag)) {
        str::ReplacePtr(&gSettings->ocrLanguage, str::Dup(tag));
        ScheduleSaveSettings();
    }
    if (job->output) {
        job->output->Release();
        job->output = nullptr;
    }
    job->saveOnly = false;
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
    selected->SetIsEnabled(false);
    language->SetIsEnabled(false);
    range->SetIsEnabled(false);
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
        if (job->saveOnly)
            status->SetText(Tr("Saving searchable PDF..."));
        else if (page)
            status->SetText(fmt(Tr("OCR: page %d of %d").s, page, len(job->pages)));
        return;
    }
    KillTimer(hwnd, 1);
    running = false;
    start->SetIsEnabled(true);
    all->SetIsEnabled(pageCount > 1);
    selected->SetIsEnabled(true);
    language->SetIsEnabled(true);
    range->SetIsEnabled(true);
    cancel->SetText(Tr("Close"));
    cancel->SetIsEnabled(true);
    if (job->saveOnly) {
        status->SetText(job->error == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? Tr("Save cancelled. Destination unchanged.")
                        : FAILED(job->error) ? Tr("Couldn't save the searchable PDF. Destination unchanged.")
                                             : Tr("Searchable PDF saved. Reopen it to search or copy text."));
        save->SetIsEnabled(true);
        return;
    }
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
    save->SetIsEnabled(SUCCEEDED(job->error) && job->output && EngineMupdfIsPdf(job->source) && CanAccessDisk());
}

void OcrDialog::Copy(VirtMouseEvent*) {
    if (HasPermission(Perm::CopySelection) && !CopyTextToClipboard(ToStr(job->text))) {
        MessageBoxWarning(hwnd, Tr("Couldn't copy text to the clipboard."), Tr("Scan & OCR"));
    }
}

void OcrDialog::Save(VirtMouseEvent*) {
    if (!CanAccessDisk() || saving) return;
    WCHAR path[MAX_PATH] = L"Searchable.pdf";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.lpstrFilter = L"PDF files (*.pdf)\0*.pdf\0\0";
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    saving = true;
    bool picked = GetSaveFileNameW(&ofn);
    saving = false;
    if (closing || !IsWindow(hwnd)) {
        ScheduleDelete();
        return;
    }
    if (!picked) return;
    if (path::IsSame(ToUtf8Temp(path), job->source->FilePath())) {
        MessageBoxWarning(hwnd, Tr("Choose a new file name for the searchable PDF."), Tr("Scan & OCR"));
        return;
    }
    str::ReplacePtr(&job->destination, str::Dup(ToUtf8Temp(path)));
    job->saveOnly = true;
    ResetEvent(job->cancel);
    InterlockedExchange(&job->done, 0);
    running = true;
    start->SetIsEnabled(false);
    all->SetIsEnabled(false);
    selected->SetIsEnabled(false);
    save->SetIsEnabled(false);
    cancel->SetText(Tr("Cancel"));
    status->SetText(Tr("Saving searchable PDF..."));
    InterlockedIncrement(&job->refs);
    ThreadHandle thread = StartThread(MkFunc0(RunOcr, job), StrL("Save OCR"));
    if (!thread) {
        job->Release();
        job->error = E_FAIL;
        InterlockedExchange(&job->done, 1);
    }
    SafeCloseThreadHandle(&thread);
    SetTimer(hwnd, 1, 100, nullptr);
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
        {.s = Tr("Save a searchable PDF copy after OCR. Review recognized text for mistakes."), .font = font}));
    box->AddChild(NewVirtText({.s = Tr("OCR language"), .font = font}));
    language = new DropDown();
    language->Create({.parent = hwnd, .font = font});
    StrVec names;
    names.Append(Tr("Automatic (Windows profile)"));
    languageTags.Append(StrL(""));
    HRESULT init = RoInitialize(RO_INIT_SINGLETHREADED);
    {
        AutoReleaseComPtr<ocr::IOcrEngineStatics> factory;
        if (SUCCEEDED(OcrFactory(RuntimeClass_Windows_Media_Ocr_OcrEngine, factory))) {
            AutoReleaseComPtr<
                ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::Globalization::Language*>>
                available;
            UINT32 count = 0;
            if (SUCCEEDED(factory->get_AvailableRecognizerLanguages(&available)) &&
                SUCCEEDED(available->get_Size(&count))) {
                for (UINT32 i = 0; i < count; i++) {
                    AutoReleaseComPtr<ABI::Windows::Globalization::ILanguage> item;
                    OcrString tag, name;
                    if (FAILED(available->GetAt(i, &item)) || FAILED(item->get_LanguageTag(&tag.value)) ||
                        FAILED(item->get_DisplayName(&name.value)))
                        continue;
                    languageTags.Append(ToUtf8Temp(WindowsGetStringRawBuffer(tag.value, nullptr)));
                    names.Append(ToUtf8Temp(WindowsGetStringRawBuffer(name.value, nullptr)));
                }
            }
        }
    }
    if (SUCCEEDED(init)) RoUninitialize();
    language->SetItems(names);
    int choice = 0;
    for (int i = 1; i < len(languageTags); i++) {
        if (gSettings && str::EqI(languageTags[i], gSettings->ocrLanguage)) choice = i;
    }
    CbSetCurrentSelection(language, choice);
    box->AddChild(language);
    box->AddChild(NewVirtText({.s = Tr("Selected pages (for example 1-3, 5)"), .font = font}));
    range = new Edit();
    Edit::CreateArgs rangeArgs;
    rangeArgs.parent = hwnd;
    rangeArgs.font = font;
    rangeArgs.withBorder = true;
    range->Create(rangeArgs);
    range->SetText(fmt("%d", current));
    box->AddChild(range);
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
    selected = NewThemedButton(hwnd, Tr("Selected pages"), font, false);
    selected->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Start>(this);
    buttons->AddChild(selected);
    copy = NewThemedButton(hwnd, Tr("Copy text"), font, false);
    copy->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Copy>(this);
    copy->SetIsEnabled(false);
    buttons->AddChild(copy);
    save = NewThemedButton(hwnd, Tr("Save PDF..."), font, false);
    save->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Save>(this);
    save->SetIsEnabled(false);
    buttons->AddChild(save);
    cancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
    cancel->onClick = MkMethod1<OcrDialog, VirtMouseEvent*, &OcrDialog::Cancel>(this);
    buttons->AddChild(cancel);
    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(880), DpiScale(700)};
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
// A real xref table keeps this regression independent of PDF repair heuristics.
static Str OcrFixture(int rotation) {
    StrVec objects;
    objects.Append(StrL("<< /Type /Catalog /Pages 2 0 R >>"));
    objects.Append(StrL("<< /Type /Pages /Count 2 /Kids [3 0 R 5 0 R] /Resources << >> >>"));
    objects.Append(
        fmt("<< /Type /Page /Parent 2 0 R /MediaBox [10 20 410 620] "
            "/CropBox [30 40 390 600] /UserUnit 2 /Rotate %d /Contents 4 0 R >>",
            rotation));
    Str content = StrL("0.7 g 40 50 80 90 re f 2 0 0 2 7 9 cm");
    objects.Append(fmt("<< /Length %d >>\nstream\n%s\nendstream", len(content), content));
    objects.Append(StrL("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 600] >>"));
    str::Builder pdf;
    pdf.Append(StrL("%PDF-1.7\n"));
    Vec<int> offsets;
    for (int i = 0; i < len(objects); i++) {
        VecAppend(offsets, len(ToStr(pdf)));
        pdf.Append(fmt("%d 0 obj\n%s\nendobj\n", i + 1, objects[i]));
    }
    int xref = len(ToStr(pdf));
    pdf.Append(fmt("xref\n0 %d\n0000000000 65535 f \n", len(objects) + 1));
    for (int offset : offsets) pdf.Append(fmt("%010d 00000 n \n", offset));
    pdf.Append(fmt("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n", len(objects) + 1, xref));
    return str::Dup(ToStr(pdf));
}

static bool SameOcrPixels(Pixmap* a, Pixmap* b) {
    if (!a || !b || a->width != b->width || a->height != b->height) return false;
    for (int y = 0; y < a->height; y++) {
        for (int x = 0; x < a->width; x++) {
            if (memcmp(a->data + y * a->stride + x * 4, b->data + y * b->stride + x * 4, 3)) return false;
        }
    }
    return true;
}

static bool OcrRoundTrip(int pageRotation, int viewRotation) {
    Str fixture = OcrFixture(pageRotation);
    EngineBase* engine = CreateEngineMupdfFromData(fixture, StrL("ocr.pdf"), nullptr);
    if (!engine) {
        str::Free(fixture);
        return false;
    }
    constexpr float zoom = 1.75f;
    RenderPageArgs args(1, zoom, viewRotation, nullptr, RenderTarget::Export);
    Pixmap* before = PixmapToBgra(engine->RenderPage(args));
    Vec<PdfOcrWord> words;
    PdfOcrWord word{StrL("Searchable"), {80, 90, 220, 36}};
    VecAppend(words, word);
    VecAppend(words, PdfOcrWord{StrL("caf\xc3\xa9"), {80, 160, 100, 36}});
    Str dest = str::Dup(GetTempFilePathTemp(StrL("ocr")));
    bool ok = EngineMupdfAddOcrText(engine, 1, zoom, viewRotation, words) && EngineMupdfSaveCopy(engine, dest);
    engine->Release();
    engine = ok ? CreateEngineMupdfFromFile(dest, FileType::PDF, 96) : nullptr;
    ok = ok && engine;
    if (engine) {
        // Search uses the same extraction and selection machinery as the viewer.
        TextSearch search(engine);
        Vec<TextSel>* matches = search.FindFirst(1, word.text);
        ok = matches && len(*matches) > 0 && engine->PageCount() == 2;
        if (ok) {
            Rect r = (*matches)[0].rect;
            RectF screen =
                engine->Transform(RectF((float)r.x, (float)r.y, (float)r.dx, (float)r.dy), 1, zoom, viewRotation);
            float cx = screen.x + screen.dx / 2;
            float cy = screen.y + screen.dy / 2;
            ok = cx >= word.box.x && cx <= word.box.x + word.box.dx && cy >= word.box.y - 10 &&
                 cy <= word.box.y + word.box.dy + 10;
        }
        ok = ok && search.FindFirst(1, StrL("caf\xc3\xa9"));
        PageText untouched = engine->ExtractPageText(2);
        ok = ok && str::IsEmptyOrWhiteSpace(untouched.text);
        FreePageText(&untouched);
        Pixmap* after = PixmapToBgra(engine->RenderPage(args));
        ok = SameOcrPixels(before, after) && ok;
        FreePixmap(after);
    }
    if (engine) engine->Release();
    FreePixmap(before);
    file::Delete(dest);
    str::Free(dest);
    str::Free(fixture);
    printf("OCR save/reopen/search: page rotation %d, view rotation %d: %s\n", pageRotation, viewRotation,
           ok ? "PASS" : "FAIL");
    return ok;
}

static bool OcrCancelSaveTest() {
    Str fixture = OcrFixture(0);
    auto* job = new OcrJob();
    job->output = CreateEngineMupdfFromData(fixture, StrL("ocr.pdf"), nullptr);
    job->destination = str::Dup(GetTempFilePathTemp(StrL("ocr")));
    Str sentinel = StrL("Keep existing destination");
    bool ok = job->cancel && job->output && file::WriteFile(job->destination, sentinel);
    if (ok) {
        job->saveOnly = true;
        SetEvent(job->cancel);
        InterlockedIncrement(&job->refs);
        RunOcr(job);
        Str saved = file::ReadFile(job->destination);
        ok = job->error == HRESULT_FROM_WIN32(ERROR_CANCELLED) && str::Eq(saved, sentinel);
        str::Free(saved);
    }
    file::Delete(job->destination);
    job->Release();
    str::Free(fixture);
    printf("OCR cancelled save preserves destination: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

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
    Vec<PdfOcrWord> words;
    hr = reader.Recognize(px, cancel, text, words);
    *ok = SUCCEEDED(hr) && str::Contains(ToStr(text), StrL("12345"));
    // Start with a scanned (image-only) PDF, then save/reopen the actual OCR boxes.
    Str source = str::Dup(GetTempFilePathTemp(StrL("ocr")));
    Str dest = str::Dup(GetTempFilePathTemp(StrL("ocr")));
    {
        PdfCreator creator;
        Gdiplus::Bitmap* bitmap = NewGdiplusBitmapFromPixmap(px);
        *ok = *ok && bitmap && creator.AddPageFromGdiplusBitmap(bitmap, 72) && creator.SaveToFile(source);
        delete bitmap;
    }
    EngineBase* pdf = *ok ? CreateEngineMupdfFromFile(source, FileType::PDF, 96) : nullptr;
    if (pdf) {
        *ok = EngineMupdfAddOcrText(pdf, 1, 1, 0, words) && EngineMupdfSaveCopy(pdf, dest);
        pdf->Release();
    } else
        *ok = false;
    pdf = *ok ? CreateEngineMupdfFromFile(dest, FileType::PDF, 96) : nullptr;
    if (pdf) {
        {
            TextSearch search(pdf);
            *ok = search.FindFirst(1, StrL("12345")) != nullptr;
        }
        pdf->Release();
    } else
        *ok = false;
    file::Delete(source);
    file::Delete(dest);
    str::Free(source);
    str::Free(dest);
    for (auto& word : words) str::Free(word.text);
    FreePixmap(px);
    printf("OCR smoke: HRESULT 0x%x, text: %.*s\n", (uint)hr, len(ToStr(text)), ToStr(text).s);
}

bool Ocr_UnitTests() {
    if (OcrZoom({0, 0, 0, 0}, 72, 4096) != 0) return false;
    float zoom = OcrZoom({0, 0, 72000, 36000}, 72, 4096);
    if (zoom <= 0 || ceilf(zoom * 72000) > 4096) return false;
    if (OcrZoom({0, 0, 612, 792}, 72, 4096) != 300.f / 72) return false;
    for (int pageRotation : {0, 90, 180, 270}) {
        for (int viewRotation : {0, 90, 180, 270}) {
            if (!OcrRoundTrip(pageRotation, viewRotation)) return false;
        }
    }
    if (!OcrCancelSaveTest()) return false;
    bool ok = false;
    ThreadHandle thread = StartThread(MkFunc0(OcrSmokeTest, &ok), StrL("OCR smoke"));
    if (!thread) return false;
    JoinThread(&thread, INFINITE);
    return ok;
}
#endif
