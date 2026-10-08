/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct VirtRoot;

constexpr int kInstallerWinDy = 340;

// DWORD 0|1 in the uninstall key: whether the install created a desktop shortcut
#define kRegDesktopShortcut "DesktopShortcut"

enum class PreviousInstallationType {
    None = 0,
    User = 1,
    Machine = 2,
    Both = 3
};

struct PreviousInstallationInfo {
    Str installationDir;
    PreviousInstallationType typ = PreviousInstallationType::None;
    bool searchFilterInstalled = false;
    bool previewInstalled = false;
    bool allUsers = false;
    // installs before the DesktopShortcut registry value always created one
    bool desktopShortcut = true;

    PreviousInstallationInfo() = default;
    ~PreviousInstallationInfo();
};

// Swiftleaf: the installer's Material 3 colors (light scheme from the teal seed)
constexpr Color kM3Surface = MkRgb(0xf4, 0xfb, 0xf8);
constexpr Color kM3OnSurface = MkRgb(0x17, 0x1d, 0x1b);
constexpr Color kM3OnSurfaceVariant = MkRgb(0x3f, 0x49, 0x45);
constexpr Color kM3Primary = MkRgb(0x00, 0x6b, 0x5a);
constexpr Color kM3PrimaryPressed = MkRgb(0x00, 0x56, 0x48);
constexpr Color kM3Outline = MkRgb(0x6f, 0x79, 0x75);
constexpr Color kM3SecondaryContainer = MkRgb(0xcd, 0xe8, 0xe0);
constexpr Color kM3OnSecondaryContainer = MkRgb(0x06, 0x20, 0x1b);
constexpr Color kM3Error = MkRgb(0xba, 0x1a, 0x1a);

// native buttons drawn as Material pills: filled (primary action) or outlined
void MakeMaterialButton(HWND hwnd);
bool DrawMaterialButton(DRAWITEMSTRUCT* dis, bool filled);
Size MaterialButtonSize(Size ideal);

// This is the height of the lower part
extern int gBottomPartDy;

extern int gButtonDy;

constexpr int kWmAppInstallationFinished = (WM_APP + 1);
constexpr int kWmAppStartInstallation = (WM_APP + 2);

extern Str gFirstError;
extern Str gDefaultMsg;
extern HWND gHwndFrame;
extern Str gMsgError;

extern Gdiplus::Color kColorMsgWelcome;
extern Gdiplus::Color kColorMsgOk;
extern Gdiplus::Color kColorMsgInstallation;
extern Gdiplus::Color kColorMsgFailed;
extern Gdiplus::Color gCol1;
extern Gdiplus::Color gCol1Shadow;
extern Gdiplus::Color gCol2;
extern Gdiplus::Color gCol2Shadow;
extern Gdiplus::Color gCol3;
extern Gdiplus::Color gCol3Shadow;
extern Gdiplus::Color gCol4;
extern Gdiplus::Color gCol4Shadow;
extern Gdiplus::Color gCol5;
extern Gdiplus::Color gCol5Shadow;

// virt: the window's virtual controls, painted on top of the frame (can be null)
void OnPaintFrame(HWND hwnd, bool skipMessage, VirtRoot* virt = nullptr);
void AnimStep();

void NotifyFailed(Str msg);

void SetMsg(Str msg, Gdiplus::Color color);
void SetDefaultMsg();

int KillProcessesWithModule(Str modulePath, bool waitUntilTerminated);

TempStr GetShortcutPathTemp(int csidl);

bool ExtractInstallerFiles(Str dir);
bool ExtractLibsumatrapdfToDir(Str destDir);

TempStr GetExistingInstallationDirTemp();
void GetPreviousInstallInfo(PreviousInstallationInfo* info);
bool IsOurExeInstalled();

bool IsPathUnderProgramFiles(Str path);
bool InstallNeedsElevation(Str installDir, bool allUsers);

TempStr GetInstallationFilePathTemp(Str installDir, Str name);

void RegisterPreviewer(bool allUsers, Str installDir);
void UnRegisterPreviewer();

void RegisterSearchFilter(bool allUsers, Str installDir);
void UnRegisterSearchFilter();

// Unregister shell extensions and kill processes holding install-dir files
// so ExtractInstallerFiles can overwrite PdfFilter.dll / PdfPreview.dll / etc.
// Call before extracting over an existing install. removedOut (optional) is
// filled for RestoreShellExtensions if install fails later.
struct ShellExtInstallState {
    bool searchFilter = false;
    bool preview = false;
    bool allUsers = false;
    Str installDir;
};
void FreeInstallationFilesInUse(Str installDir, bool allUsers, ShellExtInstallState* removedOut = nullptr);
void RestoreShellExtensions(const ShellExtInstallState& state);

bool CheckInstallUninstallPossible(HWND hwnd, bool silent = false);
Str GetInstallerLogPath();

bool IsDirInPath(Str path, Str dir);
bool WriteRegExpandSz(HKEY root, Str keyName, Str valueName, Str value);

TempStr GetRegPathUninstTemp(Str appName);

void RemoveAppShortcuts();

bool WriteUninstallerRegistryInfo(HKEY hkey, bool allUsers, Str installDir);
bool WriteExtendedFileExtensionInfo(HKEY hkey, Str installedExePath);
bool RemoveUninstallerRegistryInfo(HKEY hkey);
void RemoveInstallRegistryKeys(HKEY hkey);
int GetInstallerWinDx();

void ReRegisterFileAssociations();
void LogNonDefaultRegisteredExtensions();
void CollectNonDefaultRegisteredExtensions(StrVec& out);
void LaunchDefaultAppDialogForExtension(HWND hwnd, Str ext);
