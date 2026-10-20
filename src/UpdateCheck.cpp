/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include <sddl.h>
#include <wincrypt.h>
#include "base/UITask.h"
#include "base/SquareTreeParser.h"
#include "base/Http.h"
#include "base/Win.h"
#include "base/File.h"
#include "base/Crypto.h"
#include "base/ScopedWin.h"

#include "gui/Layout.h"
#include "gui/UIModels.h"
#include "gui/win/WinGui.h"
#include "gui/win/WebView.h"

#include "Settings.h"
#include "AppTools.h"
#include "AppSettings.h"
#include "Version.h"
#include "SumatraConfig.h"
#include "Translations.h"
#include "SumatraPDF.h"
#include "Flags.h"
#include "Notifications.h"
#include "MainWindow.h"
#include "HomePage.h"
#include "Installer.h"
#include "UpdateCheck.h"

static Kind kNotifUpdateCheckInProgress = StrL("notifUpdateCheckInProgress").s;

// certificate on www.sumatrapdfreader.org is not supported by win7 and win8.1
// (doesn't have the ciphers they understand) so we have a backup on backblaze

// Swiftleaf: every GitHub release carries update-check.txt (written by CI);
// releases/latest/download redirects to the newest release's copy
static const Str updateInfoURLs[] = {
    StrL("https://github.com/TheFunzzies/swiftleaf-pdf/releases/latest/download/update-check.txt"),
};

#define kWebisteDownloadPageURL "https://github.com/TheFunzzies/swiftleaf-pdf/releases/latest"

// prevent multiple update tasks from happening simultaneously
// (this might e.g. happen if a user checks manually very quickly after startup)
static bool gUpdateCheckInProgress = false;

// when true, NotifyUserOfUpdate skips the install-confirmation dialog and just
// installs (set when the user clicks "Download and update" in the pre-release
// update notification)
static bool gUpdateAutoInstall = false;

constexpr i64 kMaxUpdateDownloadSize = 256LL * 1024 * 1024;
constexpr int kUpdateDigestSize = 32;

// the bottom-left "update available" notification (with the download link)
static Kind kNotifUpdateAvailable = StrL("notifUpdateAvailable").s;

struct UpdateInfo : NonCopyable {
    HWND hwndParent = nullptr;
    Str latestVer;

    Str installer64;
    Str portable64;

    Str installer32;
    Str portable32;

    Str installerArm64;
    Str portableArm64;

    Str dlURL;
    // hex SHA-256 of what dlURL serves: Swiftleaf builds aren't code-signed,
    // so this is what a download is checked against
    Str dlSha256;
    Str installerPath;
    Str stagingDir;
    HANDLE stagingLock = INVALID_HANDLE_VALUE;
    HANDLE payloadLock = INVALID_HANDLE_VALUE;
    bool launched = false;
    Str builtOn; // optional "yyyy-mm-dd" from the update-check file

    UpdateInfo() = default;
    ~UpdateInfo() {
        if (payloadLock != INVALID_HANDLE_VALUE) {
            CloseHandle(payloadLock);
        }
        if (stagingLock != INVALID_HANDLE_VALUE) {
            CloseHandle(stagingLock);
        }
        if (!launched && stagingDir) {
            if (installerPath) {
                file::Delete(installerPath);
            }
            RemoveDirectoryW(CWStrTemp(stagingDir));
        }
        str::Free(latestVer);
        str::Free(installer64);
        str::Free(portable64);
        str::Free(installer32);
        str::Free(portable32);
        str::Free(installerArm64);
        str::Free(portableArm64);
        str::Free(dlURL);
        str::Free(dlSha256);
        str::Free(installerPath);
        str::Free(stagingDir);
        str::Free(builtOn);
    }
};

// an available update surfaced by the pre-release startup notification; the
// "Download and update" link downloads & installs it (owned here until then)
static UpdateInfo* gPendingUpdate = nullptr;

bool HasPendingPreReleaseUpdate() {
    return gPendingUpdate != nullptr;
}

/*
The format of update information downloaded from the server:

[SumatraPDF]
Latest: 14276
BuiltOn: 2026-08-21
Installer64: https://www.sumatrapdfreader.org/dl/prerel/14276/SumatraPDF-prerel-64-install.exe
Installer32: https://www.sumatrapdfreader.org/dl/prerel/14276/SumatraPDF-prerel-install.exe
PortableExe64: https://www.sumatrapdfreader.org/dl/prerel/14276/SumatraPDF-prerel-64.exe
PortableExe32: https://www.sumatrapdfreader.org/dl/prerel/14276/SumatraPDF-prerel.exe
PortableZip64: https://www.sumatrapdfreader.org/dl/prerel/14276/SumatraPDF-prerel-64.zip
PortableZip32: https://www.sumatrapdfreader.org/dl/prerel/14276/SumatraPDF-prerel.zip

[Promo]
[
    Name = MarkLexis
    URL = https://marklexis.arslexis.io
    Info = Bookmarking web application
]
*/
static UpdateInfo* ParseUpdateInfo(Str d) {
    // if a user configures os-wide proxy that is not a regular ie proxy
    // (which we pick up) we might get garbage http response
    // check if response looks valid
    if (len(d) == 0) {
        return nullptr;
    }
    Str prefix = (d.s[0] == '[') ? StrL("[Swiftleaf]") : StrL("Swiftleaf");
    if (!str::StartsWith(d, prefix)) {
        return nullptr;
    }

    SquareTreeNode* root = ParseSquareTree(d);
    if (!root) {
        return nullptr;
    }
    AutoDelete delRoot(root);

    SetPromoString(SerializeSquareTreeNodeTemp(root->GetChild(StrL("Promo"))));

    SquareTreeNode* node = root->GetChild(StrL("Swiftleaf"));
    if (!node) {
        return nullptr;
    }

    Str latestVer = node->GetValue(StrL("Latest"));
    if (!IsValidProgramVersion(latestVer)) {
        return nullptr;
    }
    auto* res = new UpdateInfo();
    res->latestVer = str::Dup(latestVer);
    Str onDate = node->GetValue(StrL("BuiltOn"));
    if (onDate) {
        res->builtOn = str::Dup(onDate);
    }

    // those are optional. if missing, we'll just tell the user to go to website to download
    res->installer64 = str::Dup(node->GetValue(StrL("Installer64")));
    res->installerArm64 = str::Dup(node->GetValue(StrL("InstallerArm64")));
    res->installer32 = str::Dup(node->GetValue(StrL("Installer32")));

    res->portable64 = str::Dup(node->GetValue(StrL("PortableExe64")));
    res->portableArm64 = str::Dup(node->GetValue(StrL("PortableExeArm64")));
    res->portable32 = str::Dup(node->GetValue(StrL("PortableExe32")));

    // figure out which executable to download
    Str dlURL;
    Str shaKey;
    bool isDll = IsDllBuild();
    if (IsArmBuild()) {
        dlURL = isDll ? res->installerArm64 : res->portableArm64;
        shaKey = isDll ? StrL("Sha256InstallerArm64") : StrL("Sha256PortableExeArm64");
    } else if (IsProcess64()) {
        dlURL = isDll ? res->installer64 : res->portable64;
        shaKey = isDll ? StrL("Sha256Installer64") : StrL("Sha256PortableExe64");
    } else {
        dlURL = isDll ? res->installer32 : res->portable32;
        shaKey = isDll ? StrL("Sha256Installer32") : StrL("Sha256PortableExe32");
    }
    res->dlURL = str::Dup(dlURL);
    res->dlSha256 = str::Dup(node->GetValue(shaKey));
    return res;
}

static bool ShouldCheckForUpdate(UpdateCheck updateCheckType) {
    if (gUpdateCheckInProgress) {
        logf("CheckForUpdate: skipping because gUpdateCheckInProgress\n");
        return false;
    }

    // when forcing, we download pre-release, which shows greater version than our build
    // so we don't want to download during automatic check, only when user initiated

#ifdef FORCE_AUTO_UPDATE
    if (updateCheckType == UpdateCheck::UserInitiated) {
        logf("CheckForUpdate: checking, user initiated (FORCE_AUTO_UPDATE)\n");
        return true;
    } else {
        logf("CheckForUpdate: skipping auto check, FORCE_AUTO_UPDATE downloads pre-release\n");
        return false;
    }
#endif

    if (!HasPermission(Perm::InternetAccess)) {
        logf("CheckForUpdate: skipping because no internet access\n");
        return false;
    }

    if (updateCheckType == UpdateCheck::UserInitiated) {
        logf("CheckForUpdate: checking, user initiated\n");
        return true;
    }

    // don't check if the timestamp or version to skip can't be updated
    // (mainly in plugin mode, stress testing and restricted settings)
    if (!HasPermission(Perm::SavePreferences)) {
        logf("CheckForUpdate: skipping auto check because no prefs access\n");
        return false;
    }

    // only applies to automatic update check
    if (!gSettings->checkForUpdates) {
        logf("CheckForUpdate: skipping auto check because CheckForUpdates is false\n");
        return false;
    }

    // don't check for updates at the first start, so that privacy
    // sensitive users can disable the update check in time
    FILETIME never{};
    if (FileTimeEq(gSettings->timeOfLastUpdateCheck, never)) {
        logf("CheckForUpdate: skipping auto check, first start (TimeOfLastUpdateCheck not set)\n");
        return false;
    }

    // pre-release builds check on every startup (testers want the newest build);
    // skip the daily/weekly throttle below
    if (gIsPreReleaseBuild) {
        logf("CheckForUpdate: checking, pre-release build checks on every startup\n");
        return true;
    }

    // only check if at least a day passed since last check
    FILETIME currentTimeFt;
    GetSystemTimeAsFileTime(&currentTimeFt);
    int secsSinceLastUpdate = FileTimeDiffInSecs(currentTimeFt, gSettings->timeOfLastUpdateCheck);

    constexpr int kSecondsInDay = 60 * 60 * 24;
    constexpr int kSecondsInWeek = 7 * 60 * 60 * 24;

    int secsBetweenChecks = gIsPreReleaseBuild ? kSecondsInWeek : kSecondsInDay;
    bool checkUpdate = secsSinceLastUpdate > secsBetweenChecks;
    logf("CheckForUpdate: %s auto check, %d secs since the last one, %d secs between checks\n",
         checkUpdate ? StrL("doing") : StrL("skipping"), secsSinceLastUpdate, secsBetweenChecks);
    return checkUpdate;
}

// Use a protected DACL rather than trusting the ACL inherited from TEMP.
static bool CreateUpdateStage(UpdateInfo* info) {
    AutoCloseHandle token;
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        return false;
    }
    token = rawToken;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (!size) {
        return false;
    }
    auto* user = (TOKEN_USER*)AllocArrayTemp<u8>(size);
    if (!GetTokenInformation(token, TokenUser, user, size, &size)) {
        return false;
    }
    WCHAR* sid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid)) {
        return false;
    }
    defer {
        LocalFree(sid);
    };
    TempStr acl = fmt("D:P(A;OICI;FA;;;%s)(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", WStr(sid));
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(CWStrTemp(acl), SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }
    defer {
        LocalFree(sd);
    };

    TempStr parent = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA);
    GUID id{};
    constexpr int kGuidTextSize = 39;
    WCHAR name[kGuidTextSize]{};
    if (len(parent) == 0 || FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, name, dimof(name))) {
        return false;
    }
    TempStr dir = path::JoinTemp(parent, fmt("Swiftleaf-update-%s", WStr(name)));
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    if (!CreateDirectoryW(CWStrTemp(dir), &sa)) {
        return false;
    }
    info->stagingDir = str::Dup(dir);
    info->stagingLock = CreateFileW(CWStrTemp(dir), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (info->stagingLock == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION attrs{};
    if (!GetFileInformationByHandle(info->stagingLock, &attrs) ||
        (attrs.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return false;
    }
    info->installerPath = str::Dup(path::JoinTemp(dir, StrL("Swiftleaf-update.exe")));
    return true;
}

static HANDLE CreateUpdatePayload(Str path) {
    return CreateFileW(CWStrTemp(path), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
}

static HANDLE OpenUpdatePayload(Str path) {
    return CreateFileW(CWStrTemp(path), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
}

static i64 UpdatePayloadSize(HANDLE payload) {
    BY_HANDLE_FILE_INFORMATION attrs{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(payload, &attrs) ||
        (attrs.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        attrs.nNumberOfLinks != 1 || !GetFileSizeEx(payload, &size) || size.QuadPart <= 0 ||
        size.QuadPart > kMaxUpdateDownloadSize) {
        return -1;
    }
    return size.QuadPart;
}

static bool CopyUpdatePayload(HANDLE source, HANDLE dest) {
    i64 size = UpdatePayloadSize(source);
    LARGE_INTEGER start{};
    if (size <= 0 || !SetFilePointerEx(source, start, nullptr, FILE_BEGIN)) {
        return false;
    }
    constexpr int kCopyReadSize = 64 * 1024;
    char buf[kCopyReadSize];
    i64 total = 0;
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(source, buf, sizeof(buf), &n, nullptr)) {
            return false;
        }
        if (!n) {
            return total == size && FlushFileBuffers(dest);
        }
        total += n;
        if (total > size || !file::WriteAll(dest, Str(buf, (int)n))) {
            return false;
        }
    }
}

// Hash the locked file, including a fresh check before an elevation handoff.
static bool IsUpdateHashValid(HANDLE payload, Str wantHex) {
    if (len(wantHex) != kUpdateDigestSize * 2) {
        return false;
    }
    i64 size = UpdatePayloadSize(payload);
    LARGE_INTEGER start{};
    if (size <= 0 || !SetFilePointerEx(payload, start, nullptr, FILE_BEGIN)) {
        return false;
    }
    HCRYPTPROV provider = 0;
    if (!CryptAcquireContextW(&provider, nullptr, MS_ENH_RSA_AES_PROV, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return false;
    }
    defer {
        CryptReleaseContext(provider, 0);
    };
    HCRYPTHASH hash = 0;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        return false;
    }
    defer {
        CryptDestroyHash(hash);
    };
    constexpr int kHashReadSize = 64 * 1024;
    char buf[kHashReadSize];
    i64 total = 0;
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(payload, buf, sizeof(buf), &n, nullptr)) {
            return false;
        }
        if (!n) {
            break;
        }
        total += n;
        if (total > kMaxUpdateDownloadSize || !CryptHashData(hash, (BYTE*)buf, n, 0)) {
            return false;
        }
    }
    u8 digest[kUpdateDigestSize];
    DWORD digestSize = sizeof(digest);
    if (total != size || !CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0) || digestSize != sizeof(digest)) {
        return false;
    }
    TempStr gotHex = str::MemToHexTemp(Str((char*)digest, (int)sizeof(digest)));
    bool ok = str::EqI(gotHex, wantHex);
    logf("IsUpdateHashValid: got %s, want %s\n", gotHex, wantHex);
    return ok;
}

enum class UpdateTrust {
    ManifestHash,
    Publisher
};

static bool IsUpdateSignerValid(Str path, UpdateTrust trust) {
    TempStr expected = GetExecutableSignerTemp(GetSelfExePathTemp());
    // Unsigned releases trust GitHub's manifest; require a signer once we ship one.
    if (len(expected) == 0 && trust == UpdateTrust::ManifestHash) {
        return true;
    }
    TempStr signer = GetExecutableSignerTemp(path);
    return expected && signer && str::Eq(expected, signer) && IsPEFileSigned(path);
}

static bool LaunchLockedUpdate(Str installerPath) {
    bool installed = IsOurExeInstalled();
    bool elevate = false;
    str::Builder cmd;
    if (installed) {
        PreviousInstallationInfo previous;
        GetPreviousInstallInfo(&previous);
        Str dir = GetSelfExeDirTemp();
        cmd.Append(fmt(R"( -fast-install -update-no-elevate -install-dir "%s")", dir));
        if (previous.allUsers) {
            cmd.Append(StrL(" -all-users"));
        }
        elevate = !IsProcessRunningElevated() && InstallNeedsElevation(dir, previous.allUsers);
    } else {
        // we're asking to over-write over ourselves, so also wait 2 secs to allow
        // our process to exit
        cmd.Append(fmt(R"( -sleep-ms 2000 -exit-when-done -update-self-to "%s")", GetSelfExePathTemp()));
    }
    logf("StartInstallerAutoUpgrade: installer cmd: '%s'\n", ToStr(cmd));
    if (!elevate) {
        return CreateProcessHelper(installerPath, ToStr(cmd));
    }

    // Finish UAC while the verified payload and its directory are still locked.
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = CWStrTemp(installerPath);
    sei.lpParameters = CWStrTemp(ToStr(cmd));
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        LogLastError();
        return false;
    }
    AutoCloseHandle process(sei.hProcess);
    return process.IsValid();
}

static bool LaunchVerifiedUpdate(UpdateInfo* info) {
    if (info->payloadLock == INVALID_HANDLE_VALUE || !IsUpdateHashValid(info->payloadLock, info->dlSha256) ||
        !IsUpdateSignerValid(info->installerPath, UpdateTrust::ManifestHash)) {
        return false;
    }
    info->launched = LaunchLockedUpdate(info->installerPath);
    return info->launched;
}

// Legacy -upgrade-from has no manifest hash, so it must authenticate the publisher.
void StartInstallerAutoUpgrade(Str installerPath) {
    if (len(GetExecutableSignerTemp(GetSelfExePathTemp())) == 0) {
        logf("StartInstallerAutoUpgrade: refusing an unauthenticated update\n");
        return;
    }
    AutoCloseHandle source(OpenUpdatePayload(installerPath));
    UpdateInfo staged;
    if (!source.IsValid() || !CreateUpdateStage(&staged)) {
        return;
    }
    {
        AutoCloseHandle writer(CreateUpdatePayload(staged.installerPath));
        if (!writer.IsValid() || !CopyUpdatePayload(source, writer)) {
            return;
        }
    }
    staged.payloadLock = OpenUpdatePayload(staged.installerPath);
    if (staged.payloadLock == INVALID_HANDLE_VALUE ||
        !IsUpdateSignerValid(staged.installerPath, UpdateTrust::Publisher)) {
        return;
    }
    staged.launched = LaunchLockedUpdate(staged.installerPath);
}

static void ExitAfterStartingUpdater() {
    // Exit immediately so the updater can overwrite our exe. PostQuitMessage(0)
    // is unreliable when the dialog was shown from a uitask during startup.
    if (gPluginMode) {
        PostQuitMessage(0);
        return;
    }
    ::ExitProcess(0);
}

static void NotifyUserOfUpdate(UpdateInfo* updateInfo) {
    auto installerPathAuto = updateInfo->installerPath;
    // auto-install path: the user already opted in via the "Download and update"
    // link, so skip the confirmation dialog and just install (issue: pre-release
    // one-click update)
    if (gUpdateAutoInstall) {
        gUpdateAutoInstall = false;
        // persist timeOfLastUpdateCheck before a possible ExitProcess
        ScheduleSaveSettings();
        FlushScheduledSaveSettings();
        if (installerPathAuto && file::Exists(installerPathAuto)) {
            if (LaunchVerifiedUpdate(updateInfo)) {
                ExitAfterStartingUpdater();
            }
        } else {
            logf("NotifyUserOfUpdate: auto-install requested but installer not downloaded\n");
        }
        return;
    }

    auto mainInstr = Tr("New version available");
    auto ver = updateInfo->latestVer;
    auto fmtStr = Tr("You have version '%s' and version '%s' is available.\nDo you want to install the new version?");
    TempStr content = fmt(fmtStr.s, StrL(CURR_VERSION_STRA), ver);

    auto installerPath = updateInfo->installerPath;
    bool didDownloadInstaller = file::Exists(installerPath);

    constexpr int kBtnIdDontInstall = 100;
    constexpr int kBtnIdInstall = 101;
    auto title = Tr("Swiftleaf Update");
    TASKDIALOGCONFIG dialogConfig{};
    TASKDIALOG_BUTTON buttons[2];

    buttons[0].nButtonID = kBtnIdDontInstall;
    auto s = Tr("Don't install");
    buttons[0].pszButtonText = CWStrTemp(s);
    buttons[1].nButtonID = kBtnIdInstall;
    if (didDownloadInstaller) {
        s = Tr("Install and relaunch");
    } else {
        s = Tr("Download update");
    }
    buttons[1].pszButtonText = CWStrTemp(s);

    DWORD flags =
        TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT | TDF_ENABLE_HYPERLINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    if (trans::IsCurrLangRtl()) {
        flags |= TDF_RTL_LAYOUT;
    }
    dialogConfig.cbSize = sizeof(TASKDIALOGCONFIG);
    dialogConfig.pszWindowTitle = CWStrTemp(title);
    dialogConfig.pszMainInstruction = CWStrTemp(mainInstr);
    dialogConfig.pszContent = CWStrTemp(content);
    dialogConfig.nDefaultButton = kBtnIdInstall;
    dialogConfig.dwFlags = (TASKDIALOG_FLAGS)flags;
    dialogConfig.cxWidth = 0;
    dialogConfig.pfCallback = nullptr;
    dialogConfig.dwCommonButtons = 0;
    dialogConfig.cButtons = dimof(buttons);
    dialogConfig.pButtons = &buttons[0];
    dialogConfig.pszMainIcon = TD_INFORMATION_ICON;
    dialogConfig.hwndParent = updateInfo->hwndParent;

    int buttonPressedId = 0;

    auto hr = TaskDialogIndirect(&dialogConfig, &buttonPressedId, nullptr, nullptr);
    ReportIf(hr == E_INVALIDARG);
    bool doInstall = (hr == S_OK) && (buttonPressedId == kBtnIdInstall);

    // persist timeOfLastUpdateCheck before a possible ExitProcess
    ScheduleSaveSettings();
    FlushScheduledSaveSettings();
    if (!doInstall) {
        return;
    }

    // if installer not downloaded tell user to download from website
    if (!didDownloadInstaller) {
        SumatraLaunchBrowser(StrL(kWebisteDownloadPageURL));
        return;
    }

    if (LaunchVerifiedUpdate(updateInfo)) {
        ExitAfterStartingUpdater();
    }
}

struct UpdateProgressData {
    HWND hwndForNotif = nullptr;
    i64 nDownloaded = 0;
};

struct DownloadUpdateAsyncData {
    HWND hwndForNotif = nullptr;
    UpdateInfo* updateInfo = nullptr;
    HttpProgress httpProgress = {};

    DownloadUpdateAsyncData() = default;
    ~DownloadUpdateAsyncData() { delete updateInfo; }
};

static void DownloadUpdateFinish(DownloadUpdateAsyncData* data) {
    auto* hwndForNotif = data->hwndForNotif;
    auto* updateInfo = data->updateInfo;
    data->updateInfo = nullptr;
    RemoveNotificationsForGroup(hwndForNotif, kNotifUpdateCheckInProgress);
    NotifyUserOfUpdate(updateInfo);
    delete updateInfo;
    gUpdateCheckInProgress = false;
    delete data;
}

static void UpdateDownloadProgressNotif(UpdateProgressData* data) {
    TempStr size = FormatFileSizeShortTransTemp(data->nDownloaded);
    logf("UpdateDownloadProgressNotif: %s\n", size);
    auto* wnd = GetNotificationForGroup(data->hwndForNotif, kNotifUpdateCheckInProgress);
    if (wnd) {
        TempStr msg = fmt("Downloading update: %s\n", size);
        NotificationUpdateMessage(wnd, msg, 0, true);
    } else {
        logf("UpdateDownloadProgressNotif: no wnd\n");
    }
    delete data;
}

static void UpdateProgressCb(UpdateProgressData* data, HttpProgress* progress) {
    logf("UpdateProgressCb: n: %d\n", (int)progress->nDownloaded);
    auto* fnData = new UpdateProgressData;
    fnData->hwndForNotif = data->hwndForNotif;
    fnData->nDownloaded = progress->nDownloaded;
    auto fn = MkFunc0<UpdateProgressData>(UpdateDownloadProgressNotif, fnData);
    uitask::Post(fn, nullptr);
}

static void DownloadUpdateAsync(DownloadUpdateAsyncData* data) {
    auto* hwndForNotif = data->hwndForNotif;
    auto* updateInfo = data->updateInfo;

    // sum<hex>.tmp.exe stays after install; the .tmp stub is unused. Sweep leftovers first.
    constexpr int kStaleUpdateExeSec = 24 * 60 * 60;
    DeleteStaleUpdateTemps(GetTempDirTemp(), {}, kStaleUpdateExeSec);

    UpdateProgressData pd;
    pd.hwndForNotif = hwndForNotif;
    auto cb = MkFunc1<UpdateProgressData, HttpProgress*>(UpdateProgressCb, &pd);
    bool ok = CreateUpdateStage(updateInfo);
    if (ok) {
        AutoCloseHandle writer(CreateUpdatePayload(updateInfo->installerPath));
        ok = writer.IsValid() && HttpGetToHandle(updateInfo->dlURL, writer, cb, kMaxUpdateDownloadSize) &&
             FlushFileBuffers(writer);
    }
    if (ok) {
        updateInfo->payloadLock = OpenUpdatePayload(updateInfo->installerPath);
        ok = IsUpdateHashValid(updateInfo->payloadLock, updateInfo->dlSha256) &&
             IsUpdateSignerValid(updateInfo->installerPath, UpdateTrust::ManifestHash);
    }
    logf("DownloadUpdateAsync: verified=%d, path='%s'\n", (int)ok, updateInfo->installerPath);
    if (!ok) {
        if (updateInfo->payloadLock != INVALID_HANDLE_VALUE) {
            CloseHandle(updateInfo->payloadLock);
            updateInfo->payloadLock = INVALID_HANDLE_VALUE;
        }
        if (updateInfo->installerPath) {
            file::Delete(updateInfo->installerPath);
        }
        str::Free(updateInfo->installerPath);
        updateInfo->installerPath = {};
    }

    // process the rest on ui thread to avoid threading issues
    auto fn = MkFunc0<DownloadUpdateAsyncData>(DownloadUpdateFinish, data);
    uitask::Post(fn, "TaskShowAutoUpdateDialog");
}

// pre-release builds surface an available update with a bottom-left notification
// whose "Download and update" link triggers a one-click download + install
static void ShowUpdateAvailableNotification(MainWindow* win, UpdateInfo* updateInfo) {
    if (!win || !updateInfo) {
        return;
    }
    TempStr link = fmt("[%s](CmdInstallPrereleaseUpdate)", Tr("Update"));
    // pre-release "Latest" is a build number (e.g. 17616); show as 3.7.17616
    TempStr displayVer = updateInfo->latestVer;
    if (!str::ContainsChar(displayVer, '.')) {
        displayVer = fmt("%s.%s", StrL(CURR_VERSION_MAJOR_STRA), displayVer);
    }
    TempStr msg;
    if (updateInfo->builtOn) {
        msg = fmt(Tr("Version %s from %s available. %s").s, displayVer, updateInfo->builtOn, link);
    } else {
        msg = fmt(Tr("Version %s available. %s").s, displayVer, link);
    }
    NotificationCreateArgs args;
    args.hwndParent = win->hwndCanvas;
    args.msg = msg;
    args.warning = true; // yellowish background so it stands out
    args.groupId = kNotifUpdateAvailable;
    args.timeoutMs = 0; // persist until the user clicks the link or closes it
    args.corner = NotifCorner::BottomLeft;
    args.xMargin = 2;
    args.yMargin = 2;
    ShowNotification(args);
}

// called when the user clicks "Download and update" in the pre-release update
// notification: download the pending update and (via gUpdateAutoInstall) install
// it without the confirmation dialog
// download + install the update surfaced by the pre-release update notification
void DownloadAndInstallPendingUpdate(MainWindow* win) {
    if (!win || !gPendingUpdate) {
        return;
    }
    UpdateInfo* updateInfo = gPendingUpdate;
    gPendingUpdate = nullptr;
    gUpdateAutoInstall = true;

    HWND hwndForNotif = win->hwndCanvas;
    updateInfo->hwndParent = win->hwndFrame;
    RemoveNotificationsForGroup(hwndForNotif, kNotifUpdateAvailable);

    // progress notification updated by UpdateDownloadProgressNotif (same group)
    NotificationCreateArgs nargs;
    nargs.hwndParent = hwndForNotif;
    nargs.msg = Tr("Downloading update...");
    nargs.warning = true;
    nargs.groupId = kNotifUpdateCheckInProgress;
    nargs.timeoutMs = 0;
    nargs.corner = NotifCorner::BottomLeft;
    nargs.xMargin = 2;
    nargs.yMargin = 2;
    ShowNotification(nargs);

    gUpdateCheckInProgress = true;
    auto* fnData = new DownloadUpdateAsyncData;
    fnData->hwndForNotif = hwndForNotif;
    fnData->updateInfo = updateInfo;
    auto fn = MkFunc0<DownloadUpdateAsyncData>(DownloadUpdateAsync, fnData);
    RunAsync(fn, StrL("DownloadUpdateAsync"));
}

static bool ShouldDownloadUpdate(UpdateInfo* updateInfo) {
    if (gIsStoreBuild) {
        // I assume store will take care of updates
        return false;
    }
    Str latestVer = updateInfo->latestVer;
    Str myVer = StrL(UPDATE_CHECK_VERA);
    if (gIsDebugBuild) {
        // in debug build we compare against pre-rel version, like "17616"
        // but our version is like "3.6" so it triggers update
        myVer = StrL("50000");
    }
    bool hasUpdate = CompareProgramVersion(latestVer, myVer) > 0;
    return hasUpdate;
}

static HRESULT CALLBACK TaskDialogHyperlinkCallback(HWND /*hwnd*/, UINT msg, WPARAM /*wParam*/, LPARAM lParam,
                                                    LONG_PTR /*lpRefData*/) {
    if (msg == TDN_HYPERLINK_CLICKED) {
        WCHAR* url = (WCHAR*)lParam;
        SumatraLaunchBrowser(ToUtf8Temp(url));
    }
    return S_OK;
}

static const Str kExpectedDlHost = StrL("https://github.com/TheFunzzies/swiftleaf-pdf/releases/download/");
constexpr int kUrlHexHead = 40;

static bool IsTrustedUpdateDlUrl(Str dlURL) {
    return str::StartsWith(dlURL, kExpectedDlHost);
}

static TempStr HexHeadTemp(Str s, int nMax) {
    if (!s.s || len(s) <= 0) {
        return StrL("");
    }
    int n = std::min(len(s), nMax);
    return str::MemToHexTemp(Str(s.s, n));
}

static void NotifySuspiciousUpdate(HWND hwndParent, Str dlURL) {
    logf("NotifySuspiciousUpdate: suspicious download url '%s'\n", dlURL);
    logf("  urlLen=%d hostLen=%d host='%s'\n", len(dlURL), len(kExpectedDlHost), kExpectedDlHost);
    logf("  url hex[0..%d]=%s\n", kUrlHexHead, HexHeadTemp(dlURL, kUrlHexHead));
    logf("  host hex[0..%d]=%s\n", kUrlHexHead, HexHeadTemp(kExpectedDlHost, kUrlHexHead));
    auto title = Tr("Swiftleaf Update");
    auto content = fmt(R"(Suspicious update.

Download link should come from <a href="%s">%s</a> but is %s.

Visit <a href="%s">%s</a> to download the latest version.)",
                       kExpectedDlHost, kExpectedDlHost, dlURL, kExpectedDlHost, kExpectedDlHost);

    TASKDIALOGCONFIG dialogConfig{};
    DWORD flags =
        TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT | TDF_ENABLE_HYPERLINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    if (trans::IsCurrLangRtl()) {
        flags |= TDF_RTL_LAYOUT;
    }

    constexpr int kBtnIdVisitWebsite = 100;
    TASKDIALOG_BUTTON buttons[1];
    buttons[0].nButtonID = kBtnIdVisitWebsite;
    buttons[0].pszButtonText = CWStrTemp(Tr("Visit &Website"));

    dialogConfig.cbSize = sizeof(TASKDIALOGCONFIG);
    dialogConfig.pszWindowTitle = CWStrTemp(title);
    dialogConfig.pszContent = CWStrTemp(content);
    dialogConfig.dwFlags = (TASKDIALOG_FLAGS)flags;
    dialogConfig.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    dialogConfig.cButtons = dimof(buttons);
    dialogConfig.pButtons = buttons;
    dialogConfig.nDefaultButton = kBtnIdVisitWebsite;
    dialogConfig.pszMainIcon = TD_WARNING_ICON;
    dialogConfig.hwndParent = hwndParent;
    dialogConfig.pfCallback = TaskDialogHyperlinkCallback;
    int buttonPressedId = 0;
    TaskDialogIndirect(&dialogConfig, &buttonPressedId, nullptr, nullptr);
    if (buttonPressedId == kBtnIdVisitWebsite) {
        SumatraLaunchBrowser(kExpectedDlHost);
    }
}

// Shown only for a user-initiated update check that couldn't download/parse the
// update info. Tells the user and points them at the download page so they can
// update manually (e.g. if TLS validation or the network failed).
static void NotifyUpdateCheckFailed(HWND hwndParent, DWORD err) {
    logf("NotifyUpdateCheckFailed: err=%#x\n", (unsigned)err);
    auto title = Tr("Swiftleaf Update");
    auto mainInstr = Tr("Couldn't check for updates");
    TempStr msg = fmt(Tr("Couldn't download update information (error %#x).").s, err);
    TempStr content = fmt(R"(%s

Visit <a href="%s">%s</a> to download the latest version.)",
                          msg, StrL(kWebisteDownloadPageURL), StrL(kWebisteDownloadPageURL));

    TASKDIALOGCONFIG dialogConfig{};
    DWORD flags =
        TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT | TDF_ENABLE_HYPERLINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    if (trans::IsCurrLangRtl()) {
        flags |= TDF_RTL_LAYOUT;
    }

    constexpr int kBtnIdVisitWebsite = 100;
    TASKDIALOG_BUTTON buttons[1];
    buttons[0].nButtonID = kBtnIdVisitWebsite;
    buttons[0].pszButtonText = CWStrTemp(Tr("Visit &Website"));

    dialogConfig.cbSize = sizeof(TASKDIALOGCONFIG);
    dialogConfig.pszWindowTitle = CWStrTemp(title);
    dialogConfig.pszMainInstruction = CWStrTemp(mainInstr);
    dialogConfig.pszContent = CWStrTemp(content);
    dialogConfig.dwFlags = (TASKDIALOG_FLAGS)flags;
    dialogConfig.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    dialogConfig.cButtons = dimof(buttons);
    dialogConfig.pButtons = buttons;
    dialogConfig.nDefaultButton = kBtnIdVisitWebsite;
    dialogConfig.pszMainIcon = TD_WARNING_ICON;
    dialogConfig.hwndParent = hwndParent;
    dialogConfig.pfCallback = TaskDialogHyperlinkCallback;
    int buttonPressedId = 0;
    TaskDialogIndirect(&dialogConfig, &buttonPressedId, nullptr, nullptr);
    if (buttonPressedId == kBtnIdVisitWebsite) {
        SumatraLaunchBrowser(StrL(kWebisteDownloadPageURL));
    }
}

static DWORD MaybeStartUpdateDownload(HWND hwndParent, HttpRsp* rsp, UpdateCheck updateCheckType) {
    // for store builds we do update check but ignore the result
#if 0
    if (gIsStoreBuild) {
        return 0;
    }
#endif

    Str url = rsp->url;

    if (rsp->error != 0) {
        logf("ShowAutoUpdateDialog: http get of '%s' failed with %d\n", url, (int)rsp->error);
        return rsp->error;
    }
    if (rsp->httpStatusCode != 200) {
        logf("ShowAutoUpdateDialog: http get of '%s' failed with code %d\n", url, (int)rsp->httpStatusCode);
        return ERROR_INTERNET_INVALID_URL;
    }

    bool isValidURL = false;
    for (auto updateInfoURL : updateInfoURLs) {
        if (str::StartsWith(url, updateInfoURL)) {
            isValidURL = true;
            break;
        }
    }
    if (!isValidURL) {
        logf("ShowAutoUpdateDialog: '%s' is not a valid url\n", url);
        return ERROR_INTERNET_INVALID_URL;
    }
    str::Builder* data = &rsp->data;
    if (0 == len(*data)) {
        logf("ShowAutoUpdateDialog: empty response from url '%s'\n", url);
        return ERROR_INTERNET_CONNECTION_ABORTED;
    }

    UpdateInfo* updateInfo = ParseUpdateInfo(ToStr(*data));
    if (!updateInfo) {
        logf("ShowAutoUpdateDialog: ParseUpdateInfo() failed. URL: '%s'\nAuto update data:\n%s\n", url, ToStr(*data));
        return ERROR_INTERNET_INCORRECT_FORMAT;
    }
    updateInfo->hwndParent = hwndParent;

    MainWindow* win = FindMainWindowByHwnd(hwndParent);
    if (!win) {
        // could be destroyed since we issued update check
        delete updateInfo;
        return 0;
    }
    HWND hwndForNotif = win->hwndCanvas;
    if (!ShouldDownloadUpdate(updateInfo)) {
        Str myVer = StrL(UPDATE_CHECK_VERA);
        logf("ShowAutoUpdateDialog: myVer >= latestVer ('%s' >= '%s')\n", myVer, updateInfo->latestVer);
        /* if automated => don't notify that there is no new version */
        if (updateCheckType == UpdateCheck::UserInitiated) {
            auto* wnd = GetNotificationForGroup(hwndForNotif, kNotifUpdateCheckInProgress);
            if (wnd) {
                NotificationUpdateMessage(wnd, Tr("You have the latest version."), 5 * 1000, true);
            }
        }
        delete updateInfo;
        return 0;
    }

    if (len(updateInfo->dlURL) == 0) {
        // currently for release builds we don't set this and redirecto to a website instead
        logf("ShowAutoUpdateDialog: didn't find download url. Auto update data:\n%s\n", ToStr(*data));
        RemoveNotificationsForGroup(win->hwndCanvas, kNotifUpdateCheckInProgress);
        NotifyUserOfUpdate(updateInfo);
        delete updateInfo;
        return 0;
    }

    if (!IsTrustedUpdateDlUrl(updateInfo->dlURL)) {
        RemoveNotificationsForGroup(win->hwndCanvas, kNotifUpdateCheckInProgress);
        NotifySuspiciousUpdate(hwndParent, updateInfo->dlURL);
        delete updateInfo;
        return 0;
    }

    // pre-release automatic check: don't download yet. Show a bottom-left
    // notification whose "Download and update" link does the download + install.
    if (updateCheckType == UpdateCheck::Automatic && gIsPreReleaseBuild) {
        RemoveNotificationsForGroup(hwndForNotif, kNotifUpdateCheckInProgress);
        delete gPendingUpdate;       // drop any update from a previous check
        gPendingUpdate = updateInfo; // take ownership (freed when installed/replaced)
        ShowUpdateAvailableNotification(win, updateInfo);
        return 0;
    }

    // download the installer to make update feel instant to the user
    logf("ShowAutoUpdateDialog: starting to download '%s'\n", updateInfo->dlURL);
    gUpdateCheckInProgress = true;

    auto* fnData = new DownloadUpdateAsyncData;
    fnData->hwndForNotif = hwndForNotif;
    fnData->updateInfo = updateInfo;
    auto fn = MkFunc0<DownloadUpdateAsyncData>(DownloadUpdateAsync, fnData);
    RunAsync(fn, StrL("DownloadUpdateAsync"));
    return 0;
}

// Shared by update check and minidump upload: v, os, 64bit, arm, lang, webview, store, simd.
void AppendClientInfoQuery(str::Builder& url) {
    url.Append(StrL("?v="));
    url.Append(StrL(UPDATE_CHECK_VERA));
    TempStr osVerTemp = GetWindowsVerTemp();
    url.Append(StrL("&os="));
    url.Append(osVerTemp);
    url.Append(StrL("&64bit="));
    url.Append(Str(IsProcess64() ? "yes" : "no"));
    url.Append(StrL("&arm="));
    url.Append(Str(IsArmBuild() ? "yes" : "no"));
    Str lang = trans::GetCurrentLangCode();
    url.Append(StrL("&lang="));
    url.Append(lang);
    TempStr webView2ver = GetWebView2VersionTemp();
    if (webView2ver) {
        url.Append(StrL("&webview="));
        url.Append(webView2ver);
    }
    if (gIsStoreBuild) {
        url.Append(StrL("&store"));
    }
    url.Append(StrL("&simd="));
    url.Append(Str(LatestSupportedSIMD().s));
}

// Swiftleaf's update file is a static release asset: nothing to send with it
static void BuildUpdateURL(str::Builder& url, Str baseURL, UpdateCheck) {
    url.Reset(baseURL);
}

struct UpdateCheckAsyncData {
    MainWindow* win = nullptr;
    UpdateCheck updateCheckType = UpdateCheck::Automatic;
    HttpRsp* rsp = nullptr;
    UpdateCheckAsyncData() = default;
    ~UpdateCheckAsyncData() { delete rsp; }
};

static void UpdateCheckFinish(UpdateCheckAsyncData* data) {
    log(StrL("UpdateCheckFinish\n"));
    gUpdateCheckInProgress = false;

    AutoDelete delData(data);

    auto updateCheckType = data->updateCheckType;
    auto* rsp = data->rsp;
    MainWindow* win = nullptr;
    if (IsMainWindowValidAndNotClosing(data->win)) {
        win = data->win;
    } else {
        if (len(gWindows) > 0) {
            win = gWindows[0];
        }
    }
    if (!win) {
        return;
    }
    HWND hwnd = win->hwndFrame;
    DWORD err = MaybeStartUpdateDownload(hwnd, rsp, updateCheckType);
    if ((err != 0) && (updateCheckType == UpdateCheck::UserInitiated)) {
        RemoveNotificationsForGroup(win->hwndCanvas, kNotifUpdateCheckInProgress);
        // a manual check that couldn't fetch update info: tell the user and point
        // them at the website so they can update manually
        NotifyUpdateCheckFailed(hwnd, err);
    }
}

static void UpdateCheckAsync(UpdateCheckAsyncData* data) {
    auto updateCheckType = data->updateCheckType;
    HttpRsp* rsp = nullptr;
    for (auto updateInfoURL : updateInfoURLs) {
        if (rsp) {
            delete rsp;
        }
        str::Builder url;
        BuildUpdateURL(url, updateInfoURL, updateCheckType);
        Str uri = ToStr(url);
        rsp = new HttpRsp;
        str::ReplaceWithCopy(&rsp->url, uri);
        bool ok = HttpGet(uri, rsp);
        logf("UpdateCheckAsync: response from '%s': error=%d, status=%d, %d bytes\n%s\n", rsp->url, (int)rsp->error,
             (int)rsp->httpStatusCode, len(rsp->data), ToStr(rsp->data));
        if (ok) {
            break;
        }
    }
    data->rsp = rsp;
    auto fn = MkFunc0<UpdateCheckAsyncData>(UpdateCheckFinish, data);
    uitask::Post(fn, "TaskUpdateCheckFinish");
}

// start auto-update check by downloading auto-update information from url
// on a background thread and processing the retrieved data on ui thread
// if autoCheck is true, this is a check *not* triggered by explicit action
// of the user and therefore will show less UI
void StartAsyncUpdateCheck(MainWindow* win, UpdateCheck updateCheckType) {
    if (!ShouldCheckForUpdate(updateCheckType)) {
        return;
    }

    logf("StartAsyncUpdateCheck: updateCheckType=%d\n", (int)updateCheckType);
    if (UpdateCheck::UserInitiated == updateCheckType) {
        NotificationCreateArgs args;
        args.hwndParent = win->hwndCanvas;
        args.msg = Tr("Checking for update...");
        args.warning = true;
        args.timeoutMs = 0;
        args.groupId = kNotifUpdateCheckInProgress;
        ShowNotification(args);
    }
    GetSystemTimeAsFileTime(&gSettings->timeOfLastUpdateCheck);
    gUpdateCheckInProgress = true;

    // data freed in UpdateCheckFinish()
    auto* data = new UpdateCheckAsyncData();
    data->win = win;
    data->updateCheckType = updateCheckType;
    auto fn = MkFunc0<UpdateCheckAsyncData>(UpdateCheckAsync, data);
    RunAsync(fn, StrL("UpdateCheckAsync"));
}

// the assumption is that this is a portable version downloaded to temp directory
// we should copy ourselves over the existing file, launch ourselves and
// tell our new copy to delete ourselves
void UpdateSelfTo(Str dstPath, int sleepMs) {
    ReportIf(len(dstPath) == 0);
    if (!file::Exists(dstPath)) {
        logf("UpdateSelfTo: failed because destination doesn't exist\n");
        return;
    }

    logf("UpdateSelfTo: '%s', sleep for %d ms\n", dstPath, sleepMs);
    // sleeping for a bit to make sure that the program that launched us
    // had time to exit so that we can overwrite it
    ::Sleep(sleepMs);

    // OverwriteAtomicRetry(dst, src): copy this process (new build) onto dstPath
    TempStr srcPath = GetSelfExePathTemp();
    bool ok = file::OverwriteAtomicRetry(dstPath, srcPath, 20, 250);
    if (!ok) {
        logf("UpdateSelfTo: failed to overwrite '%s' with '%s'\n", dstPath, srcPath);
        return;
    }
    logf("UpdateSelfTo: copied self to '%s'\n", dstPath);

    TempStr args = fmt(R"(-sleep-ms 500 -delete-file "%s")", srcPath);
    CreateProcessHelper(dstPath, args);
}

#if IS_DEBUG
#include "base/tests/UtAssert.h"

void UpdateTempFileTests() {
    Flags cli;
    ParseFlags(GetTempArena(),
               WStrL(L"Swiftleaf.exe -fast-install -update-no-elevate -update-sha256 "
                     L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
               cli);
    utassert(cli.fastInstall && cli.noUpdateElevation);
    utassert(len(cli.fileNames) == 0);

    Str fixture = StrL(
        "[Swiftleaf]\n"
        "Latest: 0.3.4\n"
        "BuiltOn: 2026-10-19\n"
        "Installer64: https://github.com/TheFunzzies/swiftleaf-pdf/releases/download/v0.3.4/Swiftleaf-install.exe\n"
        "Sha256Installer64: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
        "PortableExe64: https://github.com/TheFunzzies/swiftleaf-pdf/releases/download/v0.3.4/Swiftleaf.exe\n"
        "Sha256PortableExe64: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n");
    AutoDelete info(ParseUpdateInfo(fixture));
    utassert(info != nullptr);
    utassert(ParseUpdateInfo(StrL("[Swiftleaf]\nLatest: nope\n")) == nullptr);
    utassert(ParseUpdateInfo(StrL("[SumatraPDF]\nLatest: 0.3.4\n")) == nullptr);
    utassert(str::Eq(info->latestVer, StrL("0.3.4")));
    utassert(str::Eq(info->builtOn, StrL("2026-10-19")));
    utassert(str::Eq(path::GetBaseNameTemp(info->installer64), StrL("Swiftleaf-install.exe")));
    utassert(str::Eq(path::GetBaseNameTemp(info->portable64), StrL("Swiftleaf.exe")));
    if (IsProcess64() && !IsArmBuild()) {
        utassert(str::Eq(info->dlURL, IsDllBuild() ? info->installer64 : info->portable64));
        utassert(IsTrustedUpdateDlUrl(info->dlURL));
    }

    Str hash = StrL("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    if (IsProcess64() && !IsArmBuild()) {
        utassert(str::Eq(info->dlSha256, hash));
    }
    Str stage;
    Str payload;
    defer {
        str::Free(stage);
        str::Free(payload);
    };
    {
        UpdateInfo download;
        utassert(CreateUpdateStage(&download));
        stage = str::Dup(download.stagingDir);
        payload = str::Dup(download.installerPath);
        utassert(!LaunchVerifiedUpdate(&download));
        {
            AutoCloseHandle writer(CreateUpdatePayload(payload));
            utassert(writer.IsValid());
            DWORD written = 0;
            utassert(WriteFile(writer, "abc", 3, &written, nullptr) && written == 3);
            AutoCloseHandle duplicate(CreateUpdatePayload(payload));
            utassert(!duplicate.IsValid());
        }
        TempStr link = path::JoinTemp(stage, StrL("link.exe"));
        utassert(!CreateHardLinkW(CWStrTemp(link), CWStrTemp(payload), nullptr));
        utassert(file::WriteFile(payload, StrL("abd")));
        {
            AutoCloseHandle altered(OpenUpdatePayload(payload));
            utassert(!IsUpdateHashValid(altered, hash));
        }
        utassert(file::WriteFile(payload, StrL("abc")));
        download.payloadLock = OpenUpdatePayload(payload);
        utassert(download.payloadLock != INVALID_HANDLE_VALUE);
        utassert(!IsUpdateHashValid(download.payloadLock, StrL("bad")));
        utassert(!IsUpdateHashValid(download.payloadLock,
                                    StrL("0000000000000000000000000000000000000000000000000000000000000000")));
        utassert(IsUpdateHashValid(download.payloadLock, hash));
        utassert(IsUpdateHashValid(download.payloadLock, hash));
        {
            UpdateInfo copied;
            utassert(CreateUpdateStage(&copied));
            {
                AutoCloseHandle writer(CreateUpdatePayload(copied.installerPath));
                utassert(writer.IsValid() && CopyUpdatePayload(download.payloadLock, writer));
            }
            copied.payloadLock = OpenUpdatePayload(copied.installerPath);
            utassert(IsUpdateHashValid(copied.payloadLock, hash));
            utassert(!IsUpdateSignerValid(copied.installerPath, UpdateTrust::Publisher));
        }
        utassert(!file::WriteFile(payload, StrL("altered")));
        utassert(!file::Delete(payload));
        utassert(!MoveFileExW(CWStrTemp(payload), CWStrTemp(str::JoinTemp(payload, StrL(".bak"))), 0));
        utassert(!MoveFileExW(CWStrTemp(stage), CWStrTemp(str::JoinTemp(stage, StrL(".bak"))), 0));
        utassert(!LaunchVerifiedUpdate(&download));
        utassert(!IsUpdateSignerValid(payload, UpdateTrust::Publisher));

        DWORD size = 0;
        GetFileSecurityW(CWStrTemp(stage), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
        auto* sd = (PSECURITY_DESCRIPTOR)AllocArrayTemp<u8>(size);
        utassert(GetFileSecurityW(CWStrTemp(stage), DACL_SECURITY_INFORMATION, sd, size, &size));
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        utassert(GetSecurityDescriptorControl(sd, &control, &revision));
        utassert((control & SE_DACL_PROTECTED) != 0);
    }
    utassert(!file::Exists(payload));
    utassert(!dir::Exists(stage));

    TempStr linkedFile = GetTempFilePathTemp(StrL("upd"));
    TempStr hardLink = str::JoinTemp(linkedFile, StrL(".link"));
    defer {
        file::Delete(hardLink);
        file::Delete(linkedFile);
    };
    utassert(file::WriteFile(linkedFile, StrL("abc")));
    utassert(CreateHardLinkW(CWStrTemp(hardLink), CWStrTemp(linkedFile), nullptr));
    {
        AutoCloseHandle linked(OpenUpdatePayload(linkedFile));
        utassert(!IsUpdateHashValid(linked, hash));
    }

    utassert(IsUpdateTempFileName(StrL("sum17E3.tmp")));
    utassert(IsUpdateTempFileName(StrL("sum17E3.tmp.exe")));
    utassert(IsUpdateTempFileName(StrL("SUM17e3.TMP.EXE")));
    utassert(!IsUpdateTempFileName(StrL("sum17E3.tmp.exe.bak")));
    utassert(!IsUpdateTempFileName(StrL("SumatraPDF.exe")));
    utassert(!IsUpdateTempFileName(StrL("sum.tmp.exe")));
    utassert(!IsUpdateTempFileName(StrL("sum17E.tmp.exe")));
    utassert(!IsUpdateTempFileName(StrL("sumatra.tmp.exe")));

    TempStr dir = GetTempFilePathTemp(StrL("upd"));
    utassert(len(dir) > 0);
    utassert(file::Delete(dir));
    utassert(dir::Create(dir));
    defer {
        dir::RemoveAll(dir);
    };

    TempStr stub = path::JoinTemp(dir, StrL("sumABCD.tmp"));
    TempStr exe = path::JoinTemp(dir, StrL("sumABCD.tmp.exe"));
    TempStr fresh = path::JoinTemp(dir, StrL("sum1234.tmp.exe"));
    TempStr keep = path::JoinTemp(dir, StrL("keep.exe"));
    TempStr other = path::JoinTemp(dir, StrL("sumatra.tmp.exe"));
    TempStr busy = path::JoinTemp(dir, StrL("sumBEEF.tmp"));
    utassert(file::WriteFile(stub, StrL("")));
    utassert(file::WriteFile(exe, StrL("installer")));
    utassert(file::WriteFile(fresh, StrL("new")));
    utassert(file::WriteFile(keep, StrL("x")));
    utassert(file::WriteFile(other, StrL("x")));
    utassert(file::WriteFile(busy, StrL("busy")));

    DeleteStaleUpdateTemps(dir, exe, 0);
    utassert(!file::Exists(stub));
    utassert(file::Exists(exe));
    utassert(!file::Exists(fresh));
    utassert(file::Exists(keep));
    utassert(file::Exists(other));
    utassert(file::Exists(busy));

    utassert(file::WriteFile(stub, StrL("")));
    DeleteStaleUpdateTemps(dir, {}, 100000000);
    utassert(!file::Exists(stub));
    utassert(file::Exists(exe));
    utassert(file::Exists(keep));
    utassert(file::Exists(other));
    utassert(file::Exists(busy));
}
#endif
