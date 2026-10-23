# Rename notes: SumatraPDF -> SwiftLeaf (limited rename)

User-visible text, comments, installer text, version-info resources and shown URLs were renamed.
Identifiers, namespaces, macros, source/project file names were not.
The names below are persisted or are IPC / interop names, so they were left as is (name, file:line, reason).

## Persisted names (settings, registry, folders, file names)
- `Software\SumatraPDF` (kRegKeySumatra) - src/RegistryPreview.cpp:170 - registry key read/written by the preview; changing it drops users' setting.
- `SOFTWARE\SumatraPDF\Capabilities` - src/shared/RegistryInstaller.cpp:138,149 (comments; the code builds the path from kAppName) - default-apps registration.
- `SumatraPDF.pdf` ProgID example - src/shared/RegistryInstaller.cpp:155 (comment).
- `SumatraPDF-settings.txt`, `sumatrapdfprefs.dat` - src/Uninstaller.cpp:58-59 - old settings/prefs files the uninstaller must still delete.
- `SumatraPDF-QuickLook` (Run value) - src/shared/ExplorerQuickLook.cpp:81, src/Uninstaller.cpp:160 - autostart registry value, must match across install/uninstall.
- `sumatrapdfcache` - src/FileThumbnails.cpp:52 - thumbnail cache dir in AppData.
- `sumatrapdfrestrict.ini` - src/SumatraPDF.cpp:160 - policy file name read next to the exe (documented name).
- `sumatrapdfcrash.dmp` - src/SumatraPDF.cpp:18417 - crash dump file name.
- `sumatra-install-log.txt`, `sumatra-log.txt` - src/Installer.cpp:120, src/SumatraPDF.cpp:10569 - log files (names referenced by help text at src/SumatraPDF.cpp:17411).
- `Sumatra-Uninstaller.exe` - src/Uninstaller.cpp:382,396 - installed helper exe name.
- `sumatrapdf-tool.exe` - src/SumatraPDF.cpp:18006, src/sumatrapdf-tool.cpp, src/Installer.cpp:943 - installed console helper; matches the build output name.
- `libsumatrapdf.dll` - many files - build/output name of the shared library (project name).
- `SumatraPDF.exe.manifest` - src/SumatraPDF.rc:127, src/ng/SumatraPDF.rc:72 - existing source file src/shared/SumatraPDF.exe.manifest.
- `sumatrapdf` value of a settings enum ("valid values: (empty), os, sumatrapdf") - src/Settings.cpp:1079 - persisted value in the settings file.
- AppData folder is already `Swiftleaf-data` (kAppName) - not touched.

## IPC / interop names
- `SumatraOpenCopyData`, `SumatraOpenManyCopyData` - src/SearchAndDDE.h:24,29 - WM_COPYDATA payload structs (identifiers).
- `SumatraPDF-%08x` - src/SumatraPDF.cpp:16441 - file-mapping name shared between processes.
- Window classes: `SUMATRA_PDF_FRAME` (src/GlobalHotkeys.cpp:34), `SUMATRA_PDF_CANVAS` (src/SumatraPDF.cpp:158), `SUMATRA_PDF_ABOUT`, `SUMATRA_ABOUT_HOVER` (src/HomePage.cpp:888,2972), `SUMATRA_PDF_INSTALLER_FRAME` (src/Installer.cpp:2082, src/Uninstaller.cpp:215), `SUMATRA_PDF_IMAGE_EDIT` (src/ImageSaveCropResize.cpp:78), `SUMATRA_ANNOT_FILTER_WND`, `SUMATRA_OVERLAY_SCROLLBAR`, `SUMATRA_VIRT_TOOLBAR`, `SUMATRA_PLATFORM_WINDOW`, `SUMATRA_PDF_QUICKLOOK_AGENT`, `SumatraPDFClipboardOwner` (src/base/Win.cpp:1236), `SumatraPDFRefHover` (src/shared/RefHover.h:284), `SumatraPDF_PreviewPane` (src/previewer/PdfPreview.cpp:718), and the `Sumatra*` annotation/toolbar/tab classes.
- `SumatraPDF-QuickLookAgent` mutex - src/shared/ExplorerQuickLook.cpp:80.
- DDE server `SWIFTLEAF` is already renamed (src/SearchAndDDE.h:4).
- Env vars `SUMATRAPDF_FULLDUMP` (src/SumatraPDF.cpp:18419), `SUMATRAPDF_NO_GHOSTSCRIPT_TIMEOUT` (src/shared/EngineDvi.cpp:166, EnginePs.cpp:152), `SUMATRA_DPI_OVERRIDE` (src/shared/gui/Dpi.cpp:30), `SUMATRA_TTS_FORCE_SAPI` (src/ReadAloud.cpp:167) - user-facing switches for existing scripts.
- HTTP user agent `SumatraPdfHTTP` - src/base/Http.cpp:84, src/base/Http_posix.cpp:63,235.
- Internal WebView/JS names (`window.__sumatra__`, `__sumatraFind`, CSS `sumatra-find`, virtual hosts `https://sumatrapdf.manual/` etc.) - src/gui/win/*, src/SumatraPDF.cpp - not user-visible.

## Update-check protocol and upstream endpoints (functional, left)
- `[SumatraPDF]` section name and `SumatraPDF` key in the update info format - src/UpdateCheck.cpp:126,1237, src/ng/UpdateCheck.cpp:141-167, src/base/tests/SquareTreeParser_ut.cpp - the server file format.
- `sumatrapdfreader.org` update/download/crash-upload endpoints: src/ng/UpdateCheck.cpp:64-75,544, src/UpdateCheck.cpp (kExpectedDlHost), src/SumatraPDF.cpp:18242 (minidump), src/Installer.cpp:2414.
- Help-page links `https://www.sumatrapdfreader.org/docs/...` built from `Help/<page>` tip links: src/SumatraPDF.cpp:11575-11577,11646, src/gui/VirtCtrl.cpp:3844, src/ng/TipMarkup.cpp:17, src/ng/SumatraPDF.cpp:811 - there is no SwiftLeaf docs site, page paths would 404 on GitHub.

## Credit text kept
- Copyright lines ("the SumatraPDF project authors") in file headers, `kCopyrightStr` (src/shared/Version.h:57), and the AUTHORS credit.

## Translations
- UI source strings now say "SwiftLeaf". src/shared/Translations.cpp matches a rebranded source string to the original translation key (which still says "SumatraPDF"), and rebrands translated text, so existing translations keep working.
