/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */
// Focused fault tests. Generated includes contain the actual production functions.
// App/window and PDF I/O boundaries are fakes; Base and Win32 file I/O are real.
#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"

extern "C" int fz_windows_1252_from_unicode(int);
void log(Str s) {
    fprintf(stderr, "%.*s", s.len, s.s);
}
void _uploadDebugReport(Str a, Str b, bool) {
    fprintf(stderr, "Invariant: %.*s %.*s\n", a.len, a.s, b.len, b.s);
    exit(2);
}
#pragma comment(lib, "winspool.lib")

struct EngineBase {
    bool dirty = false;
    bool signedPdf = false;
};
struct EngineMupdf : EngineBase {
    void* pdfdoc = this;
    int pageCount = 1;
};
static EngineMupdf* AsEngineMupdf(EngineBase* e) {
    return (EngineMupdf*)e;
}
struct Annotation {};
struct PdfTextRun {};
struct PdfMergePage {
    int source;
    int pageNo;
    int rotate;
};
struct PdfMergeSource {
    Str path;
    Str password;
};
struct Controller {
    int CurrentPageNo() { return 1; }
};
struct WindowTab {
    Str filePath;
    EngineBase engine;
    EngineBase* GetEngine() { return &engine; }
};
struct MainWindow {
    WindowTab tab;
    Controller controller;
    Controller* ctrl = &controller;
    HWND hwndCanvas = nullptr;
    HWND hwndFrame = nullptr;
    bool IsDocLoaded() { return true; }
    WindowTab* CurrentTab() { return &tab; }
    int TabCount() { return 1; }
    WindowTab* GetTab(int) { return &tab; }
};
struct PdfLiveSnapshot {
    Str path;
    PdfLiveSnapshot() = default;
    PdfLiveSnapshot(const PdfLiveSnapshot&) = delete;
    PdfLiveSnapshot& operator=(const PdfLiveSnapshot&) = delete;
    ~PdfLiveSnapshot();
    bool Create(EngineBase*);
};
TempStr PdfTempBeside(Str target);
bool PublishPdfFile(Str target, Str source);
static Vec<MainWindow*> gWindows;
static bool gPluginMode = false;
bool OrganizeCanUndo(MainWindow*);
static bool toolbarCanUndo = false;
static void ToolbarUpdateStateForWindow(MainWindow* win, bool) {
    toolbarCanUndo = OrganizeCanUndo(win);
}
static bool CanAccessDisk() {
    return true;
}
static bool IsPdfDoc(WindowTab*) {
    return true;
}
static bool IsMainWindowValidAndNotClosing(MainWindow*) {
    return true;
}
static Str Tr(const char* s) {
    return Str(s);
}
constexpr int kNotifDefaultTimeOut = 1;
constexpr int kNotif5SecsTimeOut = 5;
constexpr int kNotifNoTimeout = 0;
static void ShowWarningNotification(HWND, Str msg, int) {
    fprintf(stderr, "%.*s (error %lu)\n", msg.len, msg.s, GetLastError());
}
static void ShowPlainWarningNotification(HWND, Str msg, int) {
    fprintf(stderr, "%.*s\n", msg.len, msg.s);
}
static void ShowTemporaryNotification(HWND, Str) {}
struct NotificationCreateArgs {
    HWND hwndParent = nullptr;
    Str msg;
    int timeoutMs = 0;
};
static void ShowNotification(const NotificationCreateArgs&) {}
// the user's answer to every OK / Cancel prompt
static bool confirmAnswer = true;
static int prompts = 0;
static bool ConfirmWarning(HWND, Str, Str) {
    prompts++;
    return confirmAnswer;
}
static bool CommitOpenEditors(MainWindow*) {
    return true;
}
static bool EngineHasUnsavedAnnotations(EngineBase* e) {
    return e->dirty;
}
static bool EngineMupdfHasSignatures(EngineBase* e) {
    return e->signedPdf;
}
static Str EngineMupdfGetPassword(EngineBase*) {
    return {};
}
static bool failSave = false, failMerge = false, failReplace = false, failCopy = false;
static int replacements = 0, failures = 0, checks = 0;
static Str liveContent = StrL("unsaved-annotations");
static bool EngineMupdfSaveCopy(EngineBase*, Str p) {
    return !failSave && file::WriteFile(p, liveContent);
}
static bool EngineMupdfMergePdfs(const Vec<PdfMergeSource>& src, const Vec<PdfMergePage>&, Str out) {
    if (failMerge || !file::Exists(src[0].path)) return false;
    return file::WriteFile(out, StrL("reorganized"));
}
static bool ReplaceCurrentDocumentFile(MainWindow* win, Str from, int) {
    replacements++;
    ToolbarUpdateStateForWindow(win, true); // document reload rebuilds the toolbar
    if (failReplace) return false;
    return PublishPdfFile(win->tab.filePath, from);
}
static BOOL TestCopyFileW(LPCWSTR from, LPCWSTR to, BOOL exclusive) {
    return !failCopy && CopyFileW(from, to, exclusive);
}
#include "publish-under-test.inc"
#define CopyFileW TestCopyFileW
#include "organize-under-test.inc"
#undef CopyFileW
#include "encoding-under-test.inc"
#include "preflight-under-test.inc"

static void Check(bool ok, const char* name) {
    checks++;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}
static void ResetUndo() {
    for (auto& step : gUndoSteps) {
        file::Delete(step.backupPath);
        str::Free(step.filePath);
        str::Free(step.backupPath);
    }
    VecReset(gUndoSteps);
    failSave = failMerge = failReplace = failCopy = false;
    confirmAnswer = true;
    replacements = prompts = 0;
}
static bool ContentsEqual(Str p, Str expected) {
    Str data = file::ReadFile(p);
    bool equal = str::Eq(data, expected);
    str::Free(data);
    return equal;
}
static bool Apply(MainWindow& w) {
    Vec<PdfMergePage> pages;
    VecAppend(pages, PdfMergePage{0, 1, 90});
    return ApplyPageList(&w, pages, nullptr, 1, {});
}
static void TestUndo() {
    TempStr root = GetTempFilePathTemp(StrL("safety"));
    file::Delete(root);
    dir::CreateAll(root);
    TempStr aDir = path::JoinTemp(root, StrL("a"));
    TempStr bDir = path::JoinTemp(root, StrL("b"));
    dir::CreateAll(aDir);
    dir::CreateAll(bDir);
    MainWindow a, b;
    a.tab.filePath = path::JoinTemp(aDir, StrL("same.pdf"));
    b.tab.filePath = path::JoinTemp(bDir, StrL("same.pdf"));
    if (!file::WriteFile(a.tab.filePath, StrL("original-A")) || !file::WriteFile(b.tab.filePath, StrL("original-B"))) {
        fprintf(stderr, "Fixture creation failed (Windows error %lu): %.*s\n", GetLastError(), root.len, root.s);
        exit(2);
    }
    Check(Apply(a) && Apply(b), "two documents can be reorganized");
    Check(toolbarCanUndo, "Undo button enables after page change");
    OrganizeUndo(&a); // remove an earlier entry, leaving B's backup alive
    Check(Apply(a), "new change after removing an earlier undo entry");
    Check(!path::IsSame(gUndoSteps[0].backupPath, gUndoSteps[1].backupPath), "undo snapshots have unique paths");
    OrganizeUndo(&b);
    Check(ContentsEqual(b.tab.filePath, StrL("original-B")), "same basename across documents restores correct bytes");
    ResetUndo();

    file::WriteFile(a.tab.filePath, StrL("on-disk"));
    a.tab.engine.dirty = true;
    Check(Apply(a), "page change includes unsaved annotations");
    OrganizeUndo(&a);
    Check(ContentsEqual(a.tab.filePath, liveContent), "undo preserves unsaved annotations");
    Check(!toolbarCanUndo, "Undo button disables after last undo");
    ResetUndo();
    a.tab.engine.dirty = false;
    Check(Apply(a), "prepare failed restore");
    failReplace = true;
    OrganizeUndo(&a);
    Check(OrganizeCanUndo(&a), "failed restore keeps undo available");
    failReplace = false;
    OrganizeUndo(&a);
    Check(ContentsEqual(a.tab.filePath, liveContent), "retry restores the saved bytes");
    ResetUndo();

    failCopy = true;
    Check(!Apply(a), "backup failure aborts page change");
    Check(replacements == 0 && len(gUndoSteps) == 0, "backup failure never replaces document");
    ResetUndo();
    a.tab.engine.dirty = true;
    failSave = true;
    Check(!Apply(a) && replacements == 0, "unsaved snapshot failure aborts page change");
    ResetUndo();
    a.tab.engine.dirty = false;
    failMerge = true;
    Check(!Apply(a) && replacements == 0 && len(gUndoSteps) == 0, "merge failure leaves document and undo alone");
    ResetUndo();
    failReplace = true;
    Check(!Apply(a) && len(gUndoSteps) == 0, "replace failure creates no undo entry");
    ResetUndo();
    file::WriteFile(a.tab.filePath, StrL("disk-before-failed-change"));
    a.tab.engine.dirty = true;
    failReplace = true;
    Check(!Apply(a) && OrganizeCanUndo(&a), "failed replacement retains unsaved snapshot for recovery");
    a.tab.engine.dirty = false; // the app reopens the on-disk document after a failed move
    failReplace = false;
    OrganizeUndo(&a);
    Check(ContentsEqual(a.tab.filePath, liveContent), "recover unsaved annotations after failed replacement");
    ResetUndo();
    file::Delete(a.tab.filePath);
    file::Delete(b.tab.filePath);
    RemoveDirectoryW(ToWStrTemp(aDir).s);
    RemoveDirectoryW(ToWStrTemp(bDir).s);
    RemoveDirectoryW(ToWStrTemp(root).s);
}
// files in dir other than `keep`: leftovers of staging or backups
static int OtherFiles(Str dir, Str keep) {
    int n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(CWStrTemp(path::JoinTemp(dir, StrL("*"))), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        TempStr name = ToUtf8Temp(fd.cFileName);
        bool dot = str::Eq(name, StrL(".")) || str::Eq(name, StrL(".."));
        if (!dot && !str::EqI(name, path::GetBaseNameTemp(keep))) n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

static TempStr NewDir(Str name) {
    TempStr dir = GetTempFilePathTemp(name);
    file::Delete(dir);
    dir::CreateAll(dir);
    return dir;
}

// E1, E7, E10 and S3: prompts before losing edits, snapshot lifetime
static void TestPagePrompts() {
    TempStr dir = NewDir(StrL("prompts"));
    MainWindow a;
    a.tab.filePath = path::JoinTemp(dir, StrL("doc.pdf"));
    file::WriteFile(a.tab.filePath, StrL("original"));
    VecAppend(gWindows, &a);

    a.tab.engine.dirty = true;
    confirmAnswer = false;
    Check(!Apply(a) && prompts == 1 && replacements == 0 && len(gUndoSteps) == 0,
          "declining to save unsaved edits leaves the document alone");
    ResetUndo();
    a.tab.engine.dirty = false;
    a.tab.engine.signedPdf = true;
    confirmAnswer = false;
    Check(!Apply(a) && prompts == 1 && replacements == 0, "declining the signature warning leaves the document alone");
    a.tab.engine.signedPdf = false;
    ResetUndo();

    Check(Apply(a) && prompts == 0, "a clean page change asks nothing");
    file::WriteFile(a.tab.filePath, StrL("saved after the page change"));
    confirmAnswer = false;
    OrganizeUndo(&a);
    Check(prompts == 1 && ContentsEqual(a.tab.filePath, StrL("saved after the page change")) && OrganizeCanUndo(&a),
          "undo asks before discarding a later save, and cancel keeps it");
    a.tab.engine.dirty = true;
    prompts = 0;
    OrganizeUndo(&a);
    Check(prompts == 1 && OrganizeCanUndo(&a), "undo asks before discarding unsaved edits");
    a.tab.engine.dirty = false;
    confirmAnswer = true;
    OrganizeUndo(&a);
    Check(ContentsEqual(a.tab.filePath, StrL("original")) && !OrganizeCanUndo(&a), "confirmed undo restores");
    ResetUndo();

    Check(Apply(a), "prepare snapshot cleanup");
    Str backup = str::DupTemp(gUndoSteps[0].backupPath);
    OrganizeForgetClosed();
    Check(len(gUndoSteps) == 1 && file::Exists(backup), "an open document keeps its undo snapshot");
    VecReset(gWindows);
    OrganizeForgetClosed();
    Check(len(gUndoSteps) == 0 && !file::Exists(backup), "closing the document deletes its undo snapshot");

    a.tab.engine.dirty = true;
    failReplace = true;
    Check(!Apply(a) && len(gUndoSteps) == 1, "prepare recovery cleanup");
    backup = str::DupTemp(gUndoSteps[0].backupPath);
    OrganizeClearUndo();
    Check(len(gUndoSteps) == 0 && file::Exists(backup), "a recovery copy shown to the user survives cleanup");
    file::Delete(backup);
    a.tab.engine.dirty = false;
    ResetUndo();

    Check(Apply(a), "prepare exit cleanup");
    backup = str::DupTemp(gUndoSteps[0].backupPath);
    OrganizeClearUndo();
    Check(!file::Exists(backup), "exit deletes undo snapshots");
    Check(OtherFiles(dir, a.tab.filePath) == 0, "page changes leave no files beside the document");
    ResetUndo();
    file::Delete(a.tab.filePath);
    RemoveDirectoryW(CWStrTemp(dir));
}

// E3: the swap happens beside the target and never loses the old content
static void TestPublish() {
    TempStr dir = NewDir(StrL("publish"));
    TempStr other = NewDir(StrL("source"));
    TempStr target = path::JoinTemp(dir, StrL("doc.pdf"));
    TempStr source = path::JoinTemp(other, StrL("new.pdf"));
    file::WriteFile(target, StrL("old content"));
    file::WriteFile(source, StrL("new content"));

    Check(PublishPdfFile(target, source) && ContentsEqual(target, StrL("new content")) && file::Exists(source),
          "publish from another folder copies, swaps and keeps the source");
    Check(OtherFiles(dir, target) == 0, "publish leaves no staging or backup file");

    TempStr beside = PdfTempBeside(target);
    file::WriteFile(beside, StrL("beside content"));
    Check(PublishPdfFile(target, beside) && ContentsEqual(target, StrL("beside content")) && !file::Exists(beside),
          "publish from the target's folder moves the file in");

    // an open handle without delete sharing makes the swap fail
    HANDLE h = CreateFileW(CWStrTemp(target), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(h != INVALID_HANDLE_VALUE, "lock the target");
    Check(!PublishPdfFile(target, source), "publish fails while the target is locked");
    CloseHandle(h);
    Check(ContentsEqual(target, StrL("beside content")), "a failed publish keeps the old content");
    Check(OtherFiles(dir, target) == 0, "a failed publish leaves no stray files");

    TempStr fresh = path::JoinTemp(dir, StrL("fresh.pdf"));
    Check(PublishPdfFile(fresh, source) && ContentsEqual(fresh, StrL("new content")), "publish creates a new file");
    file::Delete(fresh);

    EngineBase engine;
    Str snapPath;
    {
        PdfLiveSnapshot live;
        Check(!live.Create(nullptr), "no snapshot without a document");
        Check(live.Create(&engine) && ContentsEqual(live.path, liveContent), "snapshot holds the live document");
        snapPath = str::DupTemp(live.path);
    }
    Check(len(snapPath) > 0 && !file::Exists(snapPath), "snapshot file goes away with the snapshot");

    file::Delete(target);
    file::Delete(source);
    RemoveDirectoryW(CWStrTemp(dir));
    RemoveDirectoryW(CWStrTemp(other));
}

static bool Preflight(Str text) {
    EngineMupdf engine;
    PdfTextRun run;
    Vec<Annotation*> deleted;
    return TextPreflight(&engine, 1, run, text, deleted);
}
static void TestText() {
    Check(Preflight(StrL("ASCII (text) \\ 123")), "ASCII remains editable");
    Check(Preflight(StrL("caf\xc3\xa9\nnext line")), "Latin text and line breaks remain editable");
    Check(Preflight(Str()), "empty replacement remains allowed");
    Check(!Preflight(StrL("\xe4\xb8\xad\xe6\x96\x87")), "reject Chinese before page mutation");
    Check(!Preflight(StrL("\xd0\x96")), "reject Cyrillic before page mutation");
    Check(!Preflight(StrL("\xf0\x9f\x98\x80")), "reject emoji before page mutation");
    Check(!Preflight(StrL("e\xcc\x81")), "reject unsupported combining marks before mutation");
    Check(!Preflight(StrL("\xc2\x81")), "reject nonprinting control characters");
    Check(!Preflight(StrL("\xff")), "reject invalid UTF-8");
    str::Builder out;
    AppendWinAnsiPdfString(out, StrL("\xc5\x92\xc5\xa1\xc5\xb8\xe2\x82\xac"));
    Check(str::Eq(ToStrTemp(out), StrL("(\\214\\232\\237\\200)")),
          "encode OE, s-caron, Y-diaeresis and euro without substitution");
    out.Reset();
    AppendWinAnsiPdfString(out, StrL("(a)\\"));
    Check(str::Eq(ToStrTemp(out), StrL("(\\(a\\)\\\\)")), "escape PDF delimiters");
}
int main() {
    TestUndo();
    TestPagePrompts();
    TestPublish();
    TestText();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
