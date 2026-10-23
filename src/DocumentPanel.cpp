/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include <oleacc.h>
#include "base/File.h"
#include "base/Pixmap.h"
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "gui/VirtHost.h"

#include "Settings.h"
#include "AppSettings.h"
#include "base/GuessFileType.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Translations.h"
#include "Theme.h"
#include "Material.h"
#include "Ribbon.h"
#include "LucideIcons.h"
#include "DocumentPanel.h"

constexpr int kDocumentPanelDx = 280;
constexpr int kDocumentRowDy = 38;
constexpr int kFormBannerGap = 16;
// the vertical annotation tool strip lives at the canvas's left edge
// (ToolsPanel.cpp); keep the banner clear of it so it isn't painted over
constexpr int kAnnotationStripLeft = 16;
constexpr int kAnnotationStripDx = 44;
constexpr int kAnnotationStripGap = 8;

static Kind kindDocListRow = "docListRow";

// One line of the document panel: a checkbox and a name for a layer, or a
// paperclip and a name for an attachment. It paints itself so long names get
// the whole row width instead of a button's fixed label budget.
struct DocListRow : VirtCtrl {
    Str text; // owned
    PlatformFont* font = nullptr;
    Color textColor = kColorUnset;
    Pixmap* icon = nullptr;        // not owned (SVG cache)
    Pixmap* checkPixmap = nullptr; // not owned (SVG cache)
    bool showCheck = false;
    bool checked = false;
    bool isRtl = false;

    DocListRow() {
        kind = kindDocListRow;
        cursor = CursorId::Hand;
        onMouseEnter = MkMethod0<DocListRow, &DocListRow::OnHoverChanged>(this);
        onMouseLeave = MkMethod0<DocListRow, &DocListRow::OnHoverChanged>(this);
    }
    ~DocListRow() override { str::Free(text); }

    void OnHoverChanged() { Invalidate(); }

    void SetText(Str s) { str::ReplaceWithCopy(&text, s); }

    Size GetIdealSize() override { return {DpiScale(180), DpiScale(kDocumentRowDy)}; }

    void Paint(VirtPaintCtx& ctx) override {
        const M3Scheme& m3 = M3();
        Rect r = ctx.bounds;
        Color base = RibbonPanelBgColor();
        if (IsEnabled() && (HasFlag(vwfHovered) || HasFlag(vwfPressed))) {
            ctx.gfx->FillRoundedRect(r, DpiScale(8), M3StateLayer(base, m3.onSurface, kM3HoverOpacity));
        }

        int padX = DpiScale(12);
        int gap = DpiScale(10);
        int midY = r.y + r.dy / 2;
        // leading control (check box or icon) sits on the left in LTR and on
        // the right in RTL; afterLead is the edge the text starts from
        int leadX = isRtl ? r.Right() - padX : r.x + padX;
        int afterLead = leadX;
        if (showCheck) {
            int box = DpiScale(18);
            int bx = isRtl ? leadX - box : leadX;
            Rect rb{bx, midY - box / 2, box, box};
            if (checked) {
                ctx.gfx->FillRoundedRect(rb, DpiScale(4), m3.primary, m3.primary);
                if (checkPixmap) {
                    ctx.gfx->DrawPixmap(
                        checkPixmap, {rb.x + (rb.dx - checkPixmap->width) / 2, rb.y + (rb.dy - checkPixmap->height) / 2,
                                      checkPixmap->width, checkPixmap->height});
                }
            } else {
                ctx.gfx->FillRoundedRect(rb, DpiScale(4), kColorTransparent, m3.outline);
            }
            afterLead = isRtl ? bx - gap : rb.Right() + gap;
        } else if (icon) {
            int ix = isRtl ? leadX - icon->width : leadX;
            ctx.gfx->DrawPixmap(icon, {ix, midY - icon->height / 2, icon->width, icon->height});
            afterLead = isRtl ? ix - gap : ix + icon->width + gap;
        }

        int textStart = isRtl ? r.x + padX : afterLead;
        int textEnd = isRtl ? afterLead : r.Right() - padX;
        if (textStart >= textEnd) {
            return;
        }
        Color col = textColor != kColorUnset ? textColor : (checked || !showCheck ? m3.onSurface : m3.onSurfaceVariant);
        u32 textFlags = gfxTextVCenter | gfxTextSingleLine | gfxTextEllipsis;
        textFlags |= isRtl ? (gfxTextRight | gfxTextRtl) : gfxTextLeft;
        Rect rt{textStart, r.y, textEnd - textStart, r.dy};
        ctx.gfx->DrawText(text, rt, textFlags, font, col);
    }
};

struct DocumentPanelVirt {
    MainWindow* win = nullptr;
    VirtHost* host = nullptr;
    PlatformFont* font = nullptr;
    VirtText* title = nullptr;
    Vec<DocListRow*> rows;
    EngineBase* lastEngine = nullptr;
    Vec<PdfAttachment> attachments;
    Vec<Str> extractedFiles;
    DocumentPanelMode lastMode = (DocumentPanelMode)-1;
};

static DocumentPanelMode PanelMode() {
    int mode = gSettings->documentPanelMode;
    if (mode < 0 || mode > 1) {
        return DocumentPanelMode::Layers;
    }
    return (DocumentPanelMode)mode;
}

static EngineBase* CurrentEngine(MainWindow* win) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    DisplayModel* dm = tab ? tab->AsFixed() : nullptr;
    return dm ? dm->GetEngine() : nullptr;
}

int DocumentPanelDx() {
    return DpiScale(kDocumentPanelDx);
}

bool IsDocumentPanelVisible(MainWindow* win, DocumentPanelMode mode) {
    return win && win->hwndDocumentPanel && gSettings->showDocumentPanel && PanelMode() == mode &&
           HwndIsVisible(win->hwndDocumentPanel);
}

static void OnClose(MainWindow* win, VirtMouseEvent* ev) {
    gSettings->showDocumentPanel = false;
    ScheduleSaveSettings();
    ScheduleUiUpdate(win, kUiForceRelayout);
    ev->didHandle = true;
}

static void PaintBackground(DocumentPanelVirt*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, RibbonPanelBgColor());
    Rect r = ev->clientRect;
    ev->gfx->FillRect({r.x, r.y, 1, r.dy}, RibbonEdgeColor());
}

static void UpdateLayerRow(DocumentPanelVirt* p, DocListRow* row, int layer) {
    EngineBase* engine = CurrentEngine(p->win);
    if (!engine) {
        return;
    }
    bool on = EngineMupdfIsLayerEnabled(engine, layer);
    row->checked = on;
    row->SetTooltip(on ? Tr("Visible - click to hide") : Tr("Hidden - click to show"));
    row->Invalidate();
}

static void OnLayerRowClick(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* target = ev->target;
    if (!target || !target->IsEnabled() || target->GetKind() != kindDocListRow) {
        return;
    }
    int layer = target->id;
    EngineBase* engine = CurrentEngine(win);
    if (!engine) {
        return;
    }
    bool on = EngineMupdfIsLayerEnabled(engine, layer);
    if (!EngineMupdfSetLayerEnabled(engine, layer, !on)) {
        return;
    }
    DocumentPanelVirt* p = win->documentPanel;
    if (p) {
        for (DocListRow* row : p->rows) {
            if (row) {
                UpdateLayerRow(p, row, row->id);
            }
        }
    }
    MainWindowRerender(win);
    ev->didHandle = true;
}

// Prefix and restrict the basename and extension: no device names, ADS,
// separators or trailing dots/spaces. Preserve a useful safe extension.
static TempStr AttachmentFileName(Str name) {
    name = path::GetBaseNameTemp(name);
    TempStr ext = path::GetExtTemp(name);
    if (len(ext) > 0) name = Str(name.s, name.len - len(ext));
    while (len(name) > 0 && (name.s[name.len - 1] == '.' || name.s[name.len - 1] == ' ')) name.len--;
    str::Builder safe;
    safe.Append(StrL("attachment-"));
    for (int i = 0; i < std::min(len(name), 96); i++) {
        char c = name.s[i];
        bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                       c == '_' || c == '.';
        safe.AppendChar(allowed ? c : '_');
    }
    if (len(ext) > 1 && len(ext) <= 16) {
        bool valid = true;
        for (int i = 0; i < len(ext); i++) {
            char c = ext.s[i];
            bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            if (!alnum && !(i == 0 && c == '.')) valid = false;
        }
        if (valid) {
            safe.Append(ext);
        }
    }
    return ToStrTemp(safe);
}

static void DeleteExtractedFile(Str path) {
    file::Delete(path);
    RemoveDirectoryW(CWStrTemp(path::GetDirTemp(path)));
}

// CREATE_NEW is the overwrite boundary. Keep the directory open without
// FILE_SHARE_DELETE until the write finishes, so it cannot be swapped out.
Str DocumentPanelExtractAttachment(Str name, Str data) {
    GUID id;
    if (FAILED(CoCreateGuid(&id))) {
        return {};
    }
    WCHAR guid[40];
    StringFromGUID2(id, guid, dimof(guid));
    TempStr dir = path::JoinTemp(GetTempDirTemp(), fmt("SwiftLeaf-%s", ToUtf8Temp(guid)));
    if (!CreateDirectoryW(CWStrTemp(dir), nullptr)) {
        return {};
    }
    HANDLE hd = CreateFileW(CWStrTemp(dir), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (hd == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(CWStrTemp(dir));
        return {};
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(hd, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(hd);
        RemoveDirectoryW(CWStrTemp(dir));
        return {};
    }
    TempStr dest = path::JoinTemp(dir, AttachmentFileName(name));
    HANDLE hf = INVALID_HANDLE_VALUE;
    if (path::IsInDir(dest, dir)) {
        hf = CreateFileW(CWStrTemp(dest), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    bool ok = hf != INVALID_HANDLE_VALUE && file::WriteAll(hf, data);
    if (hf != INVALID_HANDLE_VALUE) {
        CloseHandle(hf);
    }
    CloseHandle(hd);
    if (!ok) {
        if (hf != INVALID_HANDLE_VALUE) file::Delete(dest);
        RemoveDirectoryW(CWStrTemp(dir));
        return {};
    }
    return str::Dup(dest);
}

void DocumentPanelTrackExtractedFile(MainWindow* win, Str path) {
    if (win && win->documentPanel && path) VecAppend(win->documentPanel->extractedFiles, str::Dup(path));
}

static void OnAttachmentRowClick(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* target = ev->target;
    DocumentPanelVirt* p = win->documentPanel;
    if (!p || !target || !target->IsEnabled() || target->GetKind() != kindDocListRow) {
        return;
    }
    int no = target->id;
    EngineBase* engine = CurrentEngine(win);
    if (!engine || no < 1 || no > len(p->attachments)) {
        return;
    }
    // Opening in-app can rebuild the panel, so own the name before that call.
    Str name = str::Dup(p->attachments[no - 1].name);
    Str data = EngineMupdfLoadAttachment(engine, no);
    if (len(data) == 0) {
        str::Free(name);
        str::Free(data);
        return;
    }
    Str display = len(name) > 0 ? name : fmt("Attachment %d", no);
    if (!OpenDocumentFromMemory(win, data, display)) {
        Str dest = DocumentPanelExtractAttachment(display, data);
        if (dest) {
            DocumentPanelTrackExtractedFile(win, dest);
            // Embedded data must never be executed by its file association.
            OpenPathInDefaultFileManager(dest);
        }
    }
    str::Free(name);
    str::Free(data);
    ev->didHandle = true;
}

static void AddEmptyMessage(DocumentPanelVirt* p, VBox* content, Str msg) {
    auto* text = new VirtText(msg, p->font);
    text->SetColor(kColText, M3().onSurfaceVariant);
    content->AddChild(new Padding(text, DpiScaledInsets(8, 4)));
}

static void BuildPanel(DocumentPanelVirt* p) {
    MainWindow* win = p->win;
    EngineBase* engine = CurrentEngine(win);
    DocumentPanelMode mode = PanelMode();
    VecReset(p->rows);
    for (PdfAttachment& attachment : p->attachments) {
        str::Free(attachment.name);
    }
    VecClear(p->attachments);
    if (engine && mode == DocumentPanelMode::Attachments) {
        EngineMupdfGetAttachments(engine, p->attachments);
    }
    p->title = nullptr;

    int count = 0;
    if (engine) {
        count = mode == DocumentPanelMode::Layers ? EngineMupdfGetLayerCount(engine) : len(p->attachments);
    }
    Str title = mode == DocumentPanelMode::Layers ? Tr("Layers") : Tr("Attachments");

    auto* root = new VBox();
    root->alignCross = CrossAxisAlign::Stretch;
    auto header = NewLabelWithClose(win->hwndFrame, GetAppFont(), MkFunc1(OnClose, win));
    p->title = header.label;
    p->title->SetText(engine ? fmt("%s (%d)", title, count) : title);
    p->title->SetColor(kColText, ThemeWindowTextColor());
    root->AddChild(new Padding(header.box, Insets{DpiScale(10), DpiScale(12), DpiScale(2), DpiScale(12)}));

    auto* content = new VBox();
    content->alignCross = CrossAxisAlign::Stretch;
    content->gap = DpiScale(2);
    if (!engine) {
        AddEmptyMessage(p, content, Tr("Open a PDF document to see this panel."));
    } else if (mode == DocumentPanelMode::Layers) {
        if (count == 0) {
            AddEmptyMessage(p, content, Tr("This document has no layers."));
        }
        for (int i = 0; i < count; i++) {
            Str name = EngineMupdfGetLayerName(engine, i);
            auto* row = new DocListRow();
            row->font = p->font;
            row->showCheck = true;
            row->checked = EngineMupdfIsLayerEnabled(engine, i);
            row->checkPixmap = RibbonIconPixmap(Str(gLucideCheck), DpiScale(14), M3().onPrimary, kColorUnset);
            row->textColor = ThemeWindowTextColor();
            row->isRtl = IsUIRtl();
            row->id = i;
            row->SetText(len(name) > 0 ? name : fmt("Layer %d", i + 1));
            row->SetTooltip(row->checked ? Tr("Visible - click to hide") : Tr("Hidden - click to show"));
            row->onClick = MkFunc1(OnLayerRowClick, win);
            str::Free(name);
            VecAppend(p->rows, row);
            content->AddChild(row);
        }
    } else {
        if (count == 0) {
            AddEmptyMessage(p, content, Tr("This document has no attachments."));
        }
        Pixmap* clip = RibbonIconPixmap(Str(gLucidePaperclip), DpiScale(16), M3().onSurfaceVariant, kColorUnset);
        for (int i = 1; i <= count; i++) {
            Str name = p->attachments[i - 1].name;
            Str label = len(name) > 0 ? name : fmt("Attachment %d", i);
            i64 size = p->attachments[i - 1].size;
            auto* row = new DocListRow();
            row->font = p->font;
            row->icon = clip;
            row->textColor = ThemeWindowTextColor();
            row->isRtl = IsUIRtl();
            row->id = i;
            if (size >= 0) {
                row->SetText(fmt("%s  (%s)", label, FormatFileSizeTemp(size)));
            } else {
                row->SetText(label);
            }
            row->SetTooltip(fmt(Tr("Open in SwiftLeaf or reveal extracted file: %s").s, label));
            row->onClick = MkFunc1(OnAttachmentRowClick, win);
            VecAppend(p->rows, row);
            content->AddChild(row);
        }
    }

    root->AddChild(new ScrollBox(new Padding(content, Insets{DpiScale(4), DpiScale(8), DpiScale(12), DpiScale(8)})), 1);
    p->host->SetLayout(root);
}

void CreateDocumentPanel(MainWindow* win) {
    if (win->documentPanel) {
        return;
    }
    auto* p = new DocumentPanelVirt();
    p->win = win;
    p->font = GetAppFont();

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = WStrL(L"SWIFTLEAF_DOCUMENT_PANEL");
    args.initialSize = {DocumentPanelDx(), 600};
    args.bgColor = RibbonPanelBgColor();
    args.visible = false;
    args.noActivate = true;
    p->host = VirtHost::Create(args);
    if (!p->host) {
        delete p;
        return;
    }
    win->documentPanel = p;
    win->hwndDocumentPanel = p->host->native;
    p->host->onPaintBackground = MkFunc1(PaintBackground, p);

    BuildPanel(p);
    p->lastEngine = CurrentEngine(win);
    p->lastMode = PanelMode();
}

void DestroyDocumentPanel(MainWindow* win) {
    DocumentPanelVirt* p = win ? win->documentPanel : nullptr;
    if (!p) {
        return;
    }
    win->documentPanel = nullptr;
    win->hwndDocumentPanel = nullptr;
    for (PdfAttachment& attachment : p->attachments) {
        str::Free(attachment.name);
    }
    for (Str path : p->extractedFiles) {
        DeleteExtractedFile(path);
        str::Free(path);
    }
    delete p->host;
    delete p;
}

void DocumentPanelUpdate(MainWindow* win) {
    DocumentPanelVirt* p = win ? win->documentPanel : nullptr;
    if (!p) {
        return;
    }
    EngineBase* engine = CurrentEngine(win);
    DocumentPanelMode mode = PanelMode();
    if (p->host->layout && p->lastEngine == engine && p->lastMode == mode) {
        return;
    }
    p->lastEngine = engine;
    p->lastMode = mode;
    BuildPanel(p);
}

// Called before replacing or destroying a tab's controller, including reload.
void DocumentPanelForgetTab(WindowTab* tab) {
    tab->formFieldCount = -1;
    tab->formBannerDismissed = false;
    DocumentPanelVirt* p = tab->win ? tab->win->documentPanel : nullptr;
    if (p) {
        p->lastEngine = nullptr;
        p->lastMode = (DocumentPanelMode)-1;
    }
}

void DocumentPanelAfterThemeChange(MainWindow* win) {
    DocumentPanelVirt* p = win ? win->documentPanel : nullptr;
    if (!p) {
        return;
    }
    p->host->bgColor = RibbonPanelBgColor();
    p->lastMode = (DocumentPanelMode)-1;
    p->lastEngine = nullptr;
    DocumentPanelUpdate(win);
    p->host->Invalidate(true);
}

void ToggleDocumentPanel(MainWindow* win, DocumentPanelMode mode) {
    if (!win) {
        return;
    }
    bool same = gSettings->showDocumentPanel && PanelMode() == mode;
    if (same) {
        gSettings->showDocumentPanel = false;
    } else {
        gSettings->showDocumentPanel = true;
        gSettings->documentPanelMode = (int)mode;
    }
    ScheduleSaveSettings();
    ScheduleUiUpdate(win, kUiForceRelayout);
}

//--- form-field banner

struct BannerCloseButton : VirtCloseButton {
    void Paint(VirtPaintCtx& ctx) override {
        VirtCloseButton::Paint(ctx);
        if (HasFlag(vwfFocused)) {
            ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(4), kColorTransparent, M3().onSurface);
        }
    }
};

struct BannerAccessible;
struct FormBannerVirt {
    MainWindow* win = nullptr;
    VirtHost* host = nullptr;
    VirtText* text = nullptr;
    VirtButton* action = nullptr;
    VirtCloseButton* close = nullptr;
    BannerAccessible* accessible = nullptr;
};

// MSAA simple children keep the virtual banner readable without introducing
// native controls that bypass the app theme. Outstanding COM references are
// disconnected before the host is destroyed.
struct BannerAccessible : IAccessible {
    LONG refs = 1;
    FormBannerVirt* banner;
    explicit BannerAccessible(FormBannerVirt* b) : banner(b) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDispatch) && iid != __uuidof(IAccessible))
            return E_NOINTERFACE;
        *out = static_cast<IAccessible*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = InterlockedDecrement(&refs);
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* n) override {
        *n = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override {
        return E_NOTIMPL;
    }
    int Child(VARIANT v) { return banner && v.vt == VT_I4 && v.lVal >= 0 && v.lVal <= 3 ? v.lVal : -1; }
    VirtCtrl* Control(int id) {
        if (!banner) return nullptr;
        if (id == 1) return banner->text;
        if (id == 2) return banner->action;
        if (id == 3) return banner->close;
        return nullptr;
    }
    HRESULT Text(Str s, BSTR* out) {
        if (!out) return E_POINTER;
        *out = SysAllocString(CWStrTemp(s));
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!banner || !banner->host || !banner->host->native) return S_FALSE;
        HWND parent = GetParent(banner->host->native);
        if (!parent) return S_FALSE;
        using CreateFn = HRESULT(WINAPI*)(HWND, LONG, REFIID, void**);
        static HMODULE dll = LoadLibraryExW(L"oleacc.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        static auto create = dll ? (CreateFn)GetProcAddress(dll, "CreateStdAccessibleObject") : nullptr;
        return create ? create(parent, OBJID_WINDOW, IID_IDispatch, (void**)out) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* out) override {
        *out = banner ? 3 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT, IDispatch** out) override {
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT v, BSTR* out) override {
        int id = Child(v);
        if (id < 0) return E_INVALIDARG;
        if (id == 0 || id == 1) return Text(banner->text->s, out);
        return Text(id == 2 ? banner->action->s : Tr("Dismiss form-field banner"), out);
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT, BSTR* out) override {
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT, BSTR* out) override {
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT v, VARIANT* out) override {
        int id = Child(v);
        if (id < 0) return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = id == 0 ? ROLE_SYSTEM_GROUPING : id == 1 ? ROLE_SYSTEM_STATICTEXT : ROLE_SYSTEM_PUSHBUTTON;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT v, VARIANT* out) override {
        int id = Child(v);
        if (id < 0) return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = id >= 2 ? STATE_SYSTEM_FOCUSABLE : 0;
        if (!banner->host->IsVisible()) out->lVal |= STATE_SYSTEM_INVISIBLE;
        VirtCtrl* ctrl = Control(id);
        if (ctrl && ctrl->HasFlag(vwfFocused) && banner->host->HasFocus()) out->lVal |= STATE_SYSTEM_FOCUSED;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR* out) override {
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* out, VARIANT, long* topic) override {
        *out = nullptr;
        *topic = -1;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT, BSTR* out) override {
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* out) override {
        VariantInit(out);
        if (!banner || !banner->host->HasFocus()) return S_FALSE;
        out->vt = VT_I4;
        out->lVal = banner->action->HasFlag(vwfFocused) ? 2 : banner->close->HasFlag(vwfFocused) ? 3 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* out) override {
        VariantInit(out);
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT v, BSTR* out) override {
        if (Child(v) < 2) {
            *out = nullptr;
            return S_FALSE;
        }
        return Text(Tr("Press"), out);
    }
    HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT v) override {
        int id = Child(v);
        if (id < 2 || !(flags & SELFLAG_TAKEFOCUS) || !banner->host->IsVisible()) return E_INVALIDARG;
        HwndSetFocus(banner->host->native);
        banner->host->vroot->SetFocus(Control(id));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accLocation(long* x, long* y, long* dx, long* dy, VARIANT v) override {
        int id = Child(v);
        if (id < 0) return E_INVALIDARG;
        Rect r = id ? banner->host->ToScreen(Control(id)->BoundsInWindow()) : banner->host->ScreenRect();
        *x = r.x;
        *y = r.y;
        *dx = r.dx;
        *dy = r.dy;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long direction, VARIANT from, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        int id = Child(from);
        if (id < 0) return E_INVALIDARG;
        int next = -1;
        switch (direction) {
            case NAVDIR_FIRSTCHILD:
                if (id == 0) next = 1;
                break;
            case NAVDIR_LASTCHILD:
                if (id == 0) next = 3;
                break;
            case NAVDIR_NEXT:
                if (id > 0 && id < 3) next = id + 1;
                break;
            case NAVDIR_PREVIOUS:
                if (id > 1 && id <= 3) next = id - 1;
                break;
        }
        if (next < 0) return S_FALSE;
        out->vt = VT_I4;
        out->lVal = next;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* out) override {
        VariantInit(out);
        if (!banner || !banner->host->IsVisible() || !banner->host->ScreenRect().Contains({(int)x, (int)y}))
            return S_FALSE;
        out->vt = VT_I4;
        out->lVal = 0;
        for (int i = 1; i <= 3; i++) {
            if (banner->host->ToScreen(Control(i)->BoundsInWindow()).Contains({(int)x, (int)y})) out->lVal = i;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT v) override {
        int id = Child(v);
        if (id < 2 || !banner->host->IsVisible()) return E_INVALIDARG;
        if (id == 2)
            banner->action->Click();
        else {
            VirtMouseEvent ev;
            ev.target = banner->close;
            banner->close->onClick.Call(&ev);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
};

static void BannerFocus(FormBannerVirt* b, VirtFocusEvent* ev) {
    if (ev->gotFocus) {
        NotifyWinEvent(EVENT_OBJECT_FOCUS, b->host->native, OBJID_CLIENT, ev->w == b->action ? 2 : 3);
    }
}

static void BannerNativeMsg(FormBannerVirt* b, VirtHostNativeMsg* ev) {
    if (ev->msg == WM_GETOBJECT && (LONG)ev->lp == OBJID_CLIENT) {
        using ResultFn = LRESULT(WINAPI*)(REFIID, WPARAM, LPUNKNOWN);
        static HMODULE dll = LoadLibraryExW(L"oleacc.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        static auto result = dll ? (ResultFn)GetProcAddress(dll, "LresultFromObject") : nullptr;
        if (result) {
            ev->res = result(__uuidof(IAccessible), ev->wp, b->accessible);
            ev->didHandle = true;
        }
    } else if (ev->msg == WM_SETFOCUS && b->host->vroot && !b->host->vroot->focused) {
        b->host->vroot->SetFocus(b->action);
    } else if (ev->msg == WM_KEYDOWN && ev->wp == VK_ESCAPE) {
        HwndSetFocus(b->win->hwndFrame);
        ev->didHandle = true;
    }
}

static void OnBannerDismiss(MainWindow* win, VirtMouseEvent* ev) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (tab) {
        tab->formBannerDismissed = true;
    }
    HwndSetFocus(win->hwndFrame);
    FormBannerUpdate(win);
    ev->didHandle = true;
}

static void OnBannerAction(MainWindow* win, VirtMouseEvent* ev) {
    gSettings->highlightFormFields = !gSettings->highlightFormFields;
    ScheduleSaveSettings();
    // the highlight is painted over the rendered page, so repainting the
    // window picks it up without re-rendering the document
    win->RedrawAll(true);
    FormBannerUpdate(win);
    ev->didHandle = true;
}

static void BannerCloseKey(MainWindow* win, VirtKeyEvent* ev) {
    if (ev->vkey == VK_RETURN || ev->vkey == VK_SPACE) {
        VirtMouseEvent click;
        OnBannerDismiss(win, &click);
        ev->didHandle = true;
    }
}

static void StyleBanner(MainWindow* win) {
    FormBannerVirt* b = win ? win->formBanner : nullptr;
    if (!b) {
        return;
    }
    const M3Scheme& m3 = M3();
    b->host->bgColor = m3.surfaceContainerHigh;
    b->text->SetColor(kColText, ThemeWindowTextColor());
    b->action->SetColor(kColBtnText, RibbonAccentColor());
    b->action->SetColor(kColBtnBg, kColorTransparent);
    b->action->SetColor(kColBtnBorder, kColorTransparent);
    b->action->SetColor(kColBtnBgHover, m3.surfaceContainerHighest);
    b->host->Invalidate(true);
}

static void PaintBanner(FormBannerVirt*, VirtHostPaintEvent* ev) {
    const M3Scheme& m3 = M3();
    Rect r = ev->clientRect;
    ev->gfx->FillRoundedRect(r, DpiScale(12), m3.surfaceContainerHigh, m3.outline);
}

void CreateFormBanner(MainWindow* win) {
    if (!win || win->formBanner || !win->hwndCanvas) {
        return;
    }
    auto* b = new FormBannerVirt();
    b->win = win;

    VirtHost::CreateArgs args;
    args.parent = win->hwndCanvas;
    args.className = WStrL(L"SWIFTLEAF_FORM_BANNER");
    args.initialSize = {DpiScale(560), DpiScale(56)};
    args.bgColor = M3().surfaceContainerHigh;
    args.visible = false;
    args.noActivate = false;
    args.clipSiblings = true;
    b->host = VirtHost::Create(args);
    if (!b->host) {
        delete b;
        return;
    }
    win->formBanner = b;
    win->hwndFormBanner = b->host->native;
    b->host->onPaintBackground = MkFunc1(PaintBanner, b);

    b->text = new VirtText(StrL(""), GetAppFont());
    // the banner is capped by the canvas width; trim rather than paint over the
    // action button on a narrow window
    b->text->ellipsis = true;
    b->action = new VirtButton(Tr("Highlight fields"), GetAppFont());
    b->action->onClick = MkFunc1(OnBannerAction, win);
    b->action->SetTooltip(Tr("Highlight empty form fields on the page"));
    auto* close = new BannerCloseButton();
    close->SetFlag(vwfFocusable, true);
    close->onKeyDown = MkFunc1(BannerCloseKey, win);
    b->close = close;
    close->SetTooltip(Tr("Dismiss form-field banner"));
    b->action->onFocusChanged = MkFunc1(BannerFocus, b);
    close->onFocusChanged = MkFunc1(BannerFocus, b);
    ApplyCloseButtonDpi(close, DpiGetForHwnd(win->hwndFrame));
    close->onClick = MkFunc1(OnBannerDismiss, win);

    auto* row = new HBox();
    row->alignCross = CrossAxisAlign::CrossCenter;
    row->gap = DpiScale(10);
    row->AddChild(b->text, 1);
    row->AddChild(b->action);
    row->AddChild(close);
    b->host->SetLayout(new Padding(row, DpiScaledInsets(10, 12)));

    b->accessible = new BannerAccessible(b);
    b->host->onNativeMsg = MkFunc1(BannerNativeMsg, b);
    StyleBanner(win);
}

void DestroyFormBanner(MainWindow* win) {
    FormBannerVirt* b = win ? win->formBanner : nullptr;
    if (!b) {
        return;
    }
    win->formBanner = nullptr;
    win->hwndFormBanner = nullptr;
    b->accessible->banner = nullptr;
    b->accessible->Release();
    delete b->host;
    delete b;
}

void FormBannerUpdate(MainWindow* win) {
    FormBannerVirt* b = win ? win->formBanner : nullptr;
    if (!b || !win->hwndCanvas) {
        return;
    }
    VirtCtrl* focused = b->host->HasFocus() && b->host->vroot ? b->host->vroot->focused : nullptr;
    WindowTab* tab = win->CurrentTab();
    DisplayModel* dm = tab ? tab->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine || !tab || !IsPdfDoc(tab)) {
        if (b->host->HasFocus()) HwndSetFocus(win->hwndFrame);
        b->host->Show(false);
        return;
    }
    if (tab->formFieldCount < 0) {
        tab->formFieldCount = EngineMupdfCountFormFields(engine);
    }
    bool show = tab->formFieldCount > 0 && !tab->formBannerDismissed && !win->presentation && !win->isFullScreen &&
                !win->isQuickLook;
    Rect cr = HwndClientRect(win->hwndCanvas);
    int leftInset = DpiScale(kAnnotationStripLeft + kAnnotationStripDx + kAnnotationStripGap);
    int rightInset = DpiScale(kFormBannerGap);
    int avail = cr.dx - leftInset - rightInset;
    int dx = std::min(DpiScale(560), avail);
    if (!show || dx < DpiScale(300) || cr.dy < DpiScale(100)) {
        if (b->host->HasFocus()) HwndSetFocus(win->hwndFrame);
        b->host->Show(false);
        return;
    }
    int dy = DpiScale(56);
    int x = leftInset + (avail - dx) / 2;
    int y = DpiScale(kFormBannerGap);
    if (tab->formFieldCount == 1) {
        b->text->SetText(Tr("1 interactive field"));
    } else {
        b->text->SetText(fmt(Tr("%d interactive fields").s, tab->formFieldCount));
    }
    b->action->SetText(gSettings->highlightFormFields ? Tr("Hide highlights") : Tr("Highlight fields"));
    b->action->SetTooltip(gSettings->highlightFormFields ? Tr("Stop highlighting empty form fields")
                                                         : Tr("Highlight empty form fields on the page"));
    bool wasVisible = b->host->IsVisible();
    b->host->SetPos({x, y, dx, dy}, true);
    b->host->ClipToRoundedRect(DpiScale(12), {dx, dy});
    b->host->Relayout();
    // Relayout rebuilds VirtRoot's top list and clears its focus pointer.
    // Keep keyboard position when activating the highlight button changes text.
    if (focused && b->host->vroot) {
        b->host->vroot->SetFocus(focused);
    }
    if (!wasVisible) NotifyWinEvent(EVENT_OBJECT_SHOW, b->host->native, OBJID_CLIENT, CHILDID_SELF);
}

void FormBannerAfterThemeChange(MainWindow* win) {
    StyleBanner(win);
    FormBannerUpdate(win);
}

#if IS_DEBUG
#include "base/tests/UtAssert.h"
void DocumentPanel_UnitTests() {
    const Str names[] = {
        StrL(".."),  StrL("."),       StrL("../../outside.html"), StrL("C:\\outside.exe"), StrL("file:stream"),
        StrL("CON"), StrL("NUL.txt"), StrL("../x.html"),          StrL("trailing. "),      StrL("")};
    for (Str name : names) {
        Str first = DocumentPanelExtractAttachment(name, StrL("first"));
        Str second = DocumentPanelExtractAttachment(name, StrL("second"));
        utassert(first && second);
        if (first && second) {
            utassert(!path::IsSame(first, second));
            utassert(!path::IsSame(path::GetDirTemp(first), path::GetDirTemp(second)));
            utassert(path::IsInDir(first, GetTempDirTemp()));
            if (str::EndsWithI(name, StrL(".html"))) utassert(str::EndsWithI(first, StrL(".html")));
            utassert(!str::EndsWith(first, StrL(".")) && !str::EndsWith(first, StrL(" ")));
            Str data = file::ReadFile(first);
            utassert(str::Eq(data, StrL("first")));
            str::Free(data);
            data = file::ReadFile(second);
            utassert(str::Eq(data, StrL("second")));
            str::Free(data);
        }
        if (first) DeleteExtractedFile(first);
        if (second) DeleteExtractedFile(second);
        utassert(len(first) == 0 || !file::Exists(first));
        utassert(len(second) == 0 || !file::Exists(second));
        str::Free(first);
        str::Free(second);
    }
    TempStr sentinel = GetTempFilePathTemp(StrL("swiftleaf-review"));
    utassert(file::WriteFile(sentinel, StrL("keep")));
    Str extracted = DocumentPanelExtractAttachment(path::GetBaseNameTemp(sentinel), StrL("replace"));
    Str original = file::ReadFile(sentinel);
    utassert(str::Eq(original, StrL("keep")));
    str::Free(original);
    if (extracted) DeleteExtractedFile(extracted);
    str::Free(extracted);
    file::Delete(sentinel);

    WindowTab tab(nullptr);
    tab.formFieldCount = 17;
    tab.formBannerDismissed = true;
    DocumentPanelForgetTab(&tab);
    utassert(tab.formFieldCount == -1 && !tab.formBannerDismissed);

    FormBannerVirt banner;
    VirtText text(StrL("17 interactive fields"));
    VirtButton action(StrL("Highlight fields"));
    BannerCloseButton close;
    banner.text = &text;
    banner.action = &action;
    banner.close = &close;
    auto* provider = new BannerAccessible(&banner);
    VARIANT child{};
    child.vt = VT_I4;
    child.lVal = 2;
    BSTR name = nullptr;
    utassert(provider->get_accName(child, &name) == S_OK);
    utassert(name && wcscmp(name, L"Highlight fields") == 0);
    SysFreeString(name);
    VARIANT role{};
    utassert(provider->get_accRole(child, &role) == S_OK && role.lVal == ROLE_SYSTEM_PUSHBUTTON);
    child.lVal = 3;
    name = nullptr;
    utassert(provider->get_accName(child, &name) == S_OK);
    utassert(name && wcscmp(name, L"Dismiss form-field banner") == 0);
    SysFreeString(name);
    VARIANT nav{};
    VARIANT root{};
    root.vt = VT_I4;
    root.lVal = 0;
    utassert(provider->accNavigate(NAVDIR_FIRSTCHILD, root, &nav) == S_OK);
    utassert(nav.vt == VT_I4 && nav.lVal == 1);
    VARIANT from{};
    from.vt = VT_I4;
    from.lVal = 2;
    VariantInit(&nav);
    utassert(provider->accNavigate(NAVDIR_NEXT, from, &nav) == S_OK && nav.lVal == 3);
    VariantInit(&nav);
    utassert(provider->accNavigate(NAVDIR_LASTCHILD, root, &nav) == S_OK);
    utassert(nav.lVal == 3);
    VariantInit(&nav);
    from.lVal = 3;
    utassert(provider->accNavigate(NAVDIR_NEXT, from, &nav) == S_FALSE);
    provider->banner = nullptr;
    utassert(provider->get_accName(child, &name) == E_INVALIDARG);
    provider->Release();
}
#endif
