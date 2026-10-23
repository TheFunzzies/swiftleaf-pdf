/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
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
    Pixmap* icon = nullptr;      // not owned (SVG cache)
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
                    ctx.gfx->DrawPixmap(checkPixmap, {rb.x + (rb.dx - checkPixmap->width) / 2,
                                                      rb.y + (rb.dy - checkPixmap->height) / 2,
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
            if (row && row->id == layer) {
                UpdateLayerRow(p, row, layer);
                break;
            }
        }
    }
    MainWindowRerender(win);
    ev->didHandle = true;
}

static void OnAttachmentRowClick(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* target = ev->target;
    if (!target || !target->IsEnabled() || target->GetKind() != kindDocListRow) {
        return;
    }
    int attachmentNo = target->id;
    EngineBase* engine = CurrentEngine(win);
    if (!engine) {
        return;
    }
    Str name = EngineMupdfGetAttachmentName(engine, attachmentNo);
    Str data = EngineMupdfLoadAttachment(engine, attachmentNo);
    if (len(data) == 0) {
        str::Free(name);
        str::Free(data);
        return;
    }
    Str displayName = len(name) > 0 ? name : fmt("Attachment %d", attachmentNo);
    if (!OpenDocumentFromMemory(win, data, displayName)) {
        TempStr tmpDir = GetTempDirTemp();
        if (len(tmpDir) > 0) {
            TempStr fileName = path::GetBaseNameTemp(displayName);
            if (len(fileName) == 0) {
                fileName = fmt("attachment-%d", attachmentNo);
            }
            TempStr tmpPath = path::JoinTemp(tmpDir, fileName);
            if (file::WriteFile(tmpPath, data)) {
                if (!OpenFileExternally(tmpPath)) {
                    OpenPathInDefaultFileManager(tmpPath);
                }
            }
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
    p->title = nullptr;

    int count = 0;
    if (engine) {
        count = mode == DocumentPanelMode::Layers ? EngineMupdfGetLayerCount(engine)
                                                  : EngineMupdfGetAttachmentCount(engine);
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
            Str name = EngineMupdfGetAttachmentName(engine, i);
            Str label = len(name) > 0 ? name : fmt("Attachment %d", i);
            i64 size = EngineMupdfGetAttachmentSize(engine, i);
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
            row->SetTooltip(fmt(Tr("Open %s").s, label));
            row->onClick = MkFunc1(OnAttachmentRowClick, win);
            str::Free(name);
            VecAppend(p->rows, row);
            content->AddChild(row);
        }
    }

    root->AddChild(new ScrollBox(new Padding(content, Insets{DpiScale(4), DpiScale(8), DpiScale(12), DpiScale(8)})),
                   1);
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

struct FormBannerVirt {
    MainWindow* win = nullptr;
    VirtHost* host = nullptr;
    VirtText* text = nullptr;
    VirtButton* action = nullptr;
};

static void OnBannerDismiss(MainWindow* win, VirtMouseEvent* ev) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (tab) {
        tab->formBannerDismissed = true;
    }
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
    args.noActivate = true;
    args.clipSiblings = true;
    b->host = VirtHost::Create(args);
    if (!b->host) {
        delete b;
        return;
    }
    win->formBanner = b;
    win->hwndFormBanner = b->host->native;

    b->text = new VirtText(StrL(""), GetAppFont());
    // the banner is capped by the canvas width; trim rather than paint over the
    // action button on a narrow window
    b->text->ellipsis = true;
    b->action = new VirtButton(Tr("Highlight fields"), GetAppFont());
    b->action->onClick = MkFunc1(OnBannerAction, win);
    b->action->SetTooltip(Tr("Highlight empty form fields on the page"));
    auto* close = new VirtCloseButton();
    ApplyCloseButtonDpi(close, DpiGetForHwnd(win->hwndFrame));
    close->onClick = MkFunc1(OnBannerDismiss, win);

    auto* row = new HBox();
    row->alignCross = CrossAxisAlign::CrossCenter;
    row->gap = DpiScale(10);
    row->AddChild(b->text, 1);
    row->AddChild(b->action);
    row->AddChild(close);
    b->host->SetLayout(new Padding(row, DpiScaledInsets(10, 12)));

    StyleBanner(win);
}

void DestroyFormBanner(MainWindow* win) {
    FormBannerVirt* b = win ? win->formBanner : nullptr;
    if (!b) {
        return;
    }
    win->formBanner = nullptr;
    win->hwndFormBanner = nullptr;
    delete b->host;
    delete b;
}

void FormBannerUpdate(MainWindow* win) {
    FormBannerVirt* b = win ? win->formBanner : nullptr;
    if (!b || !win->hwndCanvas) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    DisplayModel* dm = tab ? tab->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine || !tab || !IsPdfDoc(tab)) {
        b->host->Show(false);
        return;
    }
    if (tab->formFieldCountEngine != engine) {
        tab->formFieldCountEngine = engine;
        tab->formFieldCount = -1;
        tab->formBannerDismissed = false;
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
    b->host->SetPos({x, y, dx, dy}, true);
    b->host->ClipToRoundedRect(DpiScale(12), {dx, dy});
    b->host->Relayout();
}

void FormBannerAfterThemeChange(MainWindow* win) {
    StyleBanner(win);
    FormBannerUpdate(win);
}
