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
    bool IsDocLoaded() { return true; }
    WindowTab* CurrentTab() { return &tab; }
};
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
static void ShowWarningNotification(HWND, Str msg, int) {
    fprintf(stderr, "%.*s (error %lu)\n", msg.len, msg.s, GetLastError());
}
static void ShowTemporaryNotification(HWND, Str) {}
static bool EngineHasUnsavedAnnotations(EngineBase* e) {
    return e->dirty;
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
static bool EngineMupdfMergePdfs(const Vec<PdfMergeSource>& src, const Vec<PdfMergePage>&, Str out,
                                 u32* warnings = nullptr) {
    if (warnings) *warnings = 0;
    if (failMerge || !file::Exists(src[0].path)) return false;
    return file::WriteFile(out, StrL("reorganized"));
}
static TempStr EngineMupdfMergeWarningTemp(u32) {
    return {};
}
static bool ReplaceCurrentDocumentFile(MainWindow* win, Str from, int) {
    replacements++;
    ToolbarUpdateStateForWindow(win, true); // document reload rebuilds the toolbar
    if (failReplace) return false;
    return MoveFileExW(ToWStrTemp(from).s, ToWStrTemp(win->tab.filePath).s,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}
static BOOL TestCopyFileW(LPCWSTR from, LPCWSTR to, BOOL exclusive) {
    return !failCopy && CopyFileW(from, to, exclusive);
}
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
    replacements = 0;
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
static bool Preflight(Str text) {
    EngineMupdf engine;
    PdfTextRun run;
    Vec<Annotation*> deleted;
    return TextPreflight(&engine, 1, run, text, deleted, nullptr);
}
static void TestText() {
    Check(Preflight(StrL("ASCII (text) \\ 123")), "ASCII remains editable");
    Check(Preflight(StrL("caf\xc3\xa9\nnext line")), "Latin text and line breaks remain editable");
    Check(Preflight(Str()), "empty replacement remains allowed");
    Check(Preflight(StrL("\xe4\xb8\xad\xe6\x96\x87")), "accept Chinese for glyph preflight");
    Check(Preflight(StrL("\xd0\x96")), "accept Cyrillic for glyph preflight");
    Check(Preflight(StrL("\xf0\x9f\x98\x80")), "accept supplementary scalars for glyph preflight");
    Check(!Preflight(StrL("e\xcc\x81")), "reject unsupported combining marks before mutation");
    Check(!Preflight(StrL("\xc2\x81")), "reject nonprinting control characters");
    Check(!Preflight(StrL("\xff")), "reject invalid UTF-8");
    Check(!Preflight(StrL("\xc0\xaf")), "reject overlong UTF-8");
    Check(!Preflight(StrL("\xed\xa0\x80")), "reject UTF-8 surrogate");
    Check(!Preflight(StrL("\xe2\x80\xae")), "reject bidi control before mutation");
    Check(!Preflight(StrL("\xd8\xb9")), "reject joining script before mutation");
}
int main() {
    TestUndo();
    TestText();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
