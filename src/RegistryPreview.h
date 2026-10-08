/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#define kPdfPreviewClsid "{33197FE0-BDA9-4128-86DD-9E8912BABEAE}"
#define kXpsPreviewClsid "{17C0EC9D-887F-421D-A914-222F6D5D503B}"
#define kDjVuPreviewClsid "{D01B1968-6E13-49F6-9F68-26DFDE7CA787}"
#define kEpubPreviewClsid "{957DB259-ABBF-4817-9819-6F4FB0FBCA89}"
#define kFb2PreviewClsid "{B6074459-9245-4773-9A3C-05395ACD3E16}"
#define kMobiPreviewClsid "{E54B04C8-1D40-4939-BDC6-BB4B4835D7F3}"
#define kCbxPreviewClsid "{A246FEA1-F17D-46ED-BAD6-9D8A2C2FFB05}"
#define kTgaPreviewClsid "{711FD03E-003A-43A1-8F0E-C961D72D4E60}"

bool InstallPreviewDll(Str dllPath, bool allUsers);
bool UninstallPreviewDll();
void DisablePreviewInstallExts(Str cmdLine);
bool IsPreviewInstalled();

// opt-in file logging for the preview handler (PdfPreview.dll), controlled by a
// registry value so it can be toggled (via CmdTogglePdfPreviewLogging) without a
// rebuild. Files are written to the per-build data dir (keyed on the sibling
// SumatraPDF.exe's sha1) so they land next to the app's other logs/crash info.
#define kPdfPreviewLogPrefix "pdfpreview.log."

bool IsPdfPreviewLoggingEnabled();
void SetPdfPreviewLoggingEnabled(bool enable);
TempStr GetPdfPreviewLogDirTemp();
void StartPdfPreviewLoggingIfEnabled();
