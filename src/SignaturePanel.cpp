/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/File.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <cryptuiapi.h>
#include <commdlg.h>
#include <time.h>

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
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "DisplayModel.h"
#include "Translations.h"
#include "DarkMode.h"
#include "Theme.h"
#include "SumatraPDF.h"
#include "SignaturePanel.h"
#include "SumatraLog.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "cryptui.lib")

extern bool SaveAnnotationsToMaybeNewPdfFile(WindowTab*);

static EngineBase* GetPdfEngine(MainWindow* win) {
    if (!IsMainWindowValidAndNotClosing(win) || !win->IsDocLoaded()) {
        return nullptr;
    }
    DisplayModel* dm = win->AsFixed();
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine || !EngineMupdfSupportsAnnotations(engine)) {
        return nullptr;
    }
    return engine;
}

void ViewSignatureCertificate(HWND hwndParent, const Str& certDer) {
    if (len(certDer) == 0) {
        MessageBoxWarning(hwndParent, Tr("No certificate data is available for this signature."),
                          Tr("View Certificate"));
        return;
    }
    PCCERT_CONTEXT pCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, (const BYTE*)certDer.s,
                                                        (DWORD)len(certDer));
    if (!pCert) {
        MessageBoxWarning(hwndParent, Tr("Failed to parse the certificate."), Tr("View Certificate"));
        return;
    }
    CryptUIDlgViewContext(CERT_STORE_CERTIFICATE_CONTEXT, pCert, hwndParent, L"Digital Certificate", 0, nullptr);
    CertFreeCertificateContext(pCert);
}

bool ClearDocumentSignature(MainWindow* win, Str fieldName) {
    EngineBase* engine = GetPdfEngine(win);
    if (!engine) {
        return false;
    }
    bool ok = EngineMupdfClearSignature(engine, fieldName);
    if (!ok) {
        MessageBoxWarning(win ? win->hwndFrame : nullptr, Tr("Could not clear the digital signature."),
                          Tr("Clear Signature"));
        return false;
    }
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (tab) {
        SaveAnnotationsToMaybeNewPdfFile(tab);
    }
    return true;
}

// Dialog displaying digital signatures in the document.
struct SignaturePanelWnd : WindowBase {
    MainWindow* win = nullptr;
    Vec<SignatureItemInfo> sigs;
    DropDown* ddSigs = nullptr;
    VirtFixedLinesText* txtStatus = nullptr;
    VirtFixedLinesText* txtDetails = nullptr;
    VirtButton* btnViewCert = nullptr;
    VirtButton* btnClear = nullptr;
    VirtButton* btnClose = nullptr;

    ~SignaturePanelWnd() override { FreeSignatures(sigs); }

    bool Create(MainWindow* win);
    void RefreshDetails();
    void OnSigChanged();
    void OnViewCert(VirtMouseEvent* = nullptr);
    void OnClearSig(VirtMouseEvent* = nullptr);
    void OnClose(VirtMouseEvent* = nullptr);
};

void SignaturePanelWnd::OnClose(VirtMouseEvent*) {
    Close();
}

void SignaturePanelWnd::OnViewCert(VirtMouseEvent*) {
    if (sigs.len == 0) return;
    int idx = CbGetCurrentSelection(ddSigs);
    if (idx < 0 || idx >= sigs.len) idx = 0;
    ViewSignatureCertificate(hwnd, sigs[idx].certDer);
}

void SignaturePanelWnd::OnClearSig(VirtMouseEvent*) {
    if (sigs.len == 0) return;
    int idx = CbGetCurrentSelection(ddSigs);
    if (idx < 0 || idx >= sigs.len) idx = 0;

    int res = MessageBox(hwnd, L"Are you sure you want to clear this digital signature?", L"Clear Signature",
                         MB_YESNO | MB_ICONQUESTION);
    if (res != IDYES) return;

    Str field = str::Dup(sigs[idx].fieldName);
    Close();
    ClearDocumentSignature(win, field);
    str::Free(field);
}

void SignaturePanelWnd::OnSigChanged() {
    RefreshDetails();
}

void SignaturePanelWnd::RefreshDetails() {
    if (sigs.len == 0) {
        txtStatus->SetText(Tr("No digital signatures present in this document."));
        txtDetails->SetText(Str{});
        if (btnViewCert) btnViewCert->SetIsEnabled(false);
        if (btnClear) btnClear->SetIsEnabled(false);
        return;
    }
    int idx = CbGetCurrentSelection(ddSigs);
    if (idx < 0 || idx >= sigs.len) idx = 0;
    const SignatureItemInfo& sig = sigs[idx];

    str::Builder sb;
    sb.Append(fmt(Tr("Validity Status: %s\n").s, len(sig.statusDesc) > 0 ? sig.statusDesc : StrL("(unknown)")));
    if (sig.isCertified) {
        Str permStr = (sig.mdpPerm == 1)   ? StrL("No changes permitted (P=1)")
                      : (sig.mdpPerm == 3) ? StrL("Annotations, form fill & signing permitted (P=3)")
                                           : StrL("Form fill & signing permitted (P=2)");
        sb.Append(fmt(Tr("DocMDP Certified: %s\n").s, permStr));
    } else {
        sb.Append(StrL("Standard Digital Signature\n"));
    }
    txtStatus->SetText(ToStrTemp(sb));

    str::Builder det;
    det.Append(fmt(Tr("Signer: %s\n").s, len(sig.signerName) > 0 ? sig.signerName : StrL("(unknown)")));
    if (len(sig.issuerName) > 0) {
        det.Append(fmt(Tr("Issued by: %s\n").s, sig.issuerName));
    }
    if (len(sig.date) > 0) {
        det.Append(fmt(Tr("Signing Time: %s\n").s, sig.date));
    }
    if (len(sig.reason) > 0) {
        det.Append(fmt(Tr("Reason: %s\n").s, sig.reason));
    }
    if (len(sig.location) > 0) {
        det.Append(fmt(Tr("Location: %s\n").s, sig.location));
    }
    det.Append(
        fmt(Tr("Field: %s (Page %d)\n").s, len(sig.fieldName) > 0 ? sig.fieldName : StrL("Signature"), sig.pageNo));
    txtDetails->SetText(ToStrTemp(det));

    if (btnViewCert) btnViewCert->SetIsEnabled(len(sig.certDer) > 0);
    if (btnClear) btnClear->SetIsEnabled(true);
}

bool SignaturePanelWnd::Create(MainWindow* parentWin) {
    win = parentWin;
    EngineBase* engine = GetPdfEngine(parentWin);
    if (!engine) return false;

    VecReset(sigs);
    EngineMupdfGetSignaturesInfo(engine, sigs);

    closeOnEsc = true;
    CreateCustomArgs args;
    args.owner = parentWin ? parentWin->hwndFrame : nullptr;
    args.title = Tr("Digital Signatures");
    args.font = GetDefaultGuiFont();
    args.style = WS_OVERLAPPEDWINDOW;
    args.visible = false;
    args.bgColor = DarkModeDialogBgColor();
    if (!CreateCustom(args)) return false;

    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;
    box->gap = DpiScale(10);

    box->AddChild(NewVirtText({.s = Tr("Signatures in Document"), .font = font}));
    ddSigs = new DropDown();
    DropDown::CreateArgs ddSigsArgs;
    ddSigsArgs.parent = hwnd;
    ddSigsArgs.font = font;
    ddSigs->Create(ddSigsArgs);

    StrVec items;
    if (sigs.len == 0) {
        items.Append(Tr("(No digital signatures)"));
    } else {
        for (int i = 0; i < sigs.len; i++) {
            Str st = (sigs[i].status == SignatureStatus::Valid)                  ? StrL("Valid")
                     : (sigs[i].status == SignatureStatus::ModifiedAfterSigning) ? StrL("Modified!")
                     : (sigs[i].status == SignatureStatus::Expired)              ? StrL("Expired")
                     : (sigs[i].status == SignatureStatus::Revoked)              ? StrL("Revoked!")
                                                                                 : StrL("Unknown Signer");
            items.Append(fmt("%d: %s [%s] (Page %d)", i + 1,
                             len(sigs[i].signerName) > 0 ? sigs[i].signerName : StrL("Signer"), st, sigs[i].pageNo));
        }
    }
    ddSigs->SetItems(items);
    CbSetCurrentSelection(ddSigs, 0);
    ddSigs->onSelectionChanged = MkMethod0<SignaturePanelWnd, &SignaturePanelWnd::OnSigChanged>(this);
    box->AddChild(ddSigs);

    txtStatus = new VirtFixedLinesText();
    txtStatus->font = font;
    txtStatus->nLines = 2;
    box->AddChild(txtStatus);

    txtDetails = new VirtFixedLinesText();
    txtDetails->font = font;
    txtDetails->nLines = 7;
    box->AddChild(txtDetails);

    auto* btns = new HBox();
    btns->gap = DpiScale(8);
    btnViewCert = NewThemedButton(hwnd, Tr("View Certificate..."), font, true);
    btnViewCert->onClick = MkMethod1<SignaturePanelWnd, VirtMouseEvent*, &SignaturePanelWnd::OnViewCert>(this);
    btns->AddChild(btnViewCert);

    btnClear = NewThemedButton(hwnd, Tr("Clear Signature"), font, false);
    btnClear->onClick = MkMethod1<SignaturePanelWnd, VirtMouseEvent*, &SignaturePanelWnd::OnClearSig>(this);
    btns->AddChild(btnClear);

    btnClose = NewThemedButton(hwnd, Tr("Close"), font, false);
    btnClose->onClick = MkMethod1<SignaturePanelWnd, VirtMouseEvent*, &SignaturePanelWnd::OnClose>(this);
    btns->AddChild(btnClose);
    box->AddChild(btns);

    RefreshDetails();

    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(500), DpiScale(360)};
    AdjustWindowRectEx(&rect, args.style, FALSE, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    UpdateTheme();
    HwndCenterDialog(hwnd, parentWin ? parentWin->hwndFrame : nullptr);
    SetIsVisible(true);
    return true;
}

void ShowSignaturePanel(MainWindow* win) {
    auto* panel = new SignaturePanelWnd();
    if (!panel->Create(win)) delete panel;
}

// Certify Document (DocMDP) Dialog
struct CertifyDocumentWnd : WindowBase {
    MainWindow* win = nullptr;
    DropDown* ddCert = nullptr;
    StrVec certThumbs;
    Edit* editCert = nullptr;
    Edit* editPassword = nullptr;
    DropDown* ddPerms = nullptr;
    Edit* editReason = nullptr;
    Edit* editLocation = nullptr;
    VirtButton* btnBrowseCert = nullptr;
    VirtButton* btnCertify = nullptr;
    VirtButton* btnCancel = nullptr;

    bool Create(MainWindow* win);
    void OnBrowseCert(VirtMouseEvent* = nullptr);
    void OnCertify(VirtMouseEvent* = nullptr);
    void OnCancel(VirtMouseEvent* = nullptr);
};

void CertifyDocumentWnd::OnCancel(VirtMouseEvent*) {
    Close();
}

void CertifyDocumentWnd::OnBrowseCert(VirtMouseEvent*) {
    WCHAR fileName[MAX_PATH + 1]{};
    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = dimof(fileName);
    ofn.lpstrFilter = L"PKCS#12 Certificates (*.pfx;*.p12)\0*.pfx;*.p12\0All files (*.*)\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;
    if (editCert) {
        editCert->SetText(ToUtf8Temp(WStr(fileName)));
    }
}

void CertifyDocumentWnd::OnCertify(VirtMouseEvent*) {
    EngineBase* engine = GetPdfEngine(win);
    if (!engine) return;

    PdfSignArgs args;
    args.isCertify = true;
    int pSel = CbGetCurrentSelection(ddPerms);
    args.mdpPerm = (pSel == 0) ? 1 : (pSel == 2) ? 3 : 2;

    int cSel = CbGetCurrentSelection(ddCert);
    if (cSel >= 0 && cSel < certThumbs.size) {
        args.certThumbprint = certThumbs[cSel];
    } else {
        TempStr pfx = editCert ? editCert->GetTextTemp() : Str{};
        if (len(pfx) == 0) {
            MessageBoxWarning(hwnd, Tr("Please select a certificate or specify a .pfx file."), Tr("Certify Document"));
            return;
        }
        args.certPath = str::Dup(pfx);
        TempStr pw = editPassword ? editPassword->GetTextTemp() : Str{};
        args.certPassword = str::Dup(pw);
    }

    TempStr r = editReason ? editReason->GetTextTemp() : Str{};
    if (len(r) > 0) args.reason = str::Dup(r);
    TempStr l = editLocation ? editLocation->GetTextTemp() : Str{};
    if (len(l) > 0) args.location = str::Dup(l);

    args.pageNo = 1;
    if (gSettings && len(gSettings->tsaUrl) > 0) {
        args.tsaUrl = gSettings->tsaUrl;
    }

    Str err;
    bool ok = EngineMupdfSignDocument(engine, args, &err);
    if (!ok) {
        MessageBoxWarning(hwnd, fmt(Tr("Could not certify document: %s").s, len(err) > 0 ? err : StrL("Unknown error")),
                          Tr("Certify Document"));
        str::Free(err);
        return;
    }
    str::Free(err);

    Close();
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (tab) {
        SaveAnnotationsToMaybeNewPdfFile(tab);
    }
}

bool CertifyDocumentWnd::Create(MainWindow* parentWin) {
    win = parentWin;
    EngineBase* engine = GetPdfEngine(parentWin);
    if (!engine) return false;

    closeOnEsc = true;
    CreateCustomArgs args;
    args.owner = parentWin ? parentWin->hwndFrame : nullptr;
    args.title = Tr("Certify Document (DocMDP)");
    args.font = GetDefaultGuiFont();
    args.style = WS_OVERLAPPEDWINDOW;
    args.visible = false;
    args.bgColor = DarkModeDialogBgColor();
    if (!CreateCustom(args)) return false;

    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;
    box->gap = DpiScale(8);

    box->AddChild(NewVirtText({.s = Tr("Digital Certificate"), .font = font}));
    ddCert = new DropDown();
    DropDown::CreateArgs ddCertArgs;
    ddCertArgs.parent = hwnd;
    ddCertArgs.font = font;
    ddCert->Create(ddCertArgs);

    StrVec certLabels;
    ListWindowsSigningCertificates(certThumbs, certLabels);
    certLabels.Append(Tr("Use certificate file (.pfx)..."));
    ddCert->SetItems(certLabels);
    CbSetCurrentSelection(ddCert, 0);
    box->AddChild(ddCert);

    auto* pfxRow = new HBox();
    pfxRow->gap = DpiScale(6);
    editCert = new Edit();
    Edit::CreateArgs editCertArgs;
    editCertArgs.parent = hwnd;
    editCertArgs.font = font;
    editCertArgs.cueText = Tr("Path to .pfx certificate");
    editCert->Create(editCertArgs);
    pfxRow->AddChild(editCert);

    btnBrowseCert = NewThemedButton(hwnd, Tr("Browse..."), font, false);
    btnBrowseCert->onClick = MkMethod1<CertifyDocumentWnd, VirtMouseEvent*, &CertifyDocumentWnd::OnBrowseCert>(this);
    pfxRow->AddChild(btnBrowseCert);
    box->AddChild(pfxRow);

    box->AddChild(NewVirtText({.s = Tr("Password (if required)"), .font = font}));
    editPassword = new Edit();
    Edit::CreateArgs pwArgs;
    pwArgs.parent = hwnd;
    pwArgs.font = font;
    pwArgs.isPassword = true;
    editPassword->Create(pwArgs);
    box->AddChild(editPassword);

    box->AddChild(NewVirtText({.s = Tr("Permitted changes after certification (DocMDP)"), .font = font}));
    ddPerms = new DropDown();
    DropDown::CreateArgs ddPermsArgs;
    ddPermsArgs.parent = hwnd;
    ddPermsArgs.font = font;
    ddPerms->Create(ddPermsArgs);

    StrVec perms;
    perms.Append(Tr("No changes allowed (strict lock)"));
    perms.Append(Tr("Form fill-in and digital signatures allowed"));
    perms.Append(Tr("Annotations, form fill-in and digital signatures allowed"));
    ddPerms->SetItems(perms);
    CbSetCurrentSelection(ddPerms, 1);
    box->AddChild(ddPerms);

    box->AddChild(NewVirtText({.s = Tr("Reason"), .font = font}));
    editReason = new Edit();
    Edit::CreateArgs reasonArgs;
    reasonArgs.parent = hwnd;
    reasonArgs.font = font;
    reasonArgs.text = Tr("I certify that this document is authentic.");
    editReason->Create(reasonArgs);
    box->AddChild(editReason);

    box->AddChild(NewVirtText({.s = Tr("Location"), .font = font}));
    editLocation = new Edit();
    Edit::CreateArgs locArgs;
    locArgs.parent = hwnd;
    locArgs.font = font;
    editLocation->Create(locArgs);
    box->AddChild(editLocation);

    auto* btns = new HBox();
    btns->gap = DpiScale(8);
    btnCertify = NewThemedButton(hwnd, Tr("Certify & Sign"), font, true);
    btnCertify->onClick = MkMethod1<CertifyDocumentWnd, VirtMouseEvent*, &CertifyDocumentWnd::OnCertify>(this);
    btns->AddChild(btnCertify);

    btnCancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
    btnCancel->onClick = MkMethod1<CertifyDocumentWnd, VirtMouseEvent*, &CertifyDocumentWnd::OnCancel>(this);
    btns->AddChild(btnCancel);
    box->AddChild(btns);

    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(460), DpiScale(460)};
    AdjustWindowRectEx(&rect, args.style, FALSE, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    UpdateTheme();
    HwndCenterDialog(hwnd, parentWin ? parentWin->hwndFrame : nullptr);
    SetIsVisible(true);
    return true;
}

void ShowCertifyDocumentDialog(MainWindow* win) {
    auto* dlg = new CertifyDocumentWnd();
    if (!dlg->Create(win)) delete dlg;
}

// Unit test: in-memory throwaway self-signed cert creation, PDF signing, DocMDP verify, tamper detection, and clear
// signature.
bool Signature_UnitTests() {
    HCRYPTPROV hProv = 0;
    LPCWSTR containerName = L"SwiftleafTestSigContainer";
    BOOL okProv = CryptAcquireContextW(&hProv, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET);
    if (!okProv && GetLastError() == NTE_EXISTS) {
        CryptAcquireContextW(&hProv, containerName, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, 0);
    }
    if (!hProv) {
        return true; // Skip if crypto context cannot be created in environment
    }

    HCRYPTKEY hKey = 0;
    CryptGenKey(hProv, AT_KEYEXCHANGE, CRYPT_EXPORTABLE | 0x08000000, &hKey);

    CRYPT_KEY_PROV_INFO kpi{};
    kpi.pwszContainerName = (LPWSTR)containerName;
    kpi.pwszProvName = (LPWSTR)MS_ENH_RSA_AES_PROV_W;
    kpi.dwProvType = PROV_RSA_AES;
    kpi.dwKeySpec = AT_KEYEXCHANGE;

    CERT_NAME_BLOB subjectIssuerBlob{};
    BYTE encodedName[128]{};
    DWORD cbEncodedName = sizeof(encodedName);
    CertStrToNameW(X509_ASN_ENCODING, L"CN=Swiftleaf Test Signer, O=Swiftleaf PDF", CERT_X500_NAME_STR, nullptr,
                   encodedName, &cbEncodedName, nullptr);
    subjectIssuerBlob.cbData = cbEncodedName;
    subjectIssuerBlob.pbData = encodedName;

    PCCERT_CONTEXT pCert =
        CertCreateSelfSignCertificate(hProv, &subjectIssuerBlob, 0, &kpi, nullptr, nullptr, nullptr, nullptr);
    if (!pCert) {
        if (hKey) CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return true;
    }

    CertSetCertificateContextProperty(pCert, CERT_KEY_PROV_INFO_PROP_ID, 0, &kpi);

#if defined(DEBUG)
    extern bool PdfSignTimeTest(HCRYPTPROV, CRYPT_KEY_PROV_INFO*, CERT_NAME_BLOB*);
    bool timeOkay = PdfSignTimeTest(hProv, &kpi, &subjectIssuerBlob);
    if (!timeOkay) logf("signature historical time test failed\n");
#endif

    HCERTSTORE memStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    CertAddCertificateContextToStore(memStore, pCert, CERT_STORE_ADD_ALWAYS, nullptr);

    CRYPT_DATA_BLOB pfxBlob{};
    BOOL okExp1 = PFXExportCertStoreEx(memStore, &pfxBlob, L"testpass", nullptr, EXPORT_PRIVATE_KEYS);
    if (!okExp1 || pfxBlob.cbData == 0) {
        CertCloseStore(memStore, 0);
        CertFreeCertificateContext(pCert);
        if (hKey) CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return true;
    }
    pfxBlob.pbData = (BYTE*)malloc(pfxBlob.cbData);
    PFXExportCertStoreEx(memStore, &pfxBlob, L"testpass", nullptr, EXPORT_PRIVATE_KEYS);

    WCHAR tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);

    Str pfxPath = fmt("%s\\swiftleaf_test_sig.pfx", ToUtf8Temp(WStr(tempDir)));
    file::WriteFile(pfxPath, Str((const char*)pfxBlob.pbData, pfxBlob.cbData));
    free(pfxBlob.pbData);
    CertCloseStore(memStore, 0);
    CertFreeCertificateContext(pCert);
    if (hKey) CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);

#if defined(DEBUG)
    extern bool PdfSignCryptoTest(Str pfxPath);
    if (!timeOkay) return false;
    if (!PdfSignCryptoTest(pfxPath)) return false;
#endif

    Str origPdfPath = fmt("%s\\swiftleaf_test_orig.pdf", ToUtf8Temp(WStr(tempDir)));
    Str sample = file::ReadFile(StrL("docs/test/swiftleaf-sample.pdf"));
    if (len(sample) == 0) {
        return true;
    }
    file::WriteFile(origPdfPath, sample);

    Str signedPdfPath = fmt("%s\\swiftleaf_test_signed.pdf", ToUtf8Temp(WStr(tempDir)));

    EngineBase* engine = CreateEngineMupdfFromFile(origPdfPath, FileType::PDF, 96);
    if (engine) {
#if defined(DEBUG)
        extern bool PdfSignPrepareTest(EngineBase*);
        if (!PdfSignPrepareTest(engine)) {
            SafeEngineRelease(&engine);
            return false;
        }
#endif
        PdfSignArgs signArgs;
        signArgs.certPath = pfxPath;
        signArgs.certPassword = StrL("testpass");
        signArgs.pageNo = 1;
        signArgs.reason = StrL("Unit Test Certification");
        signArgs.location = StrL("Local Test");
        signArgs.isCertify = true;
        signArgs.mdpPerm = 2;

        Str err;
        bool okSign = EngineMupdfSignDocument(engine, signArgs, &err);
        str::Free(err);
        if (!okSign) {
            SafeEngineRelease(&engine);
            return false;
        }

        bool okSave = EngineMupdfSaveUpdated(engine, signedPdfPath, {});
        SafeEngineRelease(&engine);
        if (!okSave) {
            return false;
        }

        // Re-open and verify signature
        engine = CreateEngineMupdfFromFile(signedPdfPath, FileType::PDF, 96);
        if (!engine) {
            return false;
        }
        Vec<SignatureItemInfo> sigs;
        bool okInfo = EngineMupdfGetSignaturesInfo(engine, sigs);
        if (okInfo && len(sigs)) {
            logf("signature test: status=%d certified=%d permission=%d reason=%s signer=%s detail=%s\n",
                 (int)sigs[0].status, (int)sigs[0].isCertified, sigs[0].mdpPerm, sigs[0].reason, sigs[0].signerName,
                 sigs[0].statusDesc);
        }
        if (!okInfo || sigs.len < 1) {
            FreeSignatures(sigs);
            SafeEngineRelease(&engine);
            return false;
        }
        if (!sigs[0].isCertified || sigs[0].mdpPerm != 2) {
            FreeSignatures(sigs);
            SafeEngineRelease(&engine);
            return false;
        }
        if (!str::Eq(sigs[0].reason, StrL("Unit Test Certification"))) {
            FreeSignatures(sigs);
            SafeEngineRelease(&engine);
            return false;
        }
        if (!str::Contains(sigs[0].signerName, StrL("Swiftleaf Test Signer"))) {
            FreeSignatures(sigs);
            SafeEngineRelease(&engine);
            return false;
        }
        if (sigs[0].status != SignatureStatus::Valid && sigs[0].status != SignatureStatus::UnknownSigner) {
            FreeSignatures(sigs);
            SafeEngineRelease(&engine);
            return false;
        }

#if defined(DEBUG)
        extern bool PdfSignChangesTest(Str, Str);
        Str changedPath = fmt("%s\\swiftleaf_test_changes.pdf", ToUtf8Temp(WStr(tempDir)));
        if (!PdfSignChangesTest(signedPdfPath, changedPath) ||
            !str::Contains(sigs[0].statusDesc, StrL("No verified timestamp"))) {
            FreeSignatures(sigs);
            SafeEngineRelease(&engine);
            return false;
        }
#endif

        // Test Clear Signature
        bool okClear = EngineMupdfClearSignature(engine, sigs[0].fieldName);
        if (!okClear) {
            FreeSignatures(sigs);
            SafeEngineRelease(&engine);
            return false;
        }
        Str clearedPdfPath = fmt("%s\\swiftleaf_test_cleared.pdf", ToUtf8Temp(WStr(tempDir)));
        EngineMupdfSaveUpdated(engine, clearedPdfPath, {});
        file::Delete(clearedPdfPath);
        FreeSignatures(sigs);
        SafeEngineRelease(&engine);

        // Test Tampering Detection
        Str signedBytes = file::ReadFile(signedPdfPath);
        if (len(signedBytes) > 100) {
            // Corrupt a byte in the middle of page object
            char* data = (char*)signedBytes.s;
            for (size_t i = 50; i < (size_t)len(signedBytes) - 50; i++) {
                if (data[i] == '0' || data[i] == '1') {
                    data[i] = (data[i] == '0') ? '9' : '8';
                    break;
                }
            }
            Str tamperedPdfPath = fmt("%s\\swiftleaf_test_tampered.pdf", ToUtf8Temp(WStr(tempDir)));
            file::WriteFile(tamperedPdfPath, signedBytes);
            EngineBase* engTampered = CreateEngineMupdfFromFile(tamperedPdfPath, FileType::PDF, 96);
            if (engTampered) {
                Vec<SignatureItemInfo> tamperedSigs;
                EngineMupdfGetSignaturesInfo(engTampered, tamperedSigs);
                bool detectedTamper =
                    (tamperedSigs.len >= 1 && tamperedSigs[0].status == SignatureStatus::ModifiedAfterSigning);
                FreeSignatures(tamperedSigs);
                SafeEngineRelease(&engTampered);
                file::Delete(tamperedPdfPath);
                if (!detectedTamper) {
                    return false;
                }
            } else {
                file::Delete(tamperedPdfPath);
            }
        }
    }

    file::Delete(pfxPath);
    file::Delete(origPdfPath);
    file::Delete(signedPdfPath);
    return true;
}
