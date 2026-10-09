/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"
#include "gui/Dpi.h"
#include "base/Pixmap.h"
#include "base/GdiPlusUtil.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"

#include "Settings.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "ImageReader.h"
#include "SumatraPDF.h"
#include "SumatraConfig.h"
#include "Theme.h"
#include "MainWindow.h"
#include "Commands.h"
#include "Translations.h"
#include "DarkMode.h"
#include "Material.h"
#include "Notifications.h"
#include "SignatureCreate.h"

// Fill & Sign's signature: drawn with the mouse / pen, typed in a cursive
// font, or taken from a picture. It is saved as a PNG with a transparent
// background and becomes Annotations.SignatureImage, which Place Signature
// stamps on the page.
//
//   [ Draw ] [ Type ] [ Upload ]
//   +-------------------------------------------+
//   |                pad (3:1)                  |
//   |  ________________________________________ |
//   +-------------------------------------------+
//   options of the mode (name + styles / choose image)
//   [Black] [Blue]              [Clear] [Save] [Cancel]

enum class SigMode {
    Draw = 0,
    Type,
    Upload,
};

constexpr int kModeCount = 3;
constexpr int kPadDx = 540;
constexpr int kPadDy = 180;
// the saved image; the same 3:1 shape as the pad
constexpr int kOutDx = 1200;
constexpr int kOutDy = 400;
constexpr float kOutPenWidth = 7.f;
constexpr float kOutDpi = 400.f;
constexpr int kMaxStyles = 4;
// separates strokes in SignaturePad::pts
constexpr float kStrokeBreak = -1.f;

// handwriting fonts that ship with Windows or Office, best first
static const char* gCursiveFonts[] = {
    "Segoe Script", "Lucida Handwriting", "Brush Script MT", "Ink Free",
    "Freestyle Script", "Edwardian Script ITC", "Segoe Print", "Gabriola",
};

static Color InkColor(int idx) {
    // black and a ballpoint blue
    return idx == 0 ? MkRgb(0x20, 0x20, 0x20) : MkRgb(0x1a, 0x3d, 0x9c);
}

static int CALLBACK OnFontFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) {
    *(bool*)lp = true;
    return 0;
}

static bool IsFontInstalled(const char* name) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    TempWStr ws = ToWStrTemp(Str(name));
    wcsncpy_s(lf.lfFaceName, ws.s, LF_FACESIZE - 1);
    bool found = false;
    HDC hdc = GetDC(nullptr);
    EnumFontFamiliesExW(hdc, &lf, OnFontFound, (LPARAM)&found, 0);
    ReleaseDC(nullptr, hdc);
    return found;
}

//--- the pad

static Kind kindSignaturePad = "signaturePad";

struct SignaturePad : VirtCtrl {
    SigMode mode = SigMode::Draw;
    // normalized to the pad (0..1); kStrokeBreak pairs separate strokes
    Vec<PointF> pts;
    bool drawing = false;
    Str name; // owned
    const char* fontName = nullptr;
    Pixmap* image = nullptr; // owned; white already made transparent
    int ink = 0;
    PlatformFont* hintFont = nullptr;
    // a stroke was drawn or the pad cleared: the dialog's Save follows it
    Func0 onChanged;

    SignaturePad();
    ~SignaturePad() override;
    Size GetIdealSize() override;
    void Paint(VirtPaintCtx&) override;
    bool IsEmpty() const;
    void Clear();

    PointF ToPad(VirtMouseEvent* ev);
    void OnDown(VirtMouseEvent*);
    void OnMove(VirtMouseEvent*);
    void OnUp(VirtMouseEvent*);
};

SignaturePad::SignaturePad() {
    kind = kindSignaturePad;
    flags &= ~vwfNoHitTest;
    cursor = CursorId::Cross;
    onMouseDown = MkMethod1<SignaturePad, VirtMouseEvent*, &SignaturePad::OnDown>(this);
    onMouseMove = MkMethod1<SignaturePad, VirtMouseEvent*, &SignaturePad::OnMove>(this);
    onMouseUp = MkMethod1<SignaturePad, VirtMouseEvent*, &SignaturePad::OnUp>(this);
}

SignaturePad::~SignaturePad() {
    str::Free(name);
    FreePixmap(image);
}

Size SignaturePad::GetIdealSize() {
    return {DpiScale(kPadDx), DpiScale(kPadDy)};
}

bool SignaturePad::IsEmpty() const {
    switch (mode) {
        case SigMode::Draw:
            return len(pts) == 0;
        case SigMode::Type:
            return str::IsEmptyOrWhiteSpace(name) || !fontName;
        case SigMode::Upload:
            return image == nullptr;
    }
    return true;
}

void SignaturePad::Clear() {
    switch (mode) {
        case SigMode::Draw:
            VecReset(pts);
            break;
        case SigMode::Upload:
            FreePixmap(image);
            image = nullptr;
            break;
        case SigMode::Type:
            break;
    }
    Invalidate();
}

PointF SignaturePad::ToPad(VirtMouseEvent* ev) {
    Rect r = BoundsInWindow();
    float x = (float)(ev->ptWindow.x - r.x) / (float)std::max(r.dx, 1);
    float y = (float)(ev->ptWindow.y - r.y) / (float)std::max(r.dy, 1);
    return {limitValue(x, 0.f, 1.f), limitValue(y, 0.f, 1.f)};
}

void SignaturePad::OnDown(VirtMouseEvent* ev) {
    if (mode != SigMode::Draw || ev->button != 0) {
        return;
    }
    if (len(pts) > 0) {
        VecAppend(pts, PointF{kStrokeBreak, kStrokeBreak});
    }
    VecAppend(pts, ToPad(ev));
    drawing = true;
    if (root) {
        root->SetCapture(this);
    }
    Invalidate();
    ev->didHandle = true;
}

void SignaturePad::OnMove(VirtMouseEvent* ev) {
    if (!drawing) {
        return;
    }
    VecAppend(pts, ToPad(ev));
    Invalidate();
    ev->didHandle = true;
}

void SignaturePad::OnUp(VirtMouseEvent* ev) {
    if (!drawing) {
        return;
    }
    drawing = false;
    if (root) {
        root->ReleaseCapture();
    }
    ev->didHandle = true;
    onChanged.Call();
}

// the largest font size at which the name fits the pad
static PlatformFont* FitCursiveFont(const char* fontName, Str text, Size box) {
    float pt = 40.f;
    PlatformFont* f = nullptr;
    for (; pt > 8.f; pt -= 2.f) {
        f = GetPlatformFont(Str(fontName), pt, PlatformFontStyle::Regular);
        Size sz = PlatformFontMeasureText(f, text);
        if (sz.dx <= box.dx && sz.dy <= box.dy) {
            break;
        }
    }
    return f;
}

// on white like paper in both themes: the signature goes on a page
void SignaturePad::Paint(VirtPaintCtx& ctx) {
    const M3Scheme& m3 = M3();
    Rect r = ctx.bounds;
    ctx.gfx->FillRoundedRect(r, DpiScale(24), kColWhite, m3.outlineVariant);

    int padX = DpiScale(24);
    int baseY = r.y + (r.dy * 3 / 4);
    ctx.gfx->FillRect({r.x + padX, baseY, r.dx - 2 * padX, 1}, MkRgb(0xc8, 0xc8, 0xc8));
    Color inkCol = InkColor(ink);

    if (IsEmpty()) {
        Str hint = Tr("Draw your signature here");
        if (mode == SigMode::Type) {
            hint = Tr("Type your name below");
        } else if (mode == SigMode::Upload) {
            hint = Tr("Choose a picture of your signature");
        }
        Rect rHint{r.x, r.y, r.dx, baseY - r.y};
        ctx.gfx->DrawText(hint, rHint, gfxTextCenter | gfxTextVCenter, hintFont, MkRgb(0x9a, 0x9a, 0x9a));
        return;
    }

    if (mode == SigMode::Draw) {
        float thickness = (float)DpiScale(3);
        for (int i = 1; i < len(pts); i++) {
            PointF a = pts[i - 1];
            PointF b = pts[i];
            if (a.x == kStrokeBreak || b.x == kStrokeBreak) {
                continue;
            }
            Point pa{r.x + (int)(a.x * r.dx), r.y + (int)(a.y * r.dy)};
            Point pb{r.x + (int)(b.x * r.dx), r.y + (int)(b.y * r.dy)};
            ctx.gfx->DrawLineAA(pa, pb, inkCol, thickness);
        }
        return;
    }

    if (mode == SigMode::Type) {
        Size box{r.dx - 2 * padX, r.dy - DpiScale(16)};
        PlatformFont* f = FitCursiveFont(fontName, name, box);
        ctx.gfx->DrawText(name, r, gfxTextCenter | gfxTextVCenter | gfxTextSingleLine, f, inkCol);
        return;
    }

    // Upload: fit the picture inside the margins
    Rect box{r.x + padX, r.y + DpiScale(8), r.dx - 2 * padX, r.dy - DpiScale(16)};
    float s = std::min((float)box.dx / image->width, (float)box.dy / image->height);
    int dx = (int)(image->width * s);
    int dy = (int)(image->height * s);
    ctx.gfx->DrawPixmap(image, {box.x + (box.dx - dx) / 2, box.y + (box.dy - dy) / 2, dx, dy});
}

//--- image processing

// A photo of a signature: scaled down to the output size, with the paper made
// transparent. Darker than kInkLum stays opaque, lighter than kPaperLum goes.
constexpr int kInkLum = 140;
constexpr int kPaperLum = 215;

static Pixmap* SignatureFromPhoto(Pixmap* src) {
    float s = std::min(1.f, std::min((float)kOutDx / src->width, (float)kOutDy / src->height));
    int dx = std::max(1, (int)(src->width * s));
    int dy = std::max(1, (int)(src->height * s));

    Gdiplus::Bitmap* srcBmp = NewGdiplusBitmapFromPixmap(src);
    if (!srcBmp) {
        return nullptr;
    }
    Gdiplus::Bitmap bmp(dx, dy, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.Clear(Gdiplus::Color(255, 255, 255, 255));
        g.DrawImage(srcBmp, 0, 0, dx, dy);
    }
    delete srcBmp;

    Gdiplus::Rect rc(0, 0, dx, dy);
    Gdiplus::BitmapData bd;
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd) !=
        Gdiplus::Ok) {
        return nullptr;
    }
    for (int y = 0; y < dy; y++) {
        u8* row = (u8*)bd.Scan0 + ((size_t)y * bd.Stride);
        for (int x = 0; x < dx; x++) {
            u8* p = row + (x * 4); // B G R A
            int lum = (p[2] * 30 + p[1] * 59 + p[0] * 11) / 100;
            int a = 255;
            if (lum >= kPaperLum) {
                a = 0;
            } else if (lum > kInkLum) {
                a = (kPaperLum - lum) * 255 / (kPaperLum - kInkLum);
            }
            p[3] = (u8)a;
        }
    }
    bmp.UnlockBits(&bd);
    Pixmap* res = PixmapFromGdiplus(&bmp);
    if (res) {
        res->hasAlpha = true;
    }
    return res;
}

// draws the signature on a transparent kOutDx x kOutDy bitmap
static void RenderSignature(SignaturePad* pad, Gdiplus::Bitmap* bmp) {
    Gdiplus::Graphics g(bmp);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Color c = InkColor(pad->ink);
    u8 cr, cg, cb;
    UnpackColor(c, cr, cg, cb);
    Gdiplus::Color ink(255, cr, cg, cb);

    if (pad->mode == SigMode::Draw) {
        Gdiplus::Pen pen(ink, kOutPenWidth);
        pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        Vec<Gdiplus::PointF> stroke;
        auto flush = [&]() {
            int n = len(stroke);
            if (n == 1) {
                float d = kOutPenWidth;
                Gdiplus::SolidBrush br(ink);
                g.FillEllipse(&br, stroke[0].X - d / 2, stroke[0].Y - d / 2, d, d);
            } else if (n > 1) {
                // a light curve smooths the mouse's straight segments
                g.DrawCurve(&pen, stroke.els, n, 0.3f);
            }
            VecReset(stroke);
        };
        for (PointF p : pad->pts) {
            if (p.x == kStrokeBreak) {
                flush();
                continue;
            }
            VecAppend(stroke, Gdiplus::PointF(p.x * kOutDx, p.y * kOutDy));
        }
        flush();
        return;
    }

    if (pad->mode == SigMode::Type) {
        TempWStr text = ToWStrTemp(pad->name);
        TempWStr family = ToWStrTemp(Str(pad->fontName));
        Gdiplus::FontFamily fam(family.s);
        float em = (float)kOutDy * 0.55f;
        Gdiplus::RectF bounds;
        for (; em > 20.f; em -= 8.f) {
            Gdiplus::Font f(&fam, em, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            g.MeasureString(text.s, len(text), &f, Gdiplus::PointF(0, 0), &bounds);
            if (bounds.Width <= kOutDx * 0.92f && bounds.Height <= kOutDy * 0.9f) {
                break;
            }
        }
        Gdiplus::Font f(&fam, em, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        g.MeasureString(text.s, len(text), &f, Gdiplus::PointF(0, 0), &bounds);
        Gdiplus::SolidBrush br(ink);
        float x = (kOutDx - bounds.Width) / 2;
        float y = (kOutDy - bounds.Height) / 2;
        g.DrawString(text.s, len(text), &f, Gdiplus::PointF(x, y), &br);
        return;
    }

    Gdiplus::Bitmap* img = NewGdiplusBitmapFromPixmap(pad->image);
    if (!img) {
        return;
    }
    float s = std::min((float)kOutDx / pad->image->width, (float)kOutDy / pad->image->height);
    float dx = pad->image->width * s;
    float dy = pad->image->height * s;
    g.DrawImage(img, Gdiplus::RectF((kOutDx - dx) / 2, (kOutDy - dy) / 2, dx, dy));
    delete img;
}

// the part of the bitmap that has ink, plus a small margin
static Gdiplus::Rect InkBounds(Gdiplus::Bitmap* bmp) {
    int w = (int)bmp->GetWidth();
    int h = (int)bmp->GetHeight();
    Gdiplus::Rect rc(0, 0, w, h);
    Gdiplus::BitmapData bd;
    if (bmp->LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
        return rc;
    }
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) {
        u8* row = (u8*)bd.Scan0 + ((size_t)y * bd.Stride);
        for (int x = 0; x < w; x++) {
            if (row[x * 4 + 3] < 16) {
                continue;
            }
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    }
    bmp->UnlockBits(&bd);
    if (x1 < 0) {
        return rc;
    }
    constexpr int kMargin = 10;
    x0 = std::max(0, x0 - kMargin);
    y0 = std::max(0, y0 - kMargin);
    x1 = std::min(w - 1, x1 + kMargin);
    y1 = std::min(h - 1, y1 + kMargin);
    return Gdiplus::Rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

static bool SaveSignaturePng(SignaturePad* pad, Str path) {
    Gdiplus::Bitmap bmp(kOutDx, kOutDy, PixelFormat32bppARGB);
    RenderSignature(pad, &bmp);
    Gdiplus::Rect ink = InkBounds(&bmp);
    Gdiplus::Bitmap* cropped = bmp.Clone(ink, PixelFormat32bppARGB);
    if (!cropped) {
        return false;
    }
    // placed in its natural size, this makes a full-width signature ~3"
    cropped->SetResolution(kOutDpi, kOutDpi);
    CLSID png = GetGdiPlusEncoderClsid(WStrL(L"image/png"));
    Gdiplus::Status st = cropped->Save(ToWStrTemp(path).s, &png, nullptr);
    delete cropped;
    return st == Gdiplus::Ok;
}

//--- the dialog

struct SignatureDialog : WindowBase {
    MainWindow* win = nullptr;
    SignaturePad* pad = nullptr;
    VirtButton* modeBtns[kModeCount]{};
    VirtButton* inkBtns[2]{};
    VirtButton* styleBtns[kMaxStyles]{};
    const char* styleFonts[kMaxStyles]{};
    int nStyles = 0;
    int style = 0;
    Edit* nameEdit = nullptr;
    ILayout* modeRows[kModeCount]{};
    VirtButton* saveBtn = nullptr;
    int clientDx = 0;

    bool Create(MainWindow*);
    void SetMode(SigMode);
    void StyleButtons();
    void Relayout();
    void UpdateSaveButton();
    void UpdateTheme() override;

    void OnMode(VirtMouseEvent*);
    void OnInk(VirtMouseEvent*);
    void OnStyle(VirtMouseEvent*);
    void OnNameChanged();
    void OnChooseImage(VirtMouseEvent*);
    void OnClear(VirtMouseEvent*);
    void OnSave(VirtMouseEvent*);
    void OnCancel(VirtMouseEvent*);
};

static void SignatureDialogOnClose(WindowBase::CloseEvent* ev) {
    auto* dlg = (SignatureDialog*)ev->e->self;
    if (dlg->win && IsWindow(dlg->win->hwndFrame)) {
        SetActiveWindow(dlg->win->hwndFrame);
    }
    dlg->ScheduleDelete();
}

// Material segmented buttons: the selected one on secondary-container
static void StyleSegment(VirtButton* b, bool selected) {
    const M3Scheme& m3 = M3();
    Color bg = selected ? m3.secondaryContainer : kColorTransparent;
    b->SetColor(kColBtnBg, bg);
    Color base = selected ? bg : DarkModeDialogBgColor();
    b->SetColor(kColBtnBgHover, M3StateLayer(base, m3.onSurface, kM3HoverOpacity));
    b->SetColor(kColBtnText, selected ? m3.onSecondaryContainer : m3.onSurface);
    b->SetColor(kColBtnBorder, m3.outline);
    b->Invalidate();
}

void SignatureDialog::StyleButtons() {
    for (int i = 0; i < kModeCount; i++) {
        StyleSegment(modeBtns[i], (int)pad->mode == i);
    }
    for (int i = 0; i < 2; i++) {
        StyleSegment(inkBtns[i], pad->ink == i);
    }
    for (int i = 0; i < nStyles; i++) {
        StyleSegment(styleBtns[i], style == i);
    }
}

void SignatureDialog::UpdateTheme() {
    WindowBase::UpdateTheme();
    if (pad) {
        StyleButtons();
    }
}

void SignatureDialog::UpdateSaveButton() {
    bool ok = !pad->IsEmpty();
    if (saveBtn->IsEnabled() != ok) {
        saveBtn->SetIsEnabled(ok);
        saveBtn->Invalidate();
    }
}

void SignatureDialog::Relayout() {
    Size size = layout->Layout(ExpandHeight(clientDx));
    ResizeHwndToClientArea(hwnd, size.dx, size.dy, false);
    DoLayout(size);
    HwndInvalidate(hwnd);
}

void SignatureDialog::SetMode(SigMode m) {
    pad->mode = m;
    for (int i = 0; i < kModeCount; i++) {
        modeRows[i]->SetVisibility(i == (int)m ? Visibility::Visible : Visibility::Collapse);
    }
    if (m == SigMode::Type) {
        nameEdit->SetVisibility(Visibility::Visible);
    } else {
        nameEdit->SetVisibility(Visibility::Collapse);
    }
    StyleButtons();
    UpdateSaveButton();
    Relayout();
    if (m == SigMode::Type) {
        EditSetFocus(nameEdit);
    }
}

void SignatureDialog::OnMode(VirtMouseEvent* ev) {
    SetMode((SigMode)ev->target->id);
}

void SignatureDialog::OnInk(VirtMouseEvent* ev) {
    pad->ink = ev->target->id;
    StyleButtons();
    pad->Invalidate();
}

void SignatureDialog::OnStyle(VirtMouseEvent* ev) {
    style = ev->target->id;
    pad->fontName = styleFonts[style];
    StyleButtons();
    pad->Invalidate();
}

void SignatureDialog::OnNameChanged() {
    str::ReplaceWithCopy(&pad->name, nameEdit->GetTextTemp());
    pad->Invalidate();
    UpdateSaveButton();
}

void SignatureDialog::OnChooseImage(VirtMouseEvent*) {
    TempStr path = PickImageFilePathTemp(hwnd);
    if (len(path) == 0) {
        return;
    }
    Str data = file::ReadFile(path);
    Pixmap* src = PixmapFromData(data);
    str::Free(data);
    if (!src) {
        MessageBoxWarning(hwnd, fmt(Tr("Couldn't load image '%s'").s, path::GetBaseNameTemp(path)),
                          Tr("Create Signature"));
        return;
    }
    FreePixmap(pad->image);
    pad->image = SignatureFromPhoto(src);
    FreePixmap(src);
    pad->Invalidate();
    UpdateSaveButton();
}

void SignatureDialog::OnClear(VirtMouseEvent*) {
    if (pad->mode == SigMode::Type) {
        nameEdit->SetText({});
    }
    pad->Clear();
    UpdateSaveButton();
}

void SignatureDialog::OnCancel(VirtMouseEvent*) {
    Close();
}

void SignatureDialog::OnSave(VirtMouseEvent*) {
    if (pad->IsEmpty()) {
        return;
    }
    TempStr path = GetPathInAppDataDirTemp(StrL("signature.png"));
    if (!SaveSignaturePng(pad, path)) {
        MessageBoxWarning(hwnd, Tr("Couldn't save the signature"), Tr("Create Signature"));
        return;
    }
    str::ReplaceWithCopy(&gSettings->annotations.signatureImage, path);
    ScheduleSaveSettings();

    MainWindow* w = win;
    Close();
    // put it on the page right away when there is one to sign
    if (w && IsWindow(w->hwndFrame) && w->IsDocLoaded()) {
        HwndPostCommand(w->hwndFrame, CmdSignWithImage);
    } else if (w && IsWindow(w->hwndFrame)) {
        ShowTemporaryNotification(w->hwndCanvas, Tr("Signature saved. Open a PDF and use Place Signature."));
    }
}

static VirtButton* AddSegment(HBox* row, HWND hwnd, Str text, PlatformFont* font, int id) {
    VirtButton* b = NewThemedButton(hwnd, text, font, false);
    b->id = id;
    row->AddChild(b);
    return b;
}

bool SignatureDialog::Create(MainWindow* w) {
    win = w;
    closeOnEsc = true;
    onClose = MkFunc1Void(SignatureDialogOnClose);

    CreateCustomArgs cargs;
    cargs.title = Tr("Create Signature");
    cargs.font = GetDefaultGuiFont();
    cargs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    cargs.visible = false;
    cargs.icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
    cargs.bgColor = DarkModeDialogBgColor();
    CreateCustom(cargs);
    if (!hwnd) {
        return false;
    }
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, (LONG_PTR)w->hwndFrame);

    int gap = DpiScale(8);
    bool rtl = IsUIRtl();
    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;
    box->gap = DpiScale(10);

    // Draw | Type | Upload
    auto* modes = new HBox();
    modes->gap = DpiScale(4);
    Str modeNames[kModeCount] = {Tr("Draw"), Tr("Type"), Tr("Upload")};
    for (int i = 0; i < kModeCount; i++) {
        modeBtns[i] = AddSegment(modes, hwnd, modeNames[i], font, i);
        modeBtns[i]->onClick = MkMethod1<SignatureDialog, VirtMouseEvent*, &SignatureDialog::OnMode>(this);
    }
    box->AddChild(modes);

    pad = new SignaturePad();
    pad->hintFont = font;
    pad->onChanged = MkMethod0<SignatureDialog, &SignatureDialog::UpdateSaveButton>(this);
    box->AddChild(pad);

    // Draw: just a hint
    modeRows[(int)SigMode::Draw] =
        NewVirtText({.s = Tr("Sign with your mouse, pen or finger."), .font = font, .isRtl = rtl});
    box->AddChild(modeRows[(int)SigMode::Draw]);

    // Type: the name, then a row of handwriting styles, each in its own font
    auto* typeBox = new VBox();
    typeBox->alignCross = CrossAxisAlign::Stretch;
    typeBox->gap = gap;
    auto* styleRow = new HBox();
    styleRow->alignCross = CrossAxisAlign::CrossCenter;
    styleRow->gap = DpiScale(4);
    Edit::CreateArgs eargs;
    eargs.parent = hwnd;
    eargs.withBorder = true;
    eargs.font = font;
    eargs.isRtl = rtl;
    eargs.cueText = Tr("Your name");
    nameEdit = new Edit();
    nameEdit->Create(eargs);
    nameEdit->onTextChanged = MkMethod0<SignatureDialog, &SignatureDialog::OnNameChanged>(this);
    typeBox->AddChild(nameEdit);
    typeBox->AddChild(styleRow);
    // GetPlatformFont scales for the dpi itself
    constexpr float kStylePt = 13.f;
    for (const char* f : gCursiveFonts) {
        if (nStyles == kMaxStyles || !IsFontInstalled(f)) {
            continue;
        }
        PlatformFont* pf = GetPlatformFont(Str(f), kStylePt, PlatformFontStyle::Regular);
        styleFonts[nStyles] = f;
        styleBtns[nStyles] = AddSegment(styleRow, hwnd, StrL("Signature"), pf, nStyles);
        styleBtns[nStyles]->SetTooltip(Str(f));
        styleBtns[nStyles]->onClick = MkMethod1<SignatureDialog, VirtMouseEvent*, &SignatureDialog::OnStyle>(this);
        nStyles++;
    }
    pad->fontName = nStyles > 0 ? styleFonts[0] : "Segoe UI";
    modeRows[(int)SigMode::Type] = typeBox;
    box->AddChild(typeBox);

    // Upload: pick a picture; white paper becomes transparent
    auto* uploadRow = new HBox();
    uploadRow->alignCross = CrossAxisAlign::CrossCenter;
    uploadRow->gap = gap;
    VirtButton* chooseBtn = NewThemedButton(hwnd, Tr("Choose Image..."), font, false);
    chooseBtn->onClick = MkMethod1<SignatureDialog, VirtMouseEvent*, &SignatureDialog::OnChooseImage>(this);
    uploadRow->AddChild(chooseBtn);
    uploadRow->AddChild(
        NewVirtText({.s = Tr("A photo of your signature on white paper works best."), .font = font, .isRtl = rtl}));
    modeRows[(int)SigMode::Upload] = uploadRow;
    box->AddChild(uploadRow);

    // ink color, then the actions
    auto* bottom = new HBox();
    bottom->alignCross = CrossAxisAlign::CrossCenter;
    bottom->gap = DpiScale(4);
    Str inkNames[2] = {Tr("Black"), Tr("Blue")};
    for (int i = 0; i < 2; i++) {
        inkBtns[i] = AddSegment(bottom, hwnd, inkNames[i], font, i);
        inkBtns[i]->onClick = MkMethod1<SignatureDialog, VirtMouseEvent*, &SignatureDialog::OnInk>(this);
    }
    bottom->AddChild(new Spacer(0, 0), 1);
    VirtButton* clearBtn = NewThemedButton(hwnd, Tr("Clear"), font, false);
    clearBtn->onClick = MkMethod1<SignatureDialog, VirtMouseEvent*, &SignatureDialog::OnClear>(this);
    bottom->AddChild(clearBtn);
    bottom->AddChild(new Spacer(gap, 0));
    saveBtn = NewThemedButton(hwnd, Tr("Save Signature"), font, true);
    saveBtn->onClick = MkMethod1<SignatureDialog, VirtMouseEvent*, &SignatureDialog::OnSave>(this);
    bottom->AddChild(saveBtn);
    VirtButton* cancelBtn = NewThemedButton(hwnd, Tr("Cancel"), font, false);
    cancelBtn->onClick = MkMethod1<SignatureDialog, VirtMouseEvent*, &SignatureDialog::OnCancel>(this);
    bottom->AddChild(cancelBtn);
    box->AddChild(bottom);

    layout = new Padding(box, DpiScaledInsets(16));
    clientDx = DpiScale(kPadDx) + DpiScale(32);
    UpdateTheme();
    SetMode(SigMode::Draw);
    HwndCenterDialog(hwnd, w->hwndFrame);
    SetIsVisible(true);
    return true;
}

bool HasSavedSignature() {
    Str path = gSettings->annotations.signatureImage;
    return len(path) > 0 && file::Exists(path);
}

void ShowCreateSignatureDialog(MainWindow* win) {
    if (!win || !CanAccessDisk()) {
        return;
    }
    auto* dlg = new SignatureDialog();
    if (!dlg->Create(win)) {
        delete dlg;
    }
}
