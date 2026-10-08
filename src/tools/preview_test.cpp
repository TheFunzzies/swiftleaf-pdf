
#include "base/Base.h"

#include <objbase.h>
#include <thumbcache.h>

#pragma comment(lib, "gdiplus.lib")

constexpr const WCHAR* kPdfPreviewClsid = L"{33197FE0-BDA9-4128-86DD-9E8912BABEAE}";
constexpr const WCHAR* kXpsPreviewClsid = L"{17C0EC9D-887F-421D-A914-222F6D5D503B}";
constexpr const WCHAR* kDjVuPreviewClsid = L"{D01B1968-6E13-49F6-9F68-26DFDE7CA787}";
constexpr const WCHAR* kEpubPreviewClsid = L"{957DB259-ABBF-4817-9819-6F4FB0FBCA89}";
constexpr const WCHAR* kFb2PreviewClsid = L"{B6074459-9245-4773-9A3C-05395ACD3E16}";
constexpr const WCHAR* kMobiPreviewClsid = L"{E54B04C8-1D40-4939-BDC6-BB4B4835D7F3}";
constexpr const WCHAR* kCbxPreviewClsid = L"{A246FEA1-F17D-46ED-BAD6-9D8A2C2FFB05}";
constexpr const WCHAR* kTgaPreviewClsid = L"{711FD03E-003A-43A1-8F0E-C961D72D4E60}";

// Our GUID here:
LPCOLESTR myGuid = kPdfPreviewClsid;

typedef HRESULT ourDllGetClassObjectT(REFCLSID rclsid, REFIID riid, void** ppv);

void log(Str s) {
    if (len(s) == 0) {
        return;
    }
    OutputDebugStringA(s.s);
    fwrite(s.s, 1, (size_t)s.len, stdout);
}
void _uploadDebugReport(Str, Str, bool) {}

static Str kPdfPreviewDllName = StrL("PdfPreview.dll");

int main(int c, char** v) {
    GUID clsid{};
    IIDFromString(myGuid, &clsid);

    if (c < 2) {
        printf("not enough arguments: file name\n");
        return 1;
    }
    HRESULT r;
    IStream* pStream = NULL;
    HMODULE dll = NULL;

    dll = LoadLibraryA(kPdfPreviewDllName.s);
    if (!dll) {
        printf("can't open DLL\n");
        return 1;
    }

    ourDllGetClassObjectT* ourDllGetClassObject = (ourDllGetClassObjectT*)GetProcAddress(dll, "DllGetClassObject");

    IClassFactory* pFactory = NULL;
    r = ourDllGetClassObject(clsid, IID_IClassFactory, (void**)&pFactory);
    if (r != S_OK) {
        printf("failed: get factory: %08x\n", r);
        return 2;
    }

    IInitializeWithStream* pInit;
    r = pFactory->CreateInstance(NULL, IID_IInitializeWithStream, (void**)&pInit);
    if (r != S_OK) {
        printf("failed: get object\n");
        return 3;
    }
    pFactory->Release();

    IThumbnailProvider* pProvider;
    r = pInit->QueryInterface(IID_IThumbnailProvider, (void**)&pProvider);
    if (r != S_OK) {
        printf("failed: get provider\n");
        return 5;
    }

    wchar_t wfile[256]{};
    MultiByteToWideChar(CP_ACP, 0, v[1], -1, wfile, 256);
    r = SHCreateStreamOnFileEx(wfile, STGM_READ, 0, FALSE, NULL, &pStream);
    if (r != S_OK || !pStream) {
        printf("can't open file\n");
        return 10;
    }

    r = pInit->Initialize(pStream, 0);
    pInit->Release();
    pStream->Release();
    if (r != S_OK) {
        printf("pInit->Initialize() failed\n");
        return 11;
    }

    HBITMAP bmp;
    WTS_ALPHATYPE alpha;
    r = pProvider->GetThumbnail(256, &bmp, &alpha);
    pProvider->Release();
    if (r != S_OK) {
        printf("pProvider->GetThumbnail() failed\n");
        return 12;
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    Gdiplus::Bitmap* gdipBmp = Gdiplus::Bitmap::FromHBITMAP(bmp, NULL);
    CLSID pngClsid;
    CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
    Gdiplus::Status st = gdipBmp->Save(L"preview.png", &pngClsid);
    if (st != Gdiplus::Ok) {
        printf("failed: save png: %d\n", (int)st);
    } else {
        printf("saved preview.png\n");
    }
    delete gdipBmp;
    DeleteObject(bmp);
    Gdiplus::GdiplusShutdown(gdiplusToken);
}
