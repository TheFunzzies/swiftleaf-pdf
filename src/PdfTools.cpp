/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/UITask.h"
#include "gui/Dpi.h"
#include "base/File.h"
#include "base/Win.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"

#include "Settings.h"
#include "DocController.h"
#include "DocProperties.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "Annotation.h"
#include "PdfCreator.h"
#include "ImageReader.h"
#include "PngOptimizer.h"
#include "base/Pixmap.h"
#include "base/GdiPlusUtil.h"
#include "SumatraPDF.h"
#include "SumatraConfig.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Translations.h"
#include "ExternalViewers.h"
#include "Flags.h"
#include "DisplayModel.h"
#include "Selection.h"
#include "Theme.h"
#include "Notifications.h"

#include "DarkMode.h"
#include "Material.h"
#include "Commands.h"
#include "PageOrganize.h"
#include "ExportFormats.h"
#include <mupdf/pdf.h>
#include "PdfTools.h"

extern const pdf_write_options gPdfDefaultWriteOptions;

extern "C" int pdfbake_main(int argc, char** argv);
extern "C" int pdfclean_main(int argc, char** argv);
extern "C" int muconvert_main(int argc, char** argv);
extern "C" void fz_set_optind(int val);

// compute a dialog client width that fits the source path text, clamped to a
// minimum and to 80% of the screen width (long paths get ellipsized instead)
static int CalcDlgWidth(PlatformFont* font, Str path, int minW, int padding) {
    Size size = PlatformFontMeasureText(font, path);
    int dlgW = size.dx + (2 * padding) + DpiScale(32);
    dlgW = std::max(dlgW, minW);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    dlgW = std::min(dlgW, screenW * 80 / 100);
    return dlgW;
}

// the SwiftLeaf app icon, shown in each tool dialog's title bar / taskbar
static HICON GetAppIcon() {
    return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
}

// shared "Save As" browse used by the layout-based PDF tool dialogs below.
// Seeds the dialog with the edit's current text and writes the chosen path back.
static void BrowseForDest(HWND owner, Edit* edit, WStr filter, WStr defExt) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    TempWStr currentFileName = ToWStrTemp(HwndGetTextTemp(edit->hwnd));
    wstr::BufSet(WStr(dstFileName, MAX_PATH), currentFileName);

    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = filter.s;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = defExt.s;

    if (GetSaveFileNameW(&ofn)) {
        edit->SetText(ToUtf8Temp(dstFileName));
    }
}

// Every dialog here has the same shape: the source path, a destination edit
// with a browse button, sometimes one more labelled field, and an action +
// Cancel row. PdfToolDialog builds that as one layout tree: the labels and
// buttons are virtual controls, the text fields are native edits, and they sit
// in the same VBox/HBox side by side. Each dialog only adds its rows and
// implements DoIt().
struct PdfToolDialog : WindowBase {
    Str srcPath; // owned
    MainWindow* win = nullptr;
    // what the "..." button's save dialog offers
    WStr browseFilter;
    WStr browseDefExt;

    VBox* mainBox = nullptr;
    VirtText* pathLabel = nullptr;
    Edit* destEdit = nullptr;
    VirtButton* browseBtn = nullptr;
    VirtButton* actionBtn = nullptr;
    VirtButton* cancelBtn = nullptr;
    // the row AddRow() built last, so a dialog can put more in it
    HBox* lastRow = nullptr;
    int rowGap = 0;
    int gap = 0;

    ~PdfToolDialog() override;

    bool CreateToolDialog(MainWindow*, WindowTab*, Str title);
    HBox* AddRow();
    void AddPathRow();
    void AddDestRow(Str destPath, WStr filter, WStr defExt);
    Edit* AddLabeledEdit(Str label, Str text, bool isPassword = false);
    void AddButtonsRow(Str actionText, Str hint = {});
    void FinishDialog(Edit* focusOn, int minWidth = 480);
    VirtButton* NewButton(Str text, bool isDefault);

    void OnBrowse(VirtMouseEvent* ev = nullptr);
    void OnCancel(VirtMouseEvent* ev = nullptr);
    // what the action button does; the only thing the dialogs really differ in
    virtual void DoIt(VirtMouseEvent* ev = nullptr) {}
};

PdfToolDialog::~PdfToolDialog() {
    str::FreePtr(&srcPath);
    // ~WindowBase deletes `layout`, which owns the whole tree - the virtual
    // controls and the edits alike
}

static void PdfToolDialogOnClose(WindowBase::CloseEvent* ev) {
    auto* dlg = (PdfToolDialog*)ev->e->self;
    // hand activation back to the main window ourselves: after the browse
    // dialog has been up, letting DestroyWindow pick the next window can drop
    // the frame behind whatever else is on screen
    if (dlg->win && IsWindow(dlg->win->hwndFrame)) {
        SetActiveWindow(dlg->win->hwndFrame);
    }
    // must not `delete dlg` here: we're inside the window's own WM_CLOSE
    dlg->ScheduleDelete();
}

void PdfToolDialog::OnCancel(VirtMouseEvent*) {
    Close();
}

void PdfToolDialog::OnBrowse(VirtMouseEvent*) {
    BrowseForDest(hwnd, destEdit, browseFilter, browseDefExt);
}

VirtButton* PdfToolDialog::NewButton(Str text, bool isDefault) {
    return NewThemedButton(hwnd, text, font, isDefault);
}

bool PdfToolDialog::CreateToolDialog(MainWindow* w, WindowTab* tab, Str title) {
    win = w;
    srcPath = tab ? str::Dup(tab->filePath) : Str();
    PlatformFont* dialogFont = GetDefaultGuiFont();
    closeOnEsc = true;
    onClose = MkFunc1Void(PdfToolDialogOnClose);

    CreateCustomArgs cargs;
    cargs.title = title;
    cargs.font = dialogFont;
    cargs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    cargs.visible = false;
    cargs.icon = GetAppIcon();
    cargs.bgColor = DarkModeDialogBgColor();
    CreateCustom(cargs);
    if (!hwnd) {
        return false;
    }
    // make the dialog an owned (rather than child) window of the main frame
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, (LONG_PTR)w->hwndFrame);

    rowGap = DpiScale(6);
    gap = DpiScale(8);
    mainBox = new VBox();
    mainBox->alignCross = CrossAxisAlign::Stretch;
    layout = new Padding(mainBox, DpiScaledInsets(10));
    return true;
}

// a row of the dialog, with the gap that separates it from the row above
HBox* PdfToolDialog::AddRow() {
    auto* row = new HBox();
    row->alignCross = CrossAxisAlign::CrossCenter;
    if (mainBox->ChildrenCount() > 0) {
        mainBox->AddChild(new Padding(row, Insets{rowGap, 0, 0, 0}));
    } else {
        mainBox->AddChild(row);
    }
    lastRow = row;
    return row;
}

// the source path, ellipsized in the middle for long paths
void PdfToolDialog::AddPathRow() {
    pathLabel = NewVirtText({.s = srcPath, .font = font, .isRtl = IsUIRtl(), .ellipsis = true});
    mainBox->AddChild(pathLabel);
}

void PdfToolDialog::AddDestRow(Str destPath, WStr filter, WStr defExt) {
    browseFilter = filter;
    browseDefExt = defExt;
    HBox* row = AddRow();
    row->gap = font->averageCharWidth;

    Edit::CreateArgs args;
    args.parent = hwnd;
    args.withBorder = true;
    args.font = font;
    args.text = destPath;
    args.isRtl = IsUIRtl();
    destEdit = new Edit();
    destEdit->Create(args);
    row->AddChild(destEdit, 1);

    browseBtn = NewButton(StrL("..."), false);
    browseBtn->onClick = MkMethod1<PdfToolDialog, VirtMouseEvent*, &PdfToolDialog::OnBrowse>(this);
    row->AddChild(browseBtn);

    // align the source path label's text with the destination edit's text,
    // which is inset by the edit's border + internal left margin
    if (pathLabel) {
        pathLabel->padding.left = destEdit->GetLeftTextMargin();
    }
}

Edit* PdfToolDialog::AddLabeledEdit(Str label, Str text, bool isPassword) {
    HBox* row = AddRow();

    row->AddChild(NewVirtText({.s = label, .font = font, .isRtl = IsUIRtl()}));
    row->AddChild(new Spacer(gap, 0));

    Edit::CreateArgs eargs;
    eargs.parent = hwnd;
    eargs.withBorder = true;
    eargs.font = font;
    eargs.text = text;
    eargs.isRtl = IsUIRtl();
    auto* e = new Edit();
    e->Create(eargs);
    if (isPassword) {
        HwndSetWindowStyle(e->hwnd, ES_PASSWORD, true);
    }
    row->AddChild(e, 1);
    return e;
}

void PdfToolDialog::AddButtonsRow(Str actionText, Str hint) {
    HBox* row = AddRow();
    row->gap = font->averageCharWidth;
    if (hint) {
        row->AddChild(NewVirtText({.s = hint, .font = font, .isRtl = IsUIRtl()}));
    }
    // a flexible spacer pushes the buttons to the right
    row->AddChild(new Spacer(0, 0), 1);

    actionBtn = NewButton(actionText, true);
    actionBtn->onClick = MkMethod1<PdfToolDialog, VirtMouseEvent*, &PdfToolDialog::DoIt>(this);
    row->AddChild(actionBtn);

    cancelBtn = NewButton(Tr("Cancel"), false);
    cancelBtn->onClick = MkMethod1<PdfToolDialog, VirtMouseEvent*, &PdfToolDialog::OnCancel>(this);
    row->AddChild(cancelBtn);
}

void PdfToolDialog::FinishDialog(Edit* focusOn, int minWidth) {
    // size to a width that fits the source path (clamped), let the layout
    // compute the height
    int minClientW = DpiScale(minWidth);
    int clientW = CalcDlgWidth(font, srcPath, minClientW, DpiScale(10));
    Size size = layout->Layout(ExpandHeight(clientW));
    ResizeHwndToClientArea(hwnd, size.dx, size.dy, false);
    // positions everything and picks up the virtual controls to paint
    DoLayout(size);

    HwndCenterDialog(hwnd, win->hwndFrame);
    UpdateTheme();
    SetIsVisible(true);
    EditSetFocus(focusOn);
}

struct PdfBakeDialog : PdfToolDialog {
    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

void PdfBakeDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }

    Str inputPath = srcPath;
    TempStr tmpPath;
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    EngineBase* engine = tab ? tab->GetEngine() : nullptr;
    // pdfbake_main re-opens the file from disk, so unsaved session
    // annotations would be missing unless we write them out first (issue #5977).
    if (engine && EngineHasUnsavedAnnotations(engine)) {
        tmpPath = GetTempFilePathTemp(StrL("bake"));
        if (len(tmpPath) == 0 || !EngineMupdfSaveCopy(engine, tmpPath)) {
            MessageBoxWarning(hwnd, StrL("Failed to bake PDF file."), Tr("Bake PDF"));
            return;
        }
        inputPath = tmpPath;
        logf("PdfBakeDoIt: unsaved annotations, baking from temp '%s'\n", tmpPath);
    }

    logf("PdfBakeDoIt: baking '%s' to '%s'\n", inputPath, destPath);

    // build argv for pdfbake_main: "bake" input output
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        file::Delete(tmpPath);
        MessageBoxWarning(hwnd, Tr("Couldn't create a temporary file beside the PDF."), Tr("Save PDF"));
        return;
    }
    char* argv[] = {(char*)"bake", CStrTemp(inputPath), CStrTemp(output.tempPath)};
    int argc = 3;

    fz_set_optind(0);
    int res = pdfbake_main(argc, argv);
    if (res == 0 && !output.Commit()) {
        res = 1;
    }
    if (tmpPath) {
        file::Delete(tmpPath);
    }
    if (res == 0) {
        logf("PdfBakeDoIt: baked successfully\n");
        MainWindow* w = win;
        TempStr path = str::DupTemp(destPath);
        Close();
        // open the baked file
        LoadArgs args(path, w);
        StartLoadDocument(&args);
    } else {
        logf("PdfBakeDoIt: pdfbake_main failed with %d\n", res);
        MessageBoxWarning(hwnd, StrL("Failed to bake PDF file."), Tr("Bake PDF"));
    }
}

bool PdfBakeDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Bake PDF"))) {
        return false;
    }
    AddPathRow();
    AddDestRow(MakeUniqueFilePathTemp(srcPath), L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf");
    AddButtonsRow(Tr("Bake PDF"));
    FinishDialog(destEdit);
    return true;
}

void ShowPdfBakeDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    if (!IsPdfDoc(tab)) {
        return;
    }
    logf("ShowPdfBakeDialog: opening for '%s'\n", tab->filePath);

    auto* dlg = new PdfBakeDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Extract PDF Text dialog ---

struct PdfExtractTextDialog : PdfToolDialog {
    Edit* pagesEdit = nullptr;
    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

static bool ExtractTextViaEngine(PdfExtractTextDialog* dlg, Str destPath, Str pages) {
    MainWindow* win = dlg->win;
    if (!win || !win->ctrl) {
        return false;
    }
    DisplayModel* dm = win->ctrl->AsFixed();
    if (!dm) {
        return false;
    }
    EngineBase* engine = dm->GetEngine();
    if (!engine) {
        return false;
    }
    int pageCount = engine->PageCount();
    Vec<PageRange> ranges;
    if (!ParsePageRanges(pages, ranges)) {
        return false;
    }
    str::Builder text;
    for (auto& range : ranges) {
        int start = std::max(range.start, 1);
        int end = std::min(range.end, pageCount);
        for (int pageNo = start; pageNo <= end; pageNo++) {
            PageText pt = engine->ExtractPageText(pageNo);
            if (pt.text) {
                text.Append(Str(pt.text.s));
                text.AppendChar('\n');
            }
            FreePageText(&pt);
        }
    }
    return file::WriteFile(destPath, ToStr(text));
}

void PdfExtractTextDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }

    TempStr pages = pagesEdit->GetTextTemp();
    if (len(pages) == 0) {
        return;
    }

    logf("PdfExtractTextDoIt: extracting text from '%s' to '%s', pages: %s\n", srcPath, destPath, pages);

    bool ok = false;
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    bool isPdf = tab && IsPdfDoc(tab);
    if (isPdf) {
        // use muconvert for PDF
        char* argv[] = {(char*)"convert", (char*)"-o", CStrTemp(destPath), CStrTemp(srcPath), CStrTemp(pages)};
        int argc = 5;
        fz_set_optind(0);
        ok = muconvert_main(argc, argv) == 0;
    } else {
        // use engine text extraction for other formats (DjVu, etc.)
        ok = ExtractTextViaEngine(this, destPath, pages);
    }

    if (ok) {
        logf("PdfExtractTextDoIt: extracted successfully\n");
        TempStr path = str::DupTemp(destPath);
        Close();
        OpenPathInDefaultFileManager(path);
    } else {
        logf("PdfExtractTextDoIt: failed to extract text, isPdf: %d\n", (int)isPdf);
        MessageBoxWarning(hwnd, StrL("Failed to extract text."), Tr("Extract Text"));
    }
}

bool PdfExtractTextDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Extract Text From PDF"))) {
        return false;
    }
    AddPathRow();
    TempStr noExt = path::GetPathNoExtTemp(srcPath);
    TempStr txtPath = str::JoinTemp(noExt, StrL(".txt"));
    AddDestRow(MakeUniqueFilePathTemp(txtPath), L"Text Files\0*.txt\0All Files\0*.*\0", L"txt");
    int pageCount = w->ctrl ? w->ctrl->PageCount() : 1;
    pagesEdit = AddLabeledEdit(Tr("Pages:"), fmt("1-%d", pageCount));
    AddButtonsRow(Tr("Extract Text"));
    FinishDialog(destEdit);
    return true;
}

void ShowPdfExtractTextDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    logf("ShowPdfExtractTextDialog: opening for '%s'\n", tab->filePath);

    auto* dlg = new PdfExtractTextDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Compress PDF dialog & optimization engine ---

constexpr int kCompressPresetCount = 4;
static const PdfOptimizerPreset kCompressPresets[kCompressPresetCount] = {
    PdfOptimizerPreset::Archive,
    PdfOptimizerPreset::Print,
    PdfOptimizerPreset::Web,
    PdfOptimizerPreset::MinSize,
};

bool PdfAuditBreakdown(Str filePath, PdfOptimizationAudit* auditOut) {
    if (!auditOut) {
        return false;
    }
    i64 total = file::GetSize(filePath);
    if (total <= 0) {
        return false;
    }
    if (auditOut->beforeBytes <= 0) {
        auditOut->beforeBytes = total;
    }
    if (auditOut->afterBytes <= 0) {
        auditOut->afterBytes = total;
    }
    auditOut->imageBytes = 0;
    auditOut->fontBytes = 0;
    auditOut->objectBytes = 0;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        return false;
    }
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    fz_var(doc);
    i64 images = 0;
    i64 fonts = 0;
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(filePath));
        int n = pdf_xref_len(ctx, doc);

        Vec<int> fontObjNums;
        for (int i = 1; i < n; i++) {
            pdf_obj* obj = pdf_load_object(ctx, doc, i);
            if (!obj) {
                continue;
            }
            if (pdf_is_dict(ctx, obj)) {
                pdf_obj* type = pdf_dict_get(ctx, obj, PDF_NAME(Type));
                pdf_obj* subtype = pdf_dict_get(ctx, obj, PDF_NAME(Subtype));
                if (pdf_name_eq(ctx, type, PDF_NAME(Font)) || pdf_name_eq(ctx, type, PDF_NAME(FontDescriptor)) ||
                    pdf_name_eq(ctx, subtype, PDF_NAME(Type1C)) || pdf_name_eq(ctx, subtype, PDF_NAME(CIDFontType0C))) {
                    if (!VecContains(fontObjNums, i)) {
                        VecAppend(fontObjNums, i);
                    }
                    pdf_obj* ff = pdf_dict_get(ctx, obj, PDF_NAME(FontFile));
                    if (!ff) {
                        ff = pdf_dict_get(ctx, obj, PDF_NAME(FontFile2));
                    }
                    if (!ff) {
                        ff = pdf_dict_get(ctx, obj, PDF_NAME(FontFile3));
                    }
                    if (ff && pdf_is_indirect(ctx, ff)) {
                        int num = pdf_to_num(ctx, ff);
                        if (num > 0 && !VecContains(fontObjNums, num)) {
                            VecAppend(fontObjNums, num);
                        }
                    }
                }
            }
            pdf_drop_obj(ctx, obj);
        }

        for (int i = 1; i < n; i++) {
            pdf_obj* obj = pdf_load_object(ctx, doc, i);
            if (!obj) {
                continue;
            }
            if (pdf_is_dict(ctx, obj)) {
                i64 len = pdf_to_int64(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Length)));
                if (len > 0) {
                    pdf_obj* type = pdf_dict_get(ctx, obj, PDF_NAME(Type));
                    pdf_obj* subtype = pdf_dict_get(ctx, obj, PDF_NAME(Subtype));
                    bool isImage =
                        pdf_name_eq(ctx, subtype, PDF_NAME(Image)) ||
                        (pdf_name_eq(ctx, type, PDF_NAME(XObject)) && pdf_name_eq(ctx, subtype, PDF_NAME(Image)));
                    bool isFont = VecContains(fontObjNums, i);
                    if (isImage) {
                        images += len;
                    } else if (isFont) {
                        fonts += len;
                    }
                }
            }
            pdf_drop_obj(ctx, obj);
        }
    }
    fz_always(ctx) {
        if (doc) {
            pdf_drop_document(ctx, doc);
        }
    }
    fz_catch(ctx) {}
    fz_drop_context(ctx);

    if (images > total) {
        images = total;
    }
    if (fonts > (total - images)) {
        fonts = total - images;
    }
    i64 objects = total - images - fonts;
    if (objects < 0) {
        objects = 0;
    }

    auditOut->imageBytes = images;
    auditOut->fontBytes = fonts;
    auditOut->objectBytes = objects;
    return true;
}

Str PdfOptimizerPresetDescription(PdfOptimizerPreset preset) {
    switch (preset) {
        case PdfOptimizerPreset::Archive:
            return Tr(
                "Archive: Lossless structure and stream compression for storage. Note: does not convert to ISO PDF/A "
                "compliance.");
        case PdfOptimizerPreset::Print:
            return Tr(
                "Print: Preserves high-resolution images (>=300 DPI) and compresses streams for physical printing.");
        case PdfOptimizerPreset::Web:
            return Tr("Web: Photos scaled to 150 DPI JPEG (quality 75): balanced size for screen viewing.");
        case PdfOptimizerPreset::MinSize:
            return Tr("Smallest: Photos scaled to 96 DPI JPEG (quality 50): minimum file size.");
    }
    return {};
}

bool PdfOptimizeFile(Str srcPath, Str destPath, PdfOptimizerPreset preset, PdfOptimizationAudit* auditOut) {
    i64 beforeBytes = file::GetSize(srcPath);
    if (beforeBytes <= 0) {
        return false;
    }

    TempStr destDir = path::GetDirTemp(destPath);
    if (len(destDir) == 0) {
        destDir = path::GetDirTemp(srcPath);
    }
    static u32 gOptCounter = 0;
    TempStr tempOutput = path::JoinTemp(destDir, fmt("opt-%u-%u-%u.pdf", (u32)GetCurrentProcessId(), (u32)GetTickCount(), ++gOptCounter));

    Vec<char*> argv;
    VecAppend(argv, (char*)"clean");
    VecAppend(argv, (char*)"-gggg");
    VecAppend(argv, (char*)"-e");
    VecAppend(argv, (char*)"100");
    VecAppend(argv, (char*)"-f");
    VecAppend(argv, (char*)"-i");
    VecAppend(argv, (char*)"-t");
    VecAppend(argv, (char*)"-z");
    VecAppend(argv, (char*)"-Z");

    switch (preset) {
        case PdfOptimizerPreset::Archive:
            // Lossless storage: keep structure, no downsampling, lossless recompression.
            // Distinct from PDF/A (does not enforce PDF/A color profiles or metadata).
            VecAppend(argv, (char*)"--structure=keep");
            VecAppend(argv, (char*)"--color-image-recompress-method=lossless");
            VecAppend(argv, (char*)"--gray-image-recompress-method=lossless");
            VecAppend(argv, (char*)"--bitonal-image-recompress-method=lossless");
            break;
        case PdfOptimizerPreset::Print:
            // Print: retain high resolution (>=300 DPI) for print quality, compress high-res images >450 to 300 DPI.
            VecAppend(argv, (char*)"--structure=keep");
            VecAppend(argv, (char*)"--color-image-subsample-dpi=450,300");
            VecAppend(argv, (char*)"--gray-image-subsample-dpi=450,300");
            VecAppend(argv, (char*)"--color-image-recompress-method=jpeg:85");
            VecAppend(argv, (char*)"--gray-image-recompress-method=jpeg:85");
            break;
        case PdfOptimizerPreset::Web:
            VecAppend(argv, (char*)"--color-image-subsample-dpi=225,150");
            VecAppend(argv, (char*)"--gray-image-subsample-dpi=225,150");
            VecAppend(argv, (char*)"--color-image-recompress-method=jpeg:75");
            VecAppend(argv, (char*)"--gray-image-recompress-method=jpeg:75");
            break;
        case PdfOptimizerPreset::MinSize:
            VecAppend(argv, (char*)"--color-image-subsample-dpi=144,96");
            VecAppend(argv, (char*)"--gray-image-subsample-dpi=144,96");
            VecAppend(argv, (char*)"--color-image-recompress-method=jpeg:50");
            VecAppend(argv, (char*)"--gray-image-recompress-method=jpeg:50");
            break;
    }

    VecAppend(argv, CStrTemp(srcPath));
    VecAppend(argv, CStrTemp(tempOutput));
    VecAppend(argv, (char*)nullptr);

    fz_set_optind(0);
    int res = pdfclean_main(len(argv) - 1, argv.els);
    if (res != 0) {
        file::Delete(tempOutput);
        return false;
    }

    i64 tempSize = file::GetSize(tempOutput);
    // keep original if output would grow
    if (tempSize >= beforeBytes) {
        logf("PdfOptimizeFile: output would grow (%lld -> %lld bytes), keeping original\n", beforeBytes, tempSize);
        file::Delete(tempOutput);
        if (!path::IsSame(srcPath, destPath)) {
            if (!file::Copy(destPath, srcPath, false)) {
                return false;
            }
        }
    } else {
        if (!file::RenameReplace(destPath, tempOutput)) {
            if (!file::Copy(destPath, tempOutput, false)) {
                file::Delete(tempOutput);
                return false;
            }
        }
        file::Delete(tempOutput);
    }

    i64 afterBytes = file::GetSize(destPath);
    if (auditOut) {
        auditOut->beforeBytes = beforeBytes;
        auditOut->afterBytes = afterBytes;
        PdfAuditBreakdown(destPath, auditOut);
    }
    return true;
}

struct PdfCompressDialog : PdfToolDialog {
    PdfOptimizerPreset preset = PdfOptimizerPreset::Web;
    VirtButton* presetBtns[kCompressPresetCount]{};
    VirtText* levelHint = nullptr;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void OnPreset(VirtMouseEvent* ev);
    void StylePresets();
    void UpdateTheme() override;
};

// the chosen preset in the selected-segment colors
void PdfCompressDialog::StylePresets() {
    const M3Scheme& m3 = M3();
    for (int i = 0; i < kCompressPresetCount; i++) {
        VirtButton* b = presetBtns[i];
        bool selected = preset == kCompressPresets[i];
        Color bg = selected ? m3.secondaryContainer : kColorTransparent;
        Color base = selected ? bg : DarkModeDialogBgColor();
        b->SetColor(kColBtnBg, bg);
        b->SetColor(kColBtnBgHover, M3StateLayer(base, m3.onSurface, kM3HoverOpacity));
        b->SetColor(kColBtnText, selected ? m3.onSecondaryContainer : m3.onSurface);
        b->SetColor(kColBtnBorder, m3.outline);
        b->Invalidate();
    }
}

void PdfCompressDialog::UpdateTheme() {
    PdfToolDialog::UpdateTheme();
    if (presetBtns[0]) {
        StylePresets();
    }
}

void PdfCompressDialog::OnPreset(VirtMouseEvent* ev) {
    int idx = ev->target->id;
    if (idx >= 0 && idx < kCompressPresetCount) {
        preset = kCompressPresets[idx];
    }
    StylePresets();
    levelHint->SetText(PdfOptimizerPresetDescription(preset));
    DoLayout();
    HwndInvalidate(hwnd);
}

void PdfCompressDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }

    logf("PdfCompressDoIt: compressing '%s' to '%s', preset %d\n", srcPath, destPath, (int)preset);

    PdfOptimizationAudit audit{};
    if (!PdfOptimizeFile(srcPath, destPath, preset, &audit)) {
        logf("PdfCompressDoIt: PdfOptimizeFile failed\n");
        MessageBoxWarning(hwnd, StrL("Failed to compress PDF file."), Tr("Compress PDF"));
        return;
    }

    if (audit.afterBytes >= audit.beforeBytes) {
        logf("PdfCompressDoIt: output would grow (%lld -> %lld bytes), kept original\n", audit.beforeBytes,
             audit.afterBytes);
        MessageBoxWarning(hwnd, Tr("Compression would increase file size. Kept original document."),
                          Tr("Compress PDF"));
    } else {
        logf("PdfCompressDoIt: compressed %lld -> %lld bytes\n", audit.beforeBytes, audit.afterBytes);
    }

    MainWindow* w = win;
    TempStr path = str::DupTemp(destPath);
    Close();
    LoadArgs args(path, w);
    StartLoadDocument(&args);
}

bool PdfCompressDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Compress PDF"))) {
        return false;
    }
    AddPathRow();

    PdfOptimizationAudit initialAudit{};
    if (PdfAuditBreakdown(srcPath, &initialAudit)) {
        HBox* auditRow = AddRow();
        TempStr auditText = fmt("%s: %s (%s: %s, %s: %s, %s: %s)", Tr("Current size"),
                                str::FormatSizeShortTemp(initialAudit.beforeBytes), Tr("Images"),
                                str::FormatSizeShortTemp(initialAudit.imageBytes), Tr("Fonts"),
                                str::FormatSizeShortTemp(initialAudit.fontBytes), Tr("Objects"),
                                str::FormatSizeShortTemp(initialAudit.objectBytes));
        auditRow->AddChild(NewVirtText({.s = auditText, .font = font, .isRtl = IsUIRtl()}));
    }

    // report.pdf -> report_compressed.pdf
    Str stem = str::DupTemp(srcPath);
    if (str::EndsWithI(stem, StrL(".pdf"))) {
        stem.len -= 4;
    }
    TempStr dest = MakeUniqueFilePathTemp(fmt("%s_compressed.pdf", stem));
    AddDestRow(dest, L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf");

    HBox* row = AddRow();
    row->gap = DpiScale(4);
    row->AddChild(NewVirtText({.s = Tr("Preset:"), .font = font, .isRtl = IsUIRtl()}));
    row->AddChild(new Spacer(gap, 0));
    Str names[kCompressPresetCount] = {Tr("Archive"), Tr("Print"), Tr("Web"), Tr("Smallest")};
    for (int i = 0; i < kCompressPresetCount; i++) {
        presetBtns[i] = NewButton(names[i], false);
        presetBtns[i]->id = i;
        presetBtns[i]->onClick = MkMethod1<PdfCompressDialog, VirtMouseEvent*, &PdfCompressDialog::OnPreset>(this);
        row->AddChild(presetBtns[i]);
    }
    HBox* hintRow = AddRow();
    levelHint = NewVirtText({.s = PdfOptimizerPresetDescription(preset), .font = font, .isRtl = IsUIRtl()});
    hintRow->AddChild(levelHint, 1);

    AddButtonsRow(Tr("Compress PDF"));
    FinishDialog(destEdit);
    StylePresets();
    return true;
}

void ShowPdfCompressDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    if (!IsPdfDoc(tab)) {
        return;
    }
    logf("ShowPdfCompressDialog: opening for '%s'\n", tab->filePath);

    auto* dlg = new PdfCompressDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Decompress PDF dialog ---

struct PdfDecompressDialog : PdfToolDialog {
    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

void PdfDecompressDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }

    logf("PdfDecompressDoIt: decompressing '%s' to '%s'\n", srcPath, destPath);

    // equivalent of: clean -d input output
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        MessageBoxWarning(hwnd, Tr("Couldn't create a temporary file beside the PDF."), Tr("Save PDF"));
        return;
    }
    char* argv[] = {(char*)"clean", (char*)"-d", CStrTemp(srcPath), CStrTemp(output.tempPath)};
    int argc = 4;

    fz_set_optind(0);
    int res = pdfclean_main(argc, argv);
    if (res == 0 && !output.Commit()) {
        res = 1;
    }
    if (res == 0) {
        logf("PdfDecompressDoIt: decompressed successfully\n");
        MainWindow* w = win;
        TempStr path = str::DupTemp(destPath);
        Close();
        LoadArgs args(path, w);
        StartLoadDocument(&args);
    } else {
        logf("PdfDecompressDoIt: pdfclean_main failed with %d\n", res);
        MessageBoxWarning(hwnd, StrL("Failed to decompress PDF file."), Tr("Decompress PDF"));
    }
}

bool PdfDecompressDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Decompress PDF"))) {
        return false;
    }
    AddPathRow();
    AddDestRow(MakeUniqueFilePathTemp(srcPath), L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf");
    AddButtonsRow(Tr("Decompress PDF"));
    FinishDialog(destEdit);
    return true;
}

void ShowPdfDecompressDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    if (!IsPdfDoc(tab)) {
        return;
    }
    logf("ShowPdfDecompressDialog: opening for '%s'\n", tab->filePath);

    auto* dlg = new PdfDecompressDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Delete Pages From PDF dialog ---

// The dialog's content is a VirtCtrl tree: the labels and buttons are virtual
// controls, while the two text fields stay real HWND edits, hosted in the tree
// so they take part in the same layout.
struct PdfDeletePageDialog : PdfToolDialog {
    bool isExtract = false;
    int pageCount = 0;
    Edit* pagesEdit = nullptr;
    Checkbox* onlyWithAnnotations = nullptr;

    bool Create(MainWindow* win, WindowTab* tab, bool isExtract);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void UpdateButton();
    void PreTranslate(WindowBase::PreTranslateEvent* ev);
};

// Parse delete page ranges like "1,3-8,13-N" where N means last page.
// Returns a sorted list of unique 1-based page numbers to delete.
// Returns false if the syntax is invalid or any page is out of range.
static bool ParseDeletePages(Str s, int pageCount, Vec<int>& pagesToDelete) {
    if (len(s) == 0) {
        return false;
    }
    StrVec parts;
    Split(&parts, s, StrL(","), true);
    if (len(parts) == 0) {
        return false;
    }
    for (int pi = 0; pi < len(parts); pi++) {
        Str part = parts[pi];
        str::TrimWSInPlace(part, str::TrimOpt::Both);
        if (len(part) == 0) {
            return false;
        }
        // check for range "A-B" where A/B can be a number or "N"
        Str startStr, endStr;
        if (str::CutChar(part, '-', &startStr, &endStr)) {
            str::TrimWSInPlace(startStr, str::TrimOpt::Both);
            str::TrimWSInPlace(endStr, str::TrimOpt::Both);
            if (len(startStr) == 0) {
                return false;
            }
            // "8-" means "8-N" (from page 8 to the last page)
            bool endIsEmpty = len(endStr) == 0;
            int start, end;
            if (str::EqI(startStr, StrL("N"))) {
                start = pageCount;
            } else {
                start = !str::IsNull(str::Parse(startStr, "%d%$", &start)) ? start : -1;
            }
            if (endIsEmpty || str::EqI(endStr, StrL("N"))) {
                end = pageCount;
            } else {
                end = !str::IsNull(str::Parse(endStr, "%d%$", &end)) ? end : -1;
            }
            if (start < 1 || start > pageCount || end < 1 || end > pageCount || start > end) {
                return false;
            }
            for (int i = start; i <= end; i++) {
                VecAppend(pagesToDelete, i);
            }
        } else {
            // single page
            int page;
            if (str::EqI(part, StrL("N"))) {
                page = pageCount;
            } else {
                page = !str::IsNull(str::Parse(part, "%d%$", &page)) ? page : -1;
            }
            if (page < 1 || page > pageCount) {
                return false;
            }
            VecAppend(pagesToDelete, page);
        }
    }
    if (len(pagesToDelete) == 0) {
        return false;
    }
    // sort and deduplicate
    VecSort(pagesToDelete, [](const int* a, const int* b) -> int { return *a - *b; });
    int prev = -1;
    Vec<int> unique;
    for (int p : pagesToDelete) {
        if (p != prev) {
            VecAppend(unique, p);
            prev = p;
        }
    }
    pagesToDelete = unique;
    return true;
}

// Build the page range string of pages to KEEP (complement of pagesToDelete).
static TempStr BuildKeepPagesRangeTemp(int pageCount, const Vec<int>& pagesToDelete) {
    str::Builder s;
    int delIdx = 0;
    int rangeStart = -1;
    int rangeEnd = -1;
    for (int p = 1; p <= pageCount; p++) {
        bool shouldDelete = (delIdx < len(pagesToDelete) && pagesToDelete[delIdx] == p);
        if (shouldDelete) {
            delIdx++;
            if (rangeStart != -1) {
                if (len(s) > 0) {
                    s.AppendChar(',');
                }
                if (rangeStart == rangeEnd) {
                    s.Append(fmt("%d", rangeStart));
                } else {
                    s.Append(fmt("%d-%d", rangeStart, rangeEnd));
                }
                rangeStart = -1;
            }
        } else {
            if (rangeStart == -1) {
                rangeStart = p;
            }
            rangeEnd = p;
        }
    }
    if (rangeStart != -1) {
        if (len(s) > 0) {
            s.AppendChar(',');
        }
        if (rangeStart == rangeEnd) {
            s.Append(fmt("%d", rangeStart));
        } else {
            s.Append(fmt("%d-%d", rangeStart, rangeEnd));
        }
    }
    return ToStrTemp(s);
}

// Format a sorted list of page numbers as a compact range string (e.g. "1-3,5,7-10").
static TempStr FormatPageRangeTemp(const Vec<int>& pages) {
    str::Builder s;
    int i = 0;
    int n = len(pages);
    while (i < n) {
        int start = pages[i];
        int end = start;
        while (i + 1 < n && pages[i + 1] == end + 1) {
            end = pages[++i];
        }
        if (len(s) > 0) {
            s.AppendChar(',');
        }
        if (start == end) {
            s.Append(fmt("%d", start));
        } else {
            s.Append(fmt("%d-%d", start, end));
        }
        i++;
    }
    return ToStrTemp(s);
}

static bool KeepOnlyPagesWithAnnotations(EngineBase* engine, Vec<int>& pages) {
    Vec<Annotation*> annotations;
    if (!EngineGetAnnotations(engine, annotations)) {
        return false;
    }

    Vec<int> annotationPages;
    for (Annotation* annotation : annotations) {
        int pageNo = PageNo(annotation);
        if (!VecContains(annotationPages, pageNo)) {
            VecAppend(annotationPages, pageNo);
        }
    }

    Vec<int> filteredPages;
    for (int pageNo : pages) {
        if (VecContains(annotationPages, pageNo)) {
            VecAppend(filteredPages, pageNo);
        }
    }
    pages = filteredPages;
    return true;
}

void PdfDeletePageDialog::UpdateButton() {
    TempStr pages = pagesEdit->GetTextTemp();
    Vec<int> parsedPages;
    bool valid = ParseDeletePages(pages, pageCount, parsedPages);
    // for delete mode, can't delete all pages
    if (valid && !isExtract && len(parsedPages) >= pageCount) {
        valid = false;
    }
    if (valid == actionBtn->HasFlag(vwfEnabled)) {
        return;
    }
    actionBtn->SetFlag(vwfEnabled, valid);
    actionBtn->Invalidate();
}

void PdfDeletePageDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = isExtract ? destEdit->GetTextTemp() : TempStr{};
    if (isExtract && len(destPath) == 0) {
        return;
    }

    TempStr pages = pagesEdit->GetTextTemp();

    Vec<int> parsedPages;
    if (!ParseDeletePages(pages, pageCount, parsedPages)) {
        return;
    }
    if (!isExtract && len(parsedPages) >= pageCount) {
        return;
    }

    if (!isExtract) {
        PdfToolEdit edit(win, srcPath);
        if (!edit.IsReady()) return;
        auto* tab = win->CurrentTab();
        Vec<PdfMergeSource> sources;
        VecAppend(sources, PdfMergeSource{edit.inputPath, EngineMupdfGetPassword(tab->GetEngine())});
        Vec<PdfMergePage> keep;
        for (int page = 1; page <= pageCount; page++) {
            if (!VecContains(parsedPages, page)) VecAppend(keep, PdfMergePage{0, page, 0});
        }
        if (EngineMupdfMergePdfs(sources, keep, edit.outputPath) && edit.Apply("Delete pages")) {
            Close();
        } else {
            MessageBoxWarning(hwnd, Tr("Couldn't change the pages of this PDF"), Tr("Delete Pages"));
        }
        return;
    }

    WindowTab* sourceTab = isExtract ? FindTabByFile(srcPath, win) : nullptr;
    EngineBase* sourceEngine = sourceTab ? sourceTab->GetEngine() : nullptr;
    if (isExtract && onlyWithAnnotations && onlyWithAnnotations->IsChecked()) {
        if (!sourceEngine || !KeepOnlyPagesWithAnnotations(sourceEngine, parsedPages)) {
            MessageBoxWarning(hwnd, StrL("Failed to read annotations from PDF file."), Tr("Extract Pages From PDF"));
            return;
        }
        if (len(parsedPages) == 0) {
            MessageBoxWarning(hwnd, StrL("No pages with annotations in the selected range."),
                              Tr("Extract Pages From PDF"));
            return;
        }
    }

    TempStr pageRange;
    if (isExtract) {
        // for extract: pass the specified pages directly to pdfclean
        pageRange = FormatPageRangeTemp(parsedPages);
    } else {
        // for delete: pass the complement (pages to keep) to pdfclean
        pageRange = BuildKeepPagesRangeTemp(pageCount, parsedPages);
    }

    Str op = isExtract ? StrL("extract") : StrL("delete");
    logf("PdfDeletePageDoIt: %s pages '%s' from '%s' to '%s', range for pdfclean: %s\n", op, pages, srcPath, destPath,
         pageRange);

    Str inputPath = srcPath;
    TempStr tmpPath;
    if (isExtract && sourceEngine && EngineHasUnsavedAnnotations(sourceEngine)) {
        tmpPath = GetTempFilePathTemp(StrL("extract-pages"));
        if (len(tmpPath) == 0 || !EngineMupdfSaveCopy(sourceEngine, tmpPath)) {
            MessageBoxWarning(hwnd, StrL("Failed to extract pages from PDF file."), Tr("Extract Pages From PDF"));
            return;
        }
        inputPath = tmpPath;
    }

    // equivalent of: clean -gggg -e 100 -f -i -t -Z input.pdf output.pdf <page-range>
    // use the same compression flags as Compress PDF so the result is re-written
    // compactly; otherwise the kept pages drag along the original's full content
    // and the output is nearly as big as the source
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        file::Delete(tmpPath);
        MessageBoxWarning(hwnd, Tr("Couldn't create a temporary file beside the PDF."), Tr("Save PDF"));
        return;
    }
    char* argv[] = {(char*)"clean",     (char*)"-gggg", (char*)"-e", (char*)"100",        (char*)"-f",
                    (char*)"-i",        (char*)"-t",    (char*)"-Z", CStrTemp(inputPath), CStrTemp(output.tempPath),
                    CStrTemp(pageRange)};
    int argc = 11;

    fz_set_optind(0);
    int res = pdfclean_main(argc, argv);
    if (res == 0 && !output.Commit()) {
        res = 1;
    }
    if (tmpPath) {
        file::Delete(tmpPath);
    }
    if (res == 0) {
        logf("PdfDeletePageDoIt: %s pages successfully\n", op);
        MainWindow* w = win;
        TempStr path = str::DupTemp(destPath);
        Close();
        LoadArgs args(path, w);
        StartLoadDocument(&args);
    } else {
        logf("PdfDeletePageDoIt: pdfclean_main failed with %d for %s\n", res, op);
        Str msg =
            isExtract ? StrL("Failed to extract pages from PDF file.") : StrL("Failed to delete pages from PDF file.");
        Str title = isExtract ? Tr("Extract Pages From PDF") : Tr("Delete Pages From PDF");
        MessageBoxWarning(hwnd, msg, title);
    }
}

// Page Up / Page Down skip the edit accelerator table; send next/prev page.
void PdfDeletePageDialog::PreTranslate(WindowBase::PreTranslateEvent* ev) {
    MSG& msg = *ev->msg;
    if (msg.message != WM_KEYDOWN) {
        return;
    }
    if (IsCtrlPressed() || IsAltPressed() || IsShiftPressed()) {
        return;
    }
    int cmdId = 0;
    if (msg.wParam == VK_NEXT) {
        cmdId = CmdGoToNextPage;
    } else if (msg.wParam == VK_PRIOR) {
        cmdId = CmdGoToPrevPage;
    } else {
        return;
    }
    if (!win || !win->hwndFrame) {
        return;
    }
    HwndSendCommand(win->hwndFrame, cmdId);
    ev->didHandle = true;
}

// the buttons are virtual controls, so they are styled here rather than by the
// system: a filled box with a border, brighter on hover

bool PdfDeletePageDialog::Create(MainWindow* w, WindowTab* tab, bool isExtractArg) {
    isExtract = isExtractArg;
    Str title = isExtract ? Tr("Extract Pages From PDF") : Tr("Delete Pages From PDF");
    if (!CreateToolDialog(w, tab, title)) {
        return false;
    }
    pageCount = w->ctrl ? w->ctrl->PageCount() : 0;
    AddPathRow();
    if (isExtract) AddDestRow(MakeUniqueFilePathTemp(srcPath), L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf");

    int currentPage = w->ctrl ? w->ctrl->CurrentPageNo() : 1;
    Str pagesLabel = isExtract ? Tr("Pages To Extract:") : Tr("Pages To Delete:");
    pagesEdit = AddLabeledEdit(pagesLabel, fmt("%d", currentPage));
    // "of N" after the edit
    lastRow->AddChild(new Spacer(gap, 0));
    lastRow->AddChild(NewVirtText({.s = fmt("of %d", pageCount), .font = font, .isRtl = IsUIRtl()}));

    if (isExtract) {
        Checkbox::CreateArgs args;
        args.parent = hwnd;
        args.text = Tr("Only with annotations");
        args.font = font;
        args.isRtl = IsUIRtl();
        onlyWithAnnotations = new Checkbox();
        onlyWithAnnotations->Create(args);
        onlyWithAnnotations->SetColors(ThemeWindowTextColor(), DarkModeDialogBgColor());
        AddRow()->AddChild(onlyWithAnnotations);
    }

    Str actionText = isExtract ? Tr("Extract Pages") : Tr("Delete Pages");
    AddButtonsRow(actionText, StrL("Syntax: 2,5-7,13-"));
    onPreTranslate =
        MkMethod1<PdfDeletePageDialog, WindowBase::PreTranslateEvent*, &PdfDeletePageDialog::PreTranslate>(this);
    FinishDialog(pagesEdit);

    // attach the change handler only now that actionBtn exists, then set the
    // initial validation state (Edit::Create fires onTextChanged on initial text)
    pagesEdit->onTextChanged = MkMethod0<PdfDeletePageDialog, &PdfDeletePageDialog::UpdateButton>(this);
    UpdateButton();
    return true;
}

static void ShowPdfPageRangeDialog(MainWindow* win, bool isExtract) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    if (!IsPdfDoc(tab)) {
        return;
    }

    int pageCount = win->ctrl ? win->ctrl->PageCount() : 0;
    if (pageCount < 2) {
        return;
    }
    logf("ShowPdfPageRangeDialog: opening %s dialog for '%s', %d pages\n", Str(isExtract ? "extract" : "delete"),
         tab->filePath, pageCount);

    auto* dlg = new PdfDeletePageDialog();
    if (!dlg->Create(win, tab, isExtract)) {
        delete dlg;
    }
}

void ShowPdfDeletePageDialog(MainWindow* win) {
    ShowPdfPageRangeDialog(win, false);
}

void ShowPdfExtractPagesDialog(MainWindow* win) {
    ShowPdfPageRangeDialog(win, true);
}

// --- Split PDF dialog (Swiftleaf) ---

struct PdfSplitDialog : PdfToolDialog {
    Edit* pagesEdit = nullptr;
    int pageCount = 0;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void UpdateButton();
};

static int ParsePagesPerFile(Str s) {
    int n = 0;
    str::TrimWSInPlace(s, str::TrimOpt::Both);
    if (str::IsNull(str::Parse(s, "%d%$", &n))) {
        return 0;
    }
    return n;
}

void PdfSplitDialog::UpdateButton() {
    int n = ParsePagesPerFile(pagesEdit->GetTextTemp());
    bool valid = n >= 1 && n < pageCount;
    if (valid == actionBtn->HasFlag(vwfEnabled)) {
        return;
    }
    actionBtn->SetFlag(vwfEnabled, valid);
    actionBtn->Invalidate();
}

void PdfSplitDialog::DoIt(VirtMouseEvent*) {
    TempStr dest = str::DupTemp(destEdit->GetTextTemp());
    int n = ParsePagesPerFile(pagesEdit->GetTextTemp());
    if (len(dest) == 0 || n < 1) {
        return;
    }
    MainWindow* w = win;
    Close();
    OrganizeSplitPdf(w, dest, n);
}

bool PdfSplitDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Split PDF"))) {
        return false;
    }
    pageCount = w->ctrl ? w->ctrl->PageCount() : 0;
    AddPathRow();
    Str base = str::DupTemp(srcPath);
    if (str::EndsWithI(base, StrL(".pdf"))) {
        base.len -= 4;
    }
    AddDestRow(fmt("%s-part.pdf", base), L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf");
    pagesEdit = AddLabeledEdit(Tr("Pages per file:"), StrL("1"));
    lastRow->AddChild(new Spacer(gap, 0));
    lastRow->AddChild(NewVirtText({.s = fmt("of %d", pageCount), .font = font, .isRtl = IsUIRtl()}));
    AddButtonsRow(Tr("Split"), StrL("Writes <name>-1.pdf, <name>-2.pdf, ..."));
    FinishDialog(pagesEdit);
    pagesEdit->onTextChanged = MkMethod0<PdfSplitDialog, &PdfSplitDialog::UpdateButton>(this);
    UpdateButton();
    return true;
}

void ShowPdfSplitDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0 || !IsPdfDoc(tab)) {
        return;
    }
    if (!win->ctrl || win->ctrl->PageCount() < 2) {
        return;
    }
    auto* dlg = new PdfSplitDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Encrypt PDF dialog ---

struct PdfEncryptDialog : PdfToolDialog {
    Edit* passwordEdit = nullptr;
    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void UpdateButton();
};

// there is nothing to encrypt with until a password is typed
void PdfEncryptDialog::UpdateButton() {
    TempStr pwd = passwordEdit->GetTextTemp();
    bool valid = len(pwd) > 0;
    if (valid == actionBtn->HasFlag(vwfEnabled)) {
        return;
    }
    actionBtn->SetFlag(vwfEnabled, valid);
    actionBtn->Invalidate();
}

void PdfEncryptDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }

    TempStr pwd = passwordEdit->GetTextTemp();
    if (len(pwd) == 0) {
        return;
    }

    logf("PdfEncryptDoIt: encrypting '%s' to '%s' with AES-256\n", srcPath, destPath);

    // equivalent of: clean -E aes-256 -U <pwd> -O <pwd> input output
    char* pwdZ = CStrTemp(pwd);
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        MessageBoxWarning(hwnd, Tr("Couldn't create a temporary file beside the PDF."), Tr("Save PDF"));
        return;
    }
    char* argv[] = {(char*)"clean", (char*)"-E",       (char*)"aes-256",         (char*)"-U", pwdZ, (char*)"-O",
                    pwdZ,           CStrTemp(srcPath), CStrTemp(output.tempPath)};
    int argc = 9;

    fz_set_optind(0);
    int res = pdfclean_main(argc, argv);
    if (res == 0 && !output.Commit()) {
        res = 1;
    }
    if (res == 0) {
        logf("PdfEncryptDoIt: encrypted successfully\n");
        MainWindow* w = win;
        TempStr path = str::DupTemp(destPath);
        Close();
        LoadArgs args(path, w);
        StartLoadDocument(&args);
    } else {
        logf("PdfEncryptDoIt: pdfclean_main failed with %d\n", res);
        MessageBoxWarning(hwnd, StrL("Failed to encrypt PDF file."), Tr("Encrypt PDF"));
    }
}

bool PdfEncryptDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Encrypt PDF"))) {
        return false;
    }
    AddPathRow();
    AddDestRow(MakeUniqueFilePathTemp(srcPath), L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf");
    passwordEdit = AddLabeledEdit(Tr("Password:"), {}, true);
    AddButtonsRow(Tr("Encrypt PDF"));
    FinishDialog(passwordEdit);
    passwordEdit->onTextChanged = MkMethod0<PdfEncryptDialog, &PdfEncryptDialog::UpdateButton>(this);
    UpdateButton();
    return true;
}

void ShowPdfEncryptDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    if (!IsPdfDoc(tab)) {
        return;
    }
    EngineBase* engine = tab->GetEngine();
    if (EngineMupdfIsEncrypted(engine)) {
        logf("ShowPdfEncryptDialog: '%s' is already encrypted, skipping\n", tab->filePath);
        return;
    }
    logf("ShowPdfEncryptDialog: opening for '%s'\n", tab->filePath);

    auto* dlg = new PdfEncryptDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Decrypt PDF dialog ---

struct PdfDecryptDialog : PdfToolDialog {
    // the password the document was opened with
    Str password;

    ~PdfDecryptDialog() override;
    bool Create(MainWindow* win, WindowTab* tab, Str pwd);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

PdfDecryptDialog::~PdfDecryptDialog() {
    str::FreePtr(&password);
}

void PdfDecryptDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }

    logf("PdfDecryptDoIt: decrypting '%s' to '%s', password len: %d\n", srcPath, destPath, len(password));

    // equivalent of: clean -p <pwd> -D input output
    // -p provides the password to open the encrypted input, -D removes encryption from output
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        MessageBoxWarning(hwnd, Tr("Couldn't create a temporary file beside the PDF."), Tr("Save PDF"));
        return;
    }
    char* argv[] = {(char*)"clean", (char*)"-p",       CStrTemp(password),
                    (char*)"-D",    CStrTemp(srcPath), CStrTemp(output.tempPath)};
    int argc = 6;

    fz_set_optind(0);
    int res = pdfclean_main(argc, argv);
    if (res == 0 && !output.Commit()) {
        res = 1;
    }
    if (res == 0) {
        logf("PdfDecryptDoIt: decrypted successfully\n");
        MainWindow* w = win;
        TempStr path = str::DupTemp(destPath);
        Close();
        LoadArgs args(path, w);
        StartLoadDocument(&args);
    } else {
        logf("PdfDecryptDoIt: pdfclean_main failed with %d, src: '%s', password len: %d\n", res, srcPath,
             len(password));
        MessageBoxWarning(hwnd, StrL("Failed to decrypt PDF file."), Tr("Decrypt PDF"));
    }
}

bool PdfDecryptDialog::Create(MainWindow* w, WindowTab* tab, Str pwd) {
    if (!CreateToolDialog(w, tab, Tr("Decrypt PDF"))) {
        return false;
    }
    password = str::Dup(pwd);
    AddPathRow();
    AddDestRow(MakeUniqueFilePathTemp(srcPath), L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf");
    AddButtonsRow(Tr("Decrypt PDF"));
    FinishDialog(destEdit);
    return true;
}

void ShowPdfDecryptDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    if (!IsPdfDoc(tab)) {
        return;
    }
    EngineBase* engine = tab->GetEngine();
    if (!EngineMupdfIsEncrypted(engine)) {
        logf("ShowPdfDecryptDialog: '%s' is not encrypted, skipping\n", tab->filePath);
        return;
    }
    Str pwd = EngineMupdfGetPassword(engine);
    if (len(pwd) == 0) {
        logf("ShowPdfDecryptDialog: '%s' is encrypted but no password available\n", tab->filePath);
        return;
    }
    logf("ShowPdfDecryptDialog: opening for '%s', password len: %d\n", tab->filePath, len(pwd));

    auto* dlg = new PdfDecryptDialog();
    if (!dlg->Create(win, tab, pwd)) {
        delete dlg;
    }
}

// --- Convert to PDF dialog (comics, image folders, single images; issue #4118) ---

// Default destination: same path with .pdf extension, made unique if the file
// already exists (e.g. comic.cbz → comic.pdf, or comic.1.pdf if taken).
static TempStr DefaultPdfDestPathTemp(Str srcPath) {
    if (len(srcPath) == 0) {
        return {};
    }
    TempStr noExt = path::GetPathNoExtTemp(srcPath);
    if (len(noExt) == 0) {
        noExt = str::DupTemp(srcPath);
    }
    TempStr pdfPath = str::JoinTemp(noExt, StrL(".pdf"));
    return MakeUniqueFilePathTemp(pdfPath);
}

struct ConvertToPdfDialog : PdfToolDialog {
    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

// Same conversion as File → Convert to PDF (issue #4118).
static bool ConvertImageCollectionToPdf(EngineBase* engine, Str destPath) {
    if (!engine || !engine->isImageCollection || len(destPath) == 0) {
        return false;
    }

    TempStr producer = fmt("SwiftLeaf %s", currentVersion);
    PdfCreator::SetProducerName(producer);
    // Formats PDF cannot re-wrap (WebP, JXL, HEIC, AVIF, TGA, …): decode via
    // the same codecs we use for viewing, then PNG + zopfli before embed.
    auto toOptimizedPng = [](Str data) -> Str {
        Pixmap* px = PixmapFromData(data);
        if (!px) {
            logf("ConvertToPdf: decode-to-pixmap failed (%d bytes)\n", len(data));
            return {};
        }
        Str png = EncodeAndOptimizePngFromPixmap(px);
        FreePixmap(px);
        return png;
    };
    return PdfCreator::SaveImageCollectionAsPdf(destPath, engine, toOptimizedPng);
}

TempStr ConvertImageCollectionToPdfResultTemp(Str srcPath, Str destPath, int* exitCodeOut) {
    auto finish = [&](int code, TempStr s) -> TempStr {
        if (exitCodeOut) {
            *exitCodeOut = code;
        }
        return s;
    };
    if (len(srcPath) == 0 || len(destPath) == 0) {
        return finish(1, str::DupTemp(StrL("ERROR bad-args")));
    }
    EngineBase* engine = CreateEngineFromFile(srcPath, nullptr, false);
    if (!engine) {
        return finish(1, str::DupTemp(StrL("ERROR engine-create-failed")));
    }
    bool ok = ConvertImageCollectionToPdf(engine, destPath);
    SafeEngineRelease(&engine);
    if (!ok) {
        return finish(1, str::DupTemp(StrL("ERROR convert-failed")));
    }
    return finish(0, str::DupTemp(StrL("OK")));
}

TempStr ExtractPdfPagesResultTemp(Str destPath, Str pagesSpec, int annotsOnly, int* exitCodeOut) {
    auto finish = [&](int code, TempStr s) -> TempStr {
        if (exitCodeOut) {
            *exitCodeOut = code;
        }
        return s;
    };
    if (len(gWindows) == 0) {
        return finish(2, str::DupTemp(StrL("NOTREADY no-window")));
    }
    MainWindow* win = gWindows[0];
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    EngineBase* engine = tab ? tab->GetEngine() : nullptr;
    if (!engine || len(tab->filePath) == 0) {
        return finish(2, str::DupTemp(StrL("NOTREADY no-engine")));
    }
    if (len(destPath) == 0 || len(pagesSpec) == 0) {
        return finish(1, str::DupTemp(StrL("ERROR bad-args")));
    }
    int pageCount = engine->PageCount();
    Vec<int> parsedPages;
    if (!ParseDeletePages(pagesSpec, pageCount, parsedPages)) {
        return finish(1, str::DupTemp(StrL("ERROR bad-pages")));
    }
    if (annotsOnly) {
        if (!KeepOnlyPagesWithAnnotations(engine, parsedPages)) {
            return finish(1, str::DupTemp(StrL("ERROR annots")));
        }
        if (len(parsedPages) == 0) {
            return finish(1, str::DupTemp(StrL("ERROR no-annot-pages")));
        }
    }
    TempStr pageRange = FormatPageRangeTemp(parsedPages);
    Str inputPath = tab->filePath;
    TempStr tmpPath;
    if (EngineHasUnsavedAnnotations(engine)) {
        tmpPath = GetTempFilePathTemp(StrL("extract-pages"));
        if (len(tmpPath) == 0 || !EngineMupdfSaveCopy(engine, tmpPath)) {
            return finish(1, str::DupTemp(StrL("ERROR save-copy")));
        }
        inputPath = tmpPath;
    }
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        file::Delete(tmpPath);
        return finish(1, str::DupTemp(StrL("ERROR staging")));
    }
    char* argv[] = {(char*)"clean",     (char*)"-gggg", (char*)"-e", (char*)"100",        (char*)"-f",
                    (char*)"-i",        (char*)"-t",    (char*)"-Z", CStrTemp(inputPath), CStrTemp(output.tempPath),
                    CStrTemp(pageRange)};
    fz_set_optind(0);
    int res = pdfclean_main(11, argv);
    if (res == 0 && !output.Commit()) {
        res = 1;
    }
    if (tmpPath) {
        file::Delete(tmpPath);
    }
    if (res != 0) {
        return finish(1, str::DupTemp(StrL("ERROR pdfclean")));
    }
    return finish(0, str::DupTemp(StrL("OK")));
}

void ConvertToPdfDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    DisplayModel* dm = win->AsFixed();
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine || !engine->isImageCollection) {
        MessageBoxWarning(hwnd, Tr("Failed to save a file"), Tr("Convert to PDF"));
        return;
    }

    logf("ConvertToPdf: converting '%s' to '%s'\n", srcPath, destPath);
    bool ok = ConvertImageCollectionToPdf(engine, destPath);
    if (ok) {
        logf("ConvertToPdf: converted successfully\n");
        MainWindow* w = win;
        TempStr path = str::DupTemp(destPath);
        Close();
        LoadArgs args(path, w);
        StartLoadDocument(&args);
    } else {
        logf("ConvertToPdf: SaveImageCollectionAsPdf failed\n");
        MessageBoxWarning(hwnd, Tr("Failed to save a file"), Tr("Convert to PDF"));
    }
}

bool ConvertToPdfDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Convert to PDF"))) {
        return false;
    }
    AddPathRow();
    AddDestRow(DefaultPdfDestPathTemp(srcPath), L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf");
    AddButtonsRow(Tr("Convert to PDF"));
    FinishDialog(destEdit);
    return true;
}

void ShowConvertToPdfDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    EngineBase* engine = tab->GetEngine();
    if (!engine || !engine->isImageCollection) {
        return;
    }
    if (!engine->AllowsPrinting()) {
        return;
    }
    logf("ShowConvertToPdfDialog: opening for '%s'\n", tab->filePath);

    auto* dlg = new ConvertToPdfDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Convert PDF to Images dialog (issue #5991) ---

constexpr int kConvertPdfToImagesDpi = 150;
constexpr i64 kMaxSaveSelectionPixels = 100LL * 1000 * 1000;
constexpr int kMaxSaveSelectionSide = 16384;
constexpr int kSaveSelectionDefaultDpi = 300;
constexpr int kMaxImageDpi = 9600;

static const int kImageDpiChoices[] = {72, 96, 150, 300, 600, 1200};

static float SaveSelectionZoom(EngineBase* engine, float dpi) {
    float fileDpi = engine->fileDPI;
    if (fileDpi <= 0) {
        fileDpi = 72.0f;
    }
    if (dpi < 1) {
        dpi = (float)kSaveSelectionDefaultDpi;
    }
    return dpi / fileDpi;
}

static bool SaveSelectionSizeOk(int w, int h) {
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (w > kMaxSaveSelectionSide || h > kMaxSaveSelectionSide) {
        return false;
    }
    i64 pixels = (i64)w * (i64)h;
    return pixels <= kMaxSaveSelectionPixels;
}

static bool EstimateSelectionPx(RectF rect, float zoom, int& w, int& h) {
    w = (int)floorf((rect.dx * zoom) + 0.5f);
    h = (int)floorf((rect.dy * zoom) + 0.5f);
    return w > 0 && h > 0;
}

// editable combo listing kImageDpiChoices; any other DPI can be typed in
static DropDown* NewDpiCombo(HWND parent, PlatformFont* font, int dpi) {
    DropDown::CreateArgs args;
    args.parent = parent;
    args.font = font;
    args.isRtl = IsUIRtl();
    args.isEditable = true;
    auto* dd = new DropDown();
    dd->Create(args);

    StrVec items;
    for (int choice : kImageDpiChoices) {
        items.Append(fmt("%d", choice));
    }
    dd->SetItems(items);
    dd->SetText(fmt("%d", dpi));
    dd->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
    return dd;
}

// 0 if not a usable DPI
static int DpiFromComboText(DropDown* dd) {
    Str s = dd->GetTextTemp();
    str::TrimWsBoth(s);
    int dpi = ParseInt(s);
    if (dpi < 1 || dpi > kMaxImageDpi) {
        return 0;
    }
    return dpi;
}

// on CBN_SELCHANGE the edit still has the previous text, so read the list
static int DpiFromComboSel(DropDown* dd) {
    int idx = CbGetCurrentSelection(dd);
    if (idx < 0 || idx >= dimofi(kImageDpiChoices)) {
        return DpiFromComboText(dd);
    }
    return kImageDpiChoices[idx];
}

// PNG / JPEG / BMP, same core formats as the Save Image dialog
struct ConvertImageFormat {
    Str label;
    Str ext; // including the dot, e.g. ".png"
    WStr mime;
    WStr defExt; // OPENFILENAME lpstrDefExt, no dot
};

static const ConvertImageFormat kConvertImageFormats[] = {
    {StrL("PNG"), StrL(".png"), L"image/png", L"png"},
    {StrL("JPEG"), StrL(".jpg"), L"image/jpeg", L"jpg"},
    {StrL("BMP"), StrL(".bmp"), L"image/bmp", L"bmp"},
};

static int ConvertImageFormatCount() {
    return dimofi(kConvertImageFormats);
}

static int ConvertImageFormatIdxFromPath(Str path) {
    if (str::EndsWithI(path, StrL(".jpg")) || str::EndsWithI(path, StrL(".jpeg"))) {
        return 1;
    }
    if (str::EndsWithI(path, StrL(".bmp"))) {
        return 2;
    }
    return 0; // PNG
}

static bool IsSupportedConvertImageExt(Str path) {
    return str::EndsWithI(path, StrL(".png")) || str::EndsWithI(path, StrL(".jpg")) ||
           str::EndsWithI(path, StrL(".jpeg")) || str::EndsWithI(path, StrL(".bmp"));
}

static bool PathHasPagePlaceholder(Str path) {
    return str::ContainsI(path, StrL("<N>"));
}

// dest template: if <N> is missing and we'll write more than one file, insert
// it before the extension so pages don't overwrite each other
static TempStr EnsurePagePlaceholderTemp(Str path, bool multiPage) {
    if (PathHasPagePlaceholder(path) || !multiPage) {
        return str::DupTemp(path);
    }
    TempStr ext = path::GetExtTemp(path);
    TempStr noExt = path::GetPathNoExtTemp(path);
    if (len(ext) == 0) {
        return str::JoinTemp(noExt, StrL("-<N>.png"));
    }
    return str::JoinTemp(noExt, StrL("-<N>"), ext);
}

static TempStr WithDefaultImageExtTemp(Str path) {
    if (IsSupportedConvertImageExt(path)) {
        return str::DupTemp(path);
    }
    TempStr ext = path::GetExtTemp(path);
    if (ext) {
        // has an extension we don't write
        return {};
    }
    return str::JoinTemp(path, StrL(".png"));
}

static TempStr ReplacePagePlaceholderTemp(Str path, int pageNo) {
    TempStr n = fmt("%d", pageNo);
    TempStr s = str::ReplaceTemp(path, StrL("<N>"), n);
    if (str::Contains(s, StrL("<n>"))) {
        s = str::ReplaceTemp(s, StrL("<n>"), n);
    }
    return s;
}

static bool GetImageEncoderForPath(Str path, CLSID* encOut) {
    if (!IsSupportedConvertImageExt(path)) {
        return false;
    }
    int idx = ConvertImageFormatIdxFromPath(path);
    CLSID enc = GetGdiPlusEncoderClsid(kConvertImageFormats[idx].mime);
    if (enc.Data1 == 0 && enc.Data2 == 0 && enc.Data3 == 0) {
        return false;
    }
    *encOut = enc;
    return true;
}

static bool SavePixmapAsImageFile(Pixmap* px, Str path) {
    if (!px || len(path) == 0) {
        return false;
    }
    CLSID enc{};
    if (!GetImageEncoderForPath(path, &enc)) {
        return false;
    }
    Pixmap* converted = nullptr;
    Pixmap* use = px;
    if (px->format == PixmapFormat::Native) {
        converted = PixmapCopyAs32bppDIB(px);
        if (!converted) {
            return false;
        }
        use = converted;
    }
    Gdiplus::Bitmap* bmp = WrapPixmapGdiplus(use);
    bool ok = false;
    if (bmp) {
        bmp->SetResolution(use->xres, use->yres);
        WCHAR* pathW = CWStrTemp(path);
        ok = bmp->Save(pathW, &enc, nullptr) == Gdiplus::Ok;
        delete bmp;
    } else if (use->hbmp) {
        Gdiplus::Bitmap gbmp(use->hbmp, nullptr);
        gbmp.SetResolution(use->xres, use->yres);
        WCHAR* pathW = CWStrTemp(path);
        ok = gbmp.Save(pathW, &enc, nullptr) == Gdiplus::Ok;
    }
    FreePixmap(converted);
    if (!ok) {
        file::Delete(path);
    }
    return ok;
}

static bool PageFitsDpi(EngineBase* engine, int pageNo, int dpi) {
    float zoom = SaveSelectionZoom(engine, (float)dpi);
    int w = 0;
    int h = 0;
    EstimateSelectionPx(engine->PageMediabox(pageNo), zoom, w, h);
    return SaveSelectionSizeOk(w, h);
}

static bool PagesFitDpi(EngineBase* engine, const Vec<int>& pages, int dpi) {
    for (int pageNo : pages) {
        if (!PageFitsDpi(engine, pageNo, dpi)) {
            return false;
        }
    }
    return true;
}

// render each page at dpi and write it to templatePath with <N> replaced by
// the 1-based page number. Returns how many files were written.
static int ConvertPagesToImages(EngineBase* engine, int rotation, Str templatePath, const Vec<int>& pages, int dpi,
                                Str* firstPathOwnedOut) {
    if (firstPathOwnedOut) {
        *firstPathOwnedOut = {};
    }
    if (!engine || len(pages) == 0 || len(templatePath) == 0) {
        return 0;
    }
    TempStr withExt = WithDefaultImageExtTemp(templatePath);
    if (len(withExt) == 0) {
        return 0;
    }
    TempStr templ = EnsurePagePlaceholderTemp(withExt, len(pages) > 1);
    float zoom = SaveSelectionZoom(engine, (float)dpi);
    int nOk = 0;
    StrVec pngs;
    for (int pageNo : pages) {
        if (!PageFitsDpi(engine, pageNo, dpi)) {
            logf("ConvertPagesToImages: page %d at %d DPI is too large\n", pageNo, dpi);
            continue;
        }
        TempStr dest = ReplacePagePlaceholderTemp(templ, pageNo);
        RenderPageArgs args(pageNo, zoom, rotation);
        Pixmap* px = engine->RenderPage(args);
        if (!px) {
            logf("ConvertPagesToImages: RenderPage failed for page %d\n", pageNo);
            continue;
        }
        px->xres = (float)dpi;
        px->yres = (float)dpi;
        bool ok = SavePixmapAsImageFile(px, dest);
        FreePixmap(px);
        if (!ok) {
            logf("ConvertPagesToImages: save failed for '%s'\n", dest);
            continue;
        }
        if (nOk == 0 && firstPathOwnedOut) {
            *firstPathOwnedOut = str::Dup(dest);
        }
        if (str::EndsWithI(dest, StrL(".png"))) {
            pngs.Append(dest);
        }
        nOk++;
    }
    OptimizePngFilesAsync(pngs);
    return nOk;
}

static void CollectAllPages(int pageCount, Vec<int>& pages) {
    VecReset(pages);
    for (int i = 1; i <= pageCount; i++) {
        VecAppend(pages, i);
    }
}

TempStr ConvertPagesToImagesResultTemp(Str templatePath, Str pagesSpec, int dpi, int* exitCodeOut) {
    auto finish = [&](int code, TempStr s) -> TempStr {
        if (exitCodeOut) {
            *exitCodeOut = code;
        }
        return s;
    };
    if (len(gWindows) == 0) {
        return finish(2, str::DupTemp(StrL("NOTREADY no-window")));
    }
    MainWindow* win = gWindows[0];
    DisplayModel* dm = win ? win->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine) {
        return finish(2, str::DupTemp(StrL("NOTREADY no-engine")));
    }
    int pageCount = engine->PageCount();
    Vec<int> pages;
    if (str::EqI(pagesSpec, StrL("current"))) {
        int cur = dm->CurrentPageNo();
        if (cur < 1 || cur > pageCount) {
            return finish(1, str::DupTemp(StrL("ERROR bad-current")));
        }
        VecAppend(pages, cur);
    } else if (str::EqI(pagesSpec, StrL("all"))) {
        CollectAllPages(pageCount, pages);
    } else if (!ParseDeletePages(pagesSpec, pageCount, pages)) {
        return finish(1, str::DupTemp(StrL("ERROR bad-pages")));
    }
    if (dpi < 1) {
        dpi = kConvertPdfToImagesDpi;
    }
    Str firstPath;
    int n = ConvertPagesToImages(engine, dm->GetRotation(), templatePath, pages, dpi, &firstPath);
    if (n == 0) {
        str::Free(firstPath);
        return finish(1, str::DupTemp(StrL("ERROR convert-failed")));
    }
    TempStr res = fmt("OK n=%d first=%s", n, firstPath);
    str::Free(firstPath);
    return finish(0, res);
}

struct ConvertPdfToImagesDialog : PdfToolDialog {
    int pageCount = 0;
    Checkbox* radioCurrent = nullptr;
    Checkbox* radioAll = nullptr;
    Checkbox* radioCustom = nullptr;
    Edit* pagesEdit = nullptr;
    DropDown* dropFormat = nullptr;
    DropDown* dropDpi = nullptr;
    int dpi = kConvertPdfToImagesDpi; // 0 if what's typed is not a DPI
    bool syncingFormat = false;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void OnBrowseTemplate(VirtMouseEvent* ev = nullptr);
    void UpdateButton();
    void OnPagesModeChanged();
    void OnFormatChanged();
    void OnDpiPicked();
    void OnDpiTyped();
    void SetDestExtFromFormat();
    void SyncFormatFromPath(Str path);
    int SelectedFormatIdx() const;
};

static Checkbox* NewPagesRadio(HWND parent, PlatformFont* font, Str text, bool groupStart, bool checked) {
    Checkbox::CreateArgs args;
    args.parent = parent;
    args.text = text;
    args.font = font;
    args.isRtl = IsUIRtl();
    args.isRadio = true;
    args.isGroupStart = groupStart;
    args.initialState = checked ? Checkbox::State::Checked : Checkbox::State::Unchecked;
    auto* c = new Checkbox();
    c->Create(args);
    c->SetColors(ThemeWindowTextColor(), DarkModeDialogBgColor());
    return c;
}

int ConvertPdfToImagesDialog::SelectedFormatIdx() const {
    if (!dropFormat) {
        return 0;
    }
    int idx = CbGetCurrentSelection(dropFormat);
    if (idx < 0 || idx >= ConvertImageFormatCount()) {
        return 0;
    }
    return idx;
}

void ConvertPdfToImagesDialog::SetDestExtFromFormat() {
    if (!destEdit) {
        return;
    }
    TempStr dest = destEdit->GetTextTemp();
    if (len(dest) == 0) {
        return;
    }
    Str ext = kConvertImageFormats[SelectedFormatIdx()].ext;
    TempStr noExt = path::GetPathNoExtTemp(dest);
    TempStr newDest = str::JoinTemp(noExt, ext);
    if (str::EqI(dest, newDest)) {
        return;
    }
    destEdit->SetText(newDest);
}

void ConvertPdfToImagesDialog::SyncFormatFromPath(Str path) {
    if (!dropFormat) {
        return;
    }
    int idx = ConvertImageFormatIdxFromPath(path);
    if (idx == CbGetCurrentSelection(dropFormat)) {
        return;
    }
    syncingFormat = true;
    CbSetCurrentSelection(dropFormat, idx);
    syncingFormat = false;
}

void ConvertPdfToImagesDialog::OnFormatChanged() {
    if (syncingFormat) {
        return;
    }
    SetDestExtFromFormat();
}

void ConvertPdfToImagesDialog::OnDpiPicked() {
    dpi = DpiFromComboSel(dropDpi);
    UpdateButton();
}

void ConvertPdfToImagesDialog::OnDpiTyped() {
    dpi = DpiFromComboText(dropDpi);
    UpdateButton();
}

void ConvertPdfToImagesDialog::OnPagesModeChanged() {
    bool custom = radioCustom && radioCustom->IsChecked();
    if (pagesEdit) {
        pagesEdit->SetIsEnabled(custom);
    }
    UpdateButton();
}

void ConvertPdfToImagesDialog::UpdateButton() {
    if (!actionBtn) {
        return;
    }
    TempStr dest = destEdit ? destEdit->GetTextTemp() : TempStr{};
    bool destOk = len(dest) > 0;
    bool pagesOk = true;
    if (radioCustom && radioCustom->IsChecked()) {
        TempStr pages = pagesEdit ? pagesEdit->GetTextTemp() : TempStr{};
        Vec<int> parsed;
        pagesOk = ParseDeletePages(pages, pageCount, parsed);
    }
    bool valid = destOk && pagesOk && dpi > 0;
    if (valid == actionBtn->HasFlag(vwfEnabled)) {
        return;
    }
    actionBtn->SetFlag(vwfEnabled, valid);
    actionBtn->Invalidate();
}

void ConvertPdfToImagesDialog::OnBrowseTemplate(VirtMouseEvent*) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    TempStr current = destEdit->GetTextTemp();
    // < and > are illegal in Windows filenames; show {N} in the save dialog
    TempStr shown = str::ReplaceTemp(current, StrL("<N>"), StrL("{N}"));
    if (str::Contains(shown, StrL("<n>"))) {
        shown = str::ReplaceTemp(shown, StrL("<n>"), StrL("{N}"));
    }
    wstr::BufSet(WStr(dstFileName, MAX_PATH), ToWStrTemp(shown));

    int fmtIdx = SelectedFormatIdx();
    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = L"PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0BMP\0*.bmp\0All Files\0*.*\0";
    ofn.nFilterIndex = (DWORD)(fmtIdx + 1);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = kConvertImageFormats[fmtIdx].defExt.s;

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }
    TempStr picked = ToUtf8Temp(dstFileName);
    if (str::Contains(picked, StrL("{N}"))) {
        picked = str::ReplaceTemp(picked, StrL("{N}"), StrL("<N>"));
    } else if (!PathHasPagePlaceholder(picked)) {
        picked = EnsurePagePlaceholderTemp(picked, true);
    }
    destEdit->SetText(picked);
    int filterIdx = (int)ofn.nFilterIndex - 1;
    if (filterIdx >= 0 && filterIdx < ConvertImageFormatCount()) {
        syncingFormat = true;
        CbSetCurrentSelection(dropFormat, filterIdx);
        syncingFormat = false;
        SetDestExtFromFormat();
    } else {
        SyncFormatFromPath(picked);
    }
}

void ConvertPdfToImagesDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }
    DisplayModel* dm = win ? win->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine) {
        MessageBoxWarning(hwnd, StrL("Failed to convert PDF to images."), Tr("Convert PDF to Images"));
        return;
    }

    Vec<int> pages;
    if (radioCurrent && radioCurrent->IsChecked()) {
        int cur = dm->CurrentPageNo();
        if (cur < 1 || cur > pageCount) {
            return;
        }
        VecAppend(pages, cur);
    } else if (radioAll && radioAll->IsChecked()) {
        CollectAllPages(pageCount, pages);
    } else {
        TempStr custom = pagesEdit ? pagesEdit->GetTextTemp() : TempStr{};
        if (!ParseDeletePages(custom, pageCount, pages)) {
            return;
        }
    }

    // the box may have been filled without a change notification
    dpi = DpiFromComboText(dropDpi);
    if (dpi < 1) {
        return;
    }
    if (!PagesFitDpi(engine, pages, dpi)) {
        MessageBoxWarning(hwnd, Tr("Too large for this DPI"), Tr("Convert PDF to Images"));
        return;
    }

    logf("ConvertPdfToImages: '%s' -> '%s', %d page(s), %d DPI\n", srcPath, destPath, len(pages), dpi);

    HCURSOR prev = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    Str firstPath;
    int nOk = ConvertPagesToImages(engine, dm->GetRotation(), destPath, pages, dpi, &firstPath);
    SetCursor(prev);

    if (nOk == 0) {
        str::Free(firstPath);
        MessageBoxWarning(hwnd, StrL("Failed to convert PDF to images."), Tr("Convert PDF to Images"));
        return;
    }
    logf("ConvertPdfToImages: wrote %d of %d file(s)\n", nOk, len(pages));
    TempStr openPath = str::DupTemp(firstPath);
    str::Free(firstPath);
    Close();
    OpenPathInDefaultFileManager(openPath);
}

bool ConvertPdfToImagesDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Convert PDF to Images"))) {
        return false;
    }
    pageCount = w->ctrl ? w->ctrl->PageCount() : 1;
    AddPathRow();

    TempStr noExt = path::GetPathNoExtTemp(srcPath);
    TempStr pngPath = str::JoinTemp(noExt, StrL("-<N>.png"));

    HBox* destRow = AddRow();
    destRow->gap = font->averageCharWidth;

    Edit::CreateArgs dargs;
    dargs.parent = hwnd;
    dargs.withBorder = true;
    dargs.font = font;
    dargs.text = pngPath;
    dargs.isRtl = IsUIRtl();
    destEdit = new Edit();
    destEdit->Create(dargs);
    destRow->AddChild(destEdit, 1);

    {
        auto* dd = new DropDown();
        DropDown::CreateArgs ddargs;
        ddargs.parent = hwnd;
        ddargs.font = font;
        ddargs.isRtl = IsUIRtl();
        dd->Create(ddargs);
        StrVec items;
        for (int i = 0; i < ConvertImageFormatCount(); i++) {
            items.Append(kConvertImageFormats[i].label);
        }
        dd->SetItems(items);
        CbSetCurrentSelection(dd, 0);
        dd->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
        dd->onSelectionChanged = MkMethod0<ConvertPdfToImagesDialog, &ConvertPdfToImagesDialog::OnFormatChanged>(this);
        dropFormat = dd;
        destRow->AddChild(dropFormat);
    }

    browseBtn = NewButton(StrL("..."), false);
    browseBtn->onClick =
        MkMethod1<ConvertPdfToImagesDialog, VirtMouseEvent*, &ConvertPdfToImagesDialog::OnBrowseTemplate>(this);
    destRow->AddChild(browseBtn);

    if (pathLabel) {
        pathLabel->padding.left = destEdit->GetLeftTextMargin();
    }

    HBox* row = AddRow();
    row->gap = font->averageCharWidth;
    row->AddChild(NewVirtText({.s = Tr("Pages:"), .font = font, .isRtl = IsUIRtl()}));

    radioCurrent = NewPagesRadio(hwnd, font, Tr("Current"), true, false);
    radioCurrent->onStateChanged =
        MkMethod0<ConvertPdfToImagesDialog, &ConvertPdfToImagesDialog::OnPagesModeChanged>(this);
    row->AddChild(radioCurrent);

    radioAll = NewPagesRadio(hwnd, font, Tr("All"), false, true);
    radioAll->onStateChanged = MkMethod0<ConvertPdfToImagesDialog, &ConvertPdfToImagesDialog::OnPagesModeChanged>(this);
    row->AddChild(radioAll);

    radioCustom = NewPagesRadio(hwnd, font, Tr("Custom"), false, false);
    radioCustom->onStateChanged =
        MkMethod0<ConvertPdfToImagesDialog, &ConvertPdfToImagesDialog::OnPagesModeChanged>(this);
    row->AddChild(radioCustom);

    Edit::CreateArgs eargs;
    eargs.parent = hwnd;
    eargs.withBorder = true;
    eargs.font = font;
    eargs.text = fmt("1-%d", pageCount);
    eargs.isRtl = IsUIRtl();
    pagesEdit = new Edit();
    pagesEdit->Create(eargs);
    pagesEdit->SetIsEnabled(false);
    row->AddChild(pagesEdit, 1);

    HBox* dpiRow = AddRow();
    dpiRow->gap = font->averageCharWidth;
    dpiRow->AddChild(NewVirtText({.s = Tr("Resolution (DPI):"), .font = font, .isRtl = IsUIRtl()}));

    dropDpi = NewDpiCombo(hwnd, font, dpi);
    dropDpi->onSelectionChanged = MkMethod0<ConvertPdfToImagesDialog, &ConvertPdfToImagesDialog::OnDpiPicked>(this);
    dropDpi->onTextChanged = MkMethod0<ConvertPdfToImagesDialog, &ConvertPdfToImagesDialog::OnDpiTyped>(this);
    dpiRow->AddChild(dropDpi);

    AddButtonsRow(Tr("Convert"), Tr("Use <N> for the page number"));
    FinishDialog(destEdit);

    destEdit->onTextChanged = MkMethod0<ConvertPdfToImagesDialog, &ConvertPdfToImagesDialog::UpdateButton>(this);
    pagesEdit->onTextChanged = MkMethod0<ConvertPdfToImagesDialog, &ConvertPdfToImagesDialog::UpdateButton>(this);
    UpdateButton();
    return true;
}

void ShowConvertPdfToImagesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0) {
        return;
    }
    if (!IsPdfDoc(tab)) {
        return;
    }
    logf("ShowConvertPdfToImagesDialog: opening for '%s'\n", tab->filePath);

    auto* dlg = new ConvertPdfToImagesDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// rectangular selection → PNG / JPEG / BMP at a chosen DPI, independent of
// the current zoom (issue #6127)
static Kind kNotifSaveSelectionAsImage = "notifSaveSelectionAsImage";

static Pixmap* RenderSelectionPixmap(EngineBase* engine, int rotation, int pageNo, RectF rect, float dpi) {
    if (!engine || rect.IsEmpty()) {
        return nullptr;
    }
    if (pageNo < 1 || pageNo > engine->PageCount()) {
        return nullptr;
    }
    float zoom = SaveSelectionZoom(engine, dpi);
    int estW = 0;
    int estH = 0;
    if (!EstimateSelectionPx(rect, zoom, estW, estH) || !SaveSelectionSizeOk(estW, estH)) {
        logf("RenderSelectionPixmap: %dx%d at %.0f DPI is too large\n", estW, estH, dpi);
        return nullptr;
    }
    RenderPageArgs args(pageNo, zoom, rotation, &rect, RenderTarget::Export);
    Pixmap* px = engine->RenderPage(args);
    if (!px) {
        logf("RenderSelectionPixmap: RenderPage failed page %d\n", pageNo);
        return nullptr;
    }
    px->xres = dpi;
    px->yres = dpi;
    return PixmapToBgra(px);
}

static bool WriteSelectionPixmap(Pixmap* px, Str destPath) {
    if (!px || len(destPath) == 0) {
        return false;
    }
    bool ok = false;
    if (str::EndsWithI(destPath, StrL(".png"))) {
        // lodepng (no pHYs, so EngineImages displays 1:1) now, zopfli in the
        // background: a 300 dpi page takes it many seconds
        Str png = EncodePngFromPixmap(px);
        ok = len(png) > 0 && file::WriteFile(destPath, png);
        str::Free(png);
        if (!ok) {
            file::Delete(destPath);
        } else {
            OptimizePngFileAsync(destPath);
        }
    } else {
        ok = SavePixmapAsImageFile(px, destPath);
    }
    return ok;
}

static bool SavePageRectAsImage(EngineBase* engine, int rotation, int pageNo, RectF rect, Str destPath, float dpi,
                                int* widthOut, int* heightOut) {
    if (widthOut) {
        *widthOut = 0;
    }
    if (heightOut) {
        *heightOut = 0;
    }
    Pixmap* px = RenderSelectionPixmap(engine, rotation, pageNo, rect, dpi);
    if (!px) {
        return false;
    }
    bool ok = WriteSelectionPixmap(px, destPath);
    if (ok) {
        if (widthOut) {
            *widthOut = px->width;
        }
        if (heightOut) {
            *heightOut = px->height;
        }
    }
    FreePixmap(px);
    return ok;
}

struct SaveSelImgWork {
    Pixmap* px = nullptr;
    Str destPath;
    HWND hwndCanvas = nullptr;
    MainWindow* win = nullptr;
    bool ok = false;
};

static void FinishSaveSelImg(SaveSelImgWork* d) {
    if (IsWindow(d->hwndCanvas)) {
        RemoveNotificationsForGroup(d->hwndCanvas, kNotifSaveSelectionAsImage);
    }
    if (d->ok) {
        logf("SaveSelectionAsImage: wrote '%s'\n", d->destPath);
        if (IsMainWindowValidAndNotClosing(d->win)) {
            LoadArgs args(d->destPath, d->win);
            StartLoadDocument(&args);
        }
    } else {
        HWND parent = nullptr;
        if (IsMainWindowValidAndNotClosing(d->win)) {
            parent = d->win->hwndFrame;
        }
        MessageBoxWarning(parent, StrL("Failed to save the selection as an image."), Tr("Save Selection As Image"));
    }
    str::Free(d->destPath);
    delete d;
}

static void SaveSelImgThread(SaveSelImgWork* d) {
    d->ok = WriteSelectionPixmap(d->px, d->destPath);
    FreePixmap(d->px);
    d->px = nullptr;
    uitask::Post(MkFunc0(FinishSaveSelImg, d), "FinishSaveSelImg");
}

TempStr SaveSelectionAsImageResultTemp(Str destPath, int dpi, int pageNo, int x, int y, int dx, int dy,
                                       int* exitCodeOut) {
    auto finish = [&](int code, TempStr s) -> TempStr {
        if (exitCodeOut) {
            *exitCodeOut = code;
        }
        return s;
    };
    if (len(gWindows) == 0) {
        return finish(2, str::DupTemp(StrL("NOTREADY no-window")));
    }
    MainWindow* win = gWindows[0];
    DisplayModel* dm = win ? win->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine) {
        return finish(2, str::DupTemp(StrL("NOTREADY no-engine")));
    }
    if (len(destPath) == 0 || dpi < 1 || dx <= 0 || dy <= 0) {
        return finish(1, str::DupTemp(StrL("ERROR bad-args")));
    }
    TempStr withExt = WithDefaultImageExtTemp(destPath);
    if (len(withExt) == 0) {
        return finish(1, str::DupTemp(StrL("ERROR bad-ext")));
    }
    RectF rect{(float)x, (float)y, (float)dx, (float)dy};
    int w = 0;
    int h = 0;
    if (!SavePageRectAsImage(engine, dm->GetRotation(), pageNo, rect, withExt, (float)dpi, &w, &h)) {
        return finish(1, str::DupTemp(StrL("ERROR save-failed")));
    }
    return finish(0, fmt("OK w=%d h=%d path=%s", w, h, withExt));
}

struct SaveSelectionAsImageDialog : PdfToolDialog {
    int pageNo = 0;
    RectF selRect;
    DropDown* dropFormat = nullptr;
    DropDown* dropDpi = nullptr;
    VirtText* sizeLabel = nullptr;
    int dpi = kSaveSelectionDefaultDpi; // 0 if what's typed is not a DPI
    bool syncingFormat = false;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void OnBrowseDest(VirtMouseEvent* ev = nullptr);
    void OnFormatChanged();
    void OnDpiPicked();
    void OnDpiTyped();
    void SetDestExtFromFormat();
    void SyncFormatFromPath(Str path);
    void UpdateSizeLabel();
    int SelectedFormatIdx() const;
};

int SaveSelectionAsImageDialog::SelectedFormatIdx() const {
    if (!dropFormat) {
        return 0;
    }
    int idx = CbGetCurrentSelection(dropFormat);
    if (idx < 0 || idx >= ConvertImageFormatCount()) {
        return 0;
    }
    return idx;
}

void SaveSelectionAsImageDialog::SetDestExtFromFormat() {
    if (!destEdit) {
        return;
    }
    TempStr dest = destEdit->GetTextTemp();
    if (len(dest) == 0) {
        return;
    }
    Str ext = kConvertImageFormats[SelectedFormatIdx()].ext;
    TempStr noExt = path::GetPathNoExtTemp(dest);
    TempStr newDest = str::JoinTemp(noExt, ext);
    if (str::EqI(dest, newDest)) {
        return;
    }
    destEdit->SetText(newDest);
}

void SaveSelectionAsImageDialog::SyncFormatFromPath(Str path) {
    if (!dropFormat) {
        return;
    }
    int idx = ConvertImageFormatIdxFromPath(path);
    if (idx == CbGetCurrentSelection(dropFormat)) {
        return;
    }
    syncingFormat = true;
    CbSetCurrentSelection(dropFormat, idx);
    syncingFormat = false;
}

void SaveSelectionAsImageDialog::UpdateSizeLabel() {
    if (!sizeLabel || !win) {
        return;
    }
    DisplayModel* dm = win->AsFixed();
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine) {
        return;
    }
    float zoom = SaveSelectionZoom(engine, (float)dpi);
    int w = 0;
    int h = 0;
    EstimateSelectionPx(selRect, zoom, w, h);
    if (dpi < 1) {
        sizeLabel->SetText(StrL(""));
        if (actionBtn) {
            actionBtn->SetIsEnabled(false);
        }
    } else if (!SaveSelectionSizeOk(w, h)) {
        sizeLabel->SetText(Tr("Too large for this DPI"));
        if (actionBtn) {
            actionBtn->SetIsEnabled(false);
        }
    } else {
        sizeLabel->SetText(fmt("%d x %d px", w, h));
        if (actionBtn) {
            TempStr dest = destEdit ? destEdit->GetTextTemp() : Str{};
            actionBtn->SetIsEnabled(len(dest) > 0);
        }
    }
    sizeLabel->RequestLayout();
}

void SaveSelectionAsImageDialog::OnFormatChanged() {
    if (syncingFormat) {
        return;
    }
    SetDestExtFromFormat();
}

void SaveSelectionAsImageDialog::OnDpiPicked() {
    dpi = DpiFromComboSel(dropDpi);
    UpdateSizeLabel();
}

void SaveSelectionAsImageDialog::OnDpiTyped() {
    dpi = DpiFromComboText(dropDpi);
    UpdateSizeLabel();
}

void SaveSelectionAsImageDialog::OnBrowseDest(VirtMouseEvent*) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    TempStr current = destEdit->GetTextTemp();
    wstr::BufSet(WStr(dstFileName, MAX_PATH), ToWStrTemp(current));

    int fmtIdx = SelectedFormatIdx();
    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = L"PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0BMP\0*.bmp\0All Files\0*.*\0";
    ofn.nFilterIndex = (DWORD)(fmtIdx + 1);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = kConvertImageFormats[fmtIdx].defExt.s;

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }
    TempStr picked = ToUtf8Temp(dstFileName);
    destEdit->SetText(picked);
    int filterIdx = (int)ofn.nFilterIndex - 1;
    if (filterIdx >= 0 && filterIdx < ConvertImageFormatCount()) {
        syncingFormat = true;
        CbSetCurrentSelection(dropFormat, filterIdx);
        syncingFormat = false;
        SetDestExtFromFormat();
    } else {
        SyncFormatFromPath(picked);
    }
    UpdateSizeLabel();
}

void SaveSelectionAsImageDialog::DoIt(VirtMouseEvent*) {
    TempStr destPath = destEdit->GetTextTemp();
    if (len(destPath) == 0) {
        return;
    }
    DisplayModel* dm = win ? win->AsFixed() : nullptr;
    EngineBase* engine = dm ? dm->GetEngine() : nullptr;
    if (!engine) {
        return;
    }
    TempStr withExt = WithDefaultImageExtTemp(destPath);
    if (len(withExt) == 0) {
        MessageBoxWarning(hwnd, StrL("Unsupported image format."), Tr("Save Selection As Image"));
        return;
    }
    // the box may have been filled without a change notification
    dpi = DpiFromComboText(dropDpi);
    if (dpi < 1) {
        return;
    }
    Pixmap* px = RenderSelectionPixmap(engine, dm->GetRotation(), pageNo, selRect, (float)dpi);
    if (!px) {
        MessageBoxWarning(hwnd, StrL("Failed to save the selection as an image."), Tr("Save Selection As Image"));
        return;
    }

    auto* work = new SaveSelImgWork();
    work->px = px;
    work->destPath = str::Dup(withExt);
    work->hwndCanvas = win->hwndCanvas;
    work->win = win;

    NotificationCreateArgs nargs;
    nargs.hwndParent = win->hwndCanvas;
    nargs.msg = Tr("Saving image...");
    nargs.groupId = kNotifSaveSelectionAsImage;
    nargs.timeoutMs = kNotifNoTimeout;
    ShowNotification(nargs);

    Close();
    RunAsync(MkFunc0(SaveSelImgThread, work), StrL("SaveSelImg"));
}

bool SaveSelectionAsImageDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!tab || !tab->selectionOnPage || len(*tab->selectionOnPage) == 0) {
        return false;
    }
    SelectionOnPage& sel = (*tab->selectionOnPage)[0];
    if (sel.rect.IsEmpty()) {
        return false;
    }
    pageNo = sel.pageNo;
    selRect = sel.rect;

    if (!CreateToolDialog(w, tab, Tr("Save Selection As Image"))) {
        return false;
    }
    AddPathRow();

    TempStr noExt = path::GetPathNoExtTemp(srcPath);
    TempStr pngPath = MakeUniqueFilePathTemp(str::JoinTemp(noExt, StrL("-sel.png")));

    HBox* destRow = AddRow();
    destRow->gap = font->averageCharWidth;

    Edit::CreateArgs dargs;
    dargs.parent = hwnd;
    dargs.withBorder = true;
    dargs.font = font;
    dargs.text = pngPath;
    dargs.isRtl = IsUIRtl();
    destEdit = new Edit();
    destEdit->Create(dargs);
    destRow->AddChild(destEdit, 1);

    {
        auto* dd = new DropDown();
        DropDown::CreateArgs ddargs;
        ddargs.parent = hwnd;
        ddargs.font = font;
        ddargs.isRtl = IsUIRtl();
        dd->Create(ddargs);
        StrVec items;
        for (int i = 0; i < ConvertImageFormatCount(); i++) {
            items.Append(kConvertImageFormats[i].label);
        }
        dd->SetItems(items);
        CbSetCurrentSelection(dd, 0);
        dd->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
        dd->onSelectionChanged =
            MkMethod0<SaveSelectionAsImageDialog, &SaveSelectionAsImageDialog::OnFormatChanged>(this);
        dropFormat = dd;
        destRow->AddChild(dropFormat);
    }

    browseBtn = NewButton(StrL("..."), false);
    browseBtn->onClick =
        MkMethod1<SaveSelectionAsImageDialog, VirtMouseEvent*, &SaveSelectionAsImageDialog::OnBrowseDest>(this);
    destRow->AddChild(browseBtn);

    if (pathLabel) {
        pathLabel->padding.left = destEdit->GetLeftTextMargin();
    }

    HBox* dpiRow = AddRow();
    dpiRow->gap = font->averageCharWidth;
    dpiRow->AddChild(NewVirtText({.s = Tr("Resolution (DPI):"), .font = font, .isRtl = IsUIRtl()}));

    dropDpi = NewDpiCombo(hwnd, font, dpi);
    dropDpi->onSelectionChanged = MkMethod0<SaveSelectionAsImageDialog, &SaveSelectionAsImageDialog::OnDpiPicked>(this);
    dropDpi->onTextChanged = MkMethod0<SaveSelectionAsImageDialog, &SaveSelectionAsImageDialog::OnDpiTyped>(this);
    dpiRow->AddChild(dropDpi);

    sizeLabel = NewVirtText({.s = StrL(""), .font = font, .isRtl = IsUIRtl()});
    dpiRow->AddChild(sizeLabel, 1);

    AddButtonsRow(Tr("Save"));
    FinishDialog(destEdit);

    destEdit->onTextChanged = MkMethod0<SaveSelectionAsImageDialog, &SaveSelectionAsImageDialog::UpdateSizeLabel>(this);
    UpdateSizeLabel();
    return true;
}

void ShowSaveSelectionAsImageDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    if (!HasPermission(Perm::CopySelection) || !CanAccessDisk()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsRectangularSelection(win)) {
        return;
    }
    logf("ShowSaveSelectionAsImageDialog: opening for '%s'\n", tab->filePath);

    auto* dlg = new SaveSelectionAsImageDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Page stamps: Watermark, Header/Footer, Bates numbering ---

static pdf_obj* AddContentStream(fz_context* ctx, pdf_document* doc, Str s) {
    fz_buffer* buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)s.s, (size_t)s.len);
    pdf_obj* stm = nullptr;
    fz_try(ctx) {
        stm = pdf_add_stream(ctx, doc, buf, nullptr, 0);
    }
    fz_always(ctx) {
        fz_drop_buffer(ctx, buf);
    }
    fz_catch(ctx) {
        fz_rethrow(ctx);
    }
    return stm;
}

static void AppendWinAnsiPdfString(str::Builder& b, Str utf8) {
    b.AppendChar('(');
    TempWStr ws = ToWStrTemp(utf8);
    for (int i = 0; i < ws.len; i++) {
        int c = fz_windows_1252_from_unicode(ws.s[i]);
        if (c < 0) {
            c = '?';
        }
        if (c == '(' || c == ')' || c == '\\') {
            b.AppendChar('\\');
            b.AppendChar((char)c);
        } else if (c < 0x20 || c >= 0x7f) {
            b.Append(fmt("\\%03o", c));
        } else {
            b.AppendChar((char)c);
        }
    }
    b.AppendChar(')');
}

static float TextWidth(fz_context* ctx, fz_font* font, float size, Str utf8) {
    TempWStr ws = ToWStrTemp(utf8);
    float w = 0;
    for (int i = 0; i < ws.len; i++) {
        int gid = fz_encode_character(ctx, font, ws.s[i]);
        w += fz_advance_glyph(ctx, font, gid, 0);
    }
    return w * size;
}

static TempStr ReplaceHeaderFooterTokens(Str pattern, int pageNo, int totalPages, Str dateStr, Str filename) {
    str::Builder b;
    for (int i = 0; i < len(pattern);) {
        if (pattern.s[i] == '{') {
            Str rem = Str(pattern.s + i, len(pattern) - i);
            if (str::StartsWith(rem, StrL("{page}"))) {
                b.Append(fmt("%d", pageNo));
                i += 6;
                continue;
            }
            if (str::StartsWith(rem, StrL("{pages}"))) {
                b.Append(fmt("%d", totalPages));
                i += 7;
                continue;
            }
            if (str::StartsWith(rem, StrL("{date}"))) {
                b.Append(dateStr);
                i += 6;
                continue;
            }
            if (str::StartsWith(rem, StrL("{filename}"))) {
                b.Append(filename);
                i += 10;
                continue;
            }
        }
        b.AppendChar(pattern.s[i]);
        i++;
    }
    return ToStrTemp(b);
}

static bool ResolvePageRange(Str pagesSpec, int pageCount, Vec<int>& outPages) {
    if (len(pagesSpec) == 0 || str::EqI(pagesSpec, StrL("all"))) {
        for (int i = 1; i <= pageCount; i++) {
            VecAppend(outPages, i);
        }
        return true;
    }
    Vec<int> parsed;
    if (!ParseDeletePages(pagesSpec, pageCount, parsed)) {
        return false;
    }
    for (int p : parsed) {
        if (!VecContains(outPages, p)) {
            VecAppend(outPages, p);
        }
    }
    return len(outPages) > 0;
}

struct PageStampDrawItem {
    Str text;
    float fontSize = 12.f;
    float opacity = 1.0f;
    float angle = 0.f;
    float x = 0.f;
    float y = 0.f;
    int align = 0; // 0 = left, 1 = center, 2 = right
    COLORREF color = RGB(0, 0, 0);
    bool rotateCenter = false;
};

static void ApplyItemsToPage(fz_context* ctx, pdf_document* doc, int pageIndex, const Vec<PageStampDrawItem>& items) {
    if (len(items) == 0) {
        return;
    }
    pdf_page* page = pdf_load_page(ctx, doc, pageIndex);
    if (!page) {
        return;
    }
    fz_rect mediabox;
    fz_matrix ctm;
    pdf_page_transform(ctx, page, &mediabox, &ctm);
    fz_matrix inv_ctm = fz_invert_matrix(ctm);

    fz_font* font = fz_new_base14_font(ctx, "Helvetica-Bold");
    pdf_obj* fontRef = pdf_add_simple_font(ctx, doc, font, PDF_SIMPLE_ENCODING_LATIN);

    pdf_obj* res = pdf_dict_get(ctx, page->obj, PDF_NAME(Resources));
    if (!res) {
        pdf_obj* inher = pdf_dict_get_inheritable(ctx, page->obj, PDF_NAME(Resources));
        if (inher) {
            res = pdf_copy_dict(ctx, inher);
        } else {
            res = pdf_new_dict(ctx, doc, 2);
        }
        pdf_dict_put_drop(ctx, page->obj, PDF_NAME(Resources), res);
    }
    pdf_obj* fonts = pdf_dict_get(ctx, res, PDF_NAME(Font));
    if (!fonts) {
        fonts = pdf_new_dict(ctx, doc, 1);
        pdf_dict_put_drop(ctx, res, PDF_NAME(Font), fonts);
    }
    TempStr fontName;
    for (int n = 1;; n++) {
        fontName = fmt("SwiftleafF%d", n);
        if (!pdf_dict_gets(ctx, fonts, CStrTemp(fontName))) {
            break;
        }
    }
    pdf_dict_puts_drop(ctx, fonts, CStrTemp(fontName), fontRef);

    TempStr gsName;
    float minOpacity = 1.0f;
    for (const auto& item : items) {
        if (item.opacity < minOpacity) {
            minOpacity = item.opacity;
        }
    }
    if (minOpacity < 1.0f) {
        pdf_obj* egs = pdf_dict_get(ctx, res, PDF_NAME(ExtGState));
        if (!egs) {
            egs = pdf_new_dict(ctx, doc, 1);
            pdf_dict_put_drop(ctx, res, PDF_NAME(ExtGState), egs);
        }
        for (int n = 1;; n++) {
            gsName = fmt("SwiftleafGS%d", n);
            if (!pdf_dict_gets(ctx, egs, CStrTemp(gsName))) {
                break;
            }
        }
        pdf_obj* gsDict = pdf_new_dict(ctx, doc, 3);
        pdf_dict_put(ctx, gsDict, PDF_NAME(Type), PDF_NAME(ExtGState));
        pdf_dict_put_drop(ctx, gsDict, PDF_NAME(CA), pdf_new_real(ctx, minOpacity));
        pdf_dict_put_drop(ctx, gsDict, PDF_NAME(ca), pdf_new_real(ctx, minOpacity));
        pdf_dict_puts_drop(ctx, egs, CStrTemp(gsName), gsDict);
    }

    str::Builder content;
    content.Append(StrL("q\n"));
    if (minOpacity < 1.0f) {
        content.Append(fmt("/%s gs\n", gsName));
    }

    for (const auto& item : items) {
        if (len(item.text) == 0) {
            continue;
        }
        float w = TextWidth(ctx, font, item.fontSize, item.text);
        fz_matrix tm;
        if (item.rotateCenter) {
            fz_matrix mCenter = fz_translate(-w / 2.0f, -item.fontSize * 0.3f);
            fz_matrix mRot = fz_rotate(item.angle);
            fz_matrix mVis{1, 0, 0, -1, item.x, item.y};
            tm = fz_concat(fz_concat(mCenter, mRot), mVis);
        } else {
            float xPos = item.x;
            if (item.align == 1) {
                xPos -= w / 2.0f;
            } else if (item.align == 2) {
                xPos -= w;
            }
            fz_matrix mVis{1, 0, 0, -1, xPos, item.y};
            tm = mVis;
        }
        tm = fz_concat(tm, inv_ctm);

        u8 cr = GetRValue(item.color);
        u8 cg = GetGValue(item.color);
        u8 cb = GetBValue(item.color);
        content.Append(
            fmt("BT\n/%s %.2f Tf\n%.3f %.3f %.3f rg\n", fontName, item.fontSize, cr / 255.f, cg / 255.f, cb / 255.f));
        content.Append(fmt("%.4f %.4f %.4f %.4f %.3f %.3f Tm\n", tm.a, tm.b, tm.c, tm.d, tm.e, tm.f));
        AppendWinAnsiPdfString(content, item.text);
        content.Append(StrL(" Tj\nET\n"));
    }
    content.Append(StrL("Q\n"));

    pdf_obj* stmPush = AddContentStream(ctx, doc, StrL("q\n"));
    pdf_obj* stmPop = AddContentStream(ctx, doc, StrL("Q\n"));
    pdf_obj* stmOverlay = AddContentStream(ctx, doc, ToStrTemp(content));
    pdf_obj* contents = pdf_dict_get(ctx, page->obj, PDF_NAME(Contents));
    pdf_obj* arr = pdf_new_array(ctx, doc, 4);
    pdf_array_push(ctx, arr, stmPush);
    if (pdf_is_array(ctx, contents)) {
        int n = pdf_array_len(ctx, contents);
        for (int k = 0; k < n; k++) {
            pdf_array_push(ctx, arr, pdf_array_get(ctx, contents, k));
        }
    } else if (contents) {
        pdf_array_push(ctx, arr, contents);
    }
    pdf_array_push(ctx, arr, stmPop);
    pdf_array_push(ctx, arr, stmOverlay);
    pdf_dict_put(ctx, page->obj, PDF_NAME(Contents), arr);

    pdf_drop_obj(ctx, arr);
    pdf_drop_obj(ctx, stmPush);
    pdf_drop_obj(ctx, stmPop);
    pdf_drop_obj(ctx, stmOverlay);
    fz_drop_font(ctx, font);
    fz_drop_page(ctx, (fz_page*)page);
}

bool PdfApplyWatermark(Str srcPath, Str destPath, const PdfWatermarkParams& params) {
    if (len(srcPath) == 0 || len(destPath) == 0 || len(params.text) == 0) {
        return false;
    }
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        return false;
    }
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        return false;
    }
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted PDF not supported");
        }
        int pageCount = pdf_count_pages(ctx, doc);
        Vec<int> pages;
        if (!ResolvePageRange(params.pagesSpec, pageCount, pages)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "invalid page range");
        }

        for (int pNum : pages) {
            int pageIndex = pNum - 1;
            if (pageIndex < 0 || pageIndex >= pageCount) {
                continue;
            }
            pdf_page* page = pdf_load_page(ctx, doc, pageIndex);
            fz_rect bound = pdf_bound_page(ctx, page, FZ_CROP_BOX);
            float pageW = bound.x1 - bound.x0;
            float pageH = bound.y1 - bound.y0;
            fz_drop_page(ctx, (fz_page*)page);

            float cx = pageW / 2.f;
            float cy = pageH / 2.f;
            float margin = 54.f;
            switch (params.position) {
                case PdfWatermarkPos::Center:
                    cx = pageW / 2.f;
                    cy = pageH / 2.f;
                    break;
                case PdfWatermarkPos::TopLeft:
                    cx = margin;
                    cy = margin;
                    break;
                case PdfWatermarkPos::TopCenter:
                    cx = pageW / 2.f;
                    cy = margin;
                    break;
                case PdfWatermarkPos::TopRight:
                    cx = pageW - margin;
                    cy = margin;
                    break;
                case PdfWatermarkPos::CenterLeft:
                    cx = margin;
                    cy = pageH / 2.f;
                    break;
                case PdfWatermarkPos::CenterRight:
                    cx = pageW - margin;
                    cy = pageH / 2.f;
                    break;
                case PdfWatermarkPos::BottomLeft:
                    cx = margin;
                    cy = pageH - margin;
                    break;
                case PdfWatermarkPos::BottomCenter:
                    cx = pageW / 2.f;
                    cy = pageH - margin;
                    break;
                case PdfWatermarkPos::BottomRight:
                    cx = pageW - margin;
                    cy = pageH - margin;
                    break;
            }

            PageStampDrawItem item;
            item.text = params.text;
            item.fontSize = params.fontSize;
            item.opacity = std::max(0.01f, std::min(1.0f, params.opacity));
            item.angle = params.angle;
            item.x = cx;
            item.y = cy;
            item.color = params.color;
            item.rotateCenter = true;

            Vec<PageStampDrawItem> items;
            VecAppend(items, item);
            ApplyItemsToPage(ctx, doc, pageIndex, items);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        opts.do_compress = 1;
        opts.do_garbage = 1;

        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) {
            pdf_drop_document(ctx, doc);
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        logf("PdfApplyWatermark failed: %s\n", Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfApplyHeaderFooter(Str srcPath, Str destPath, const PdfHeaderFooterParams& params) {
    if (len(srcPath) == 0 || len(destPath) == 0) {
        return false;
    }
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        return false;
    }
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        return false;
    }
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted PDF not supported");
        }
        int pageCount = pdf_count_pages(ctx, doc);
        Vec<int> pages;
        if (!ResolvePageRange(params.pagesSpec, pageCount, pages)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "invalid page range");
        }

        SYSTEMTIME st;
        GetLocalTime(&st);
        TempStr dateStr = fmt("%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        TempStr filename = path::GetBaseNameTemp(srcPath);

        float marginX = 36.f;
        float marginY = 36.f;

        for (int pNum : pages) {
            int pageIndex = pNum - 1;
            if (pageIndex < 0 || pageIndex >= pageCount) {
                continue;
            }
            pdf_page* page = pdf_load_page(ctx, doc, pageIndex);
            fz_rect bound = pdf_bound_page(ctx, page, FZ_CROP_BOX);
            float pageW = bound.x1 - bound.x0;
            float pageH = bound.y1 - bound.y0;
            fz_drop_page(ctx, (fz_page*)page);

            Vec<PageStampDrawItem> items;
            auto addItem = [&](Str pat, float x, float y, int align) {
                if (len(pat) == 0) return;
                TempStr txt = ReplaceHeaderFooterTokens(pat, pNum, pageCount, dateStr, filename);
                if (len(txt) == 0) return;
                PageStampDrawItem it;
                it.text = txt;
                it.fontSize = params.fontSize;
                it.opacity = 1.0f;
                it.angle = 0.f;
                it.x = x;
                it.y = y;
                it.align = align;
                it.color = params.color;
                it.rotateCenter = false;
                VecAppend(items, it);
            };

            // Header: visual top
            addItem(params.headerLeft, marginX, marginY + params.fontSize * 0.8f, 0);
            addItem(params.headerCenter, pageW / 2.f, marginY + params.fontSize * 0.8f, 1);
            addItem(params.headerRight, pageW - marginX, marginY + params.fontSize * 0.8f, 2);

            // Footer: visual bottom
            addItem(params.footerLeft, marginX, pageH - marginY, 0);
            addItem(params.footerCenter, pageW / 2.f, pageH - marginY, 1);
            addItem(params.footerRight, pageW - marginX, pageH - marginY, 2);

            ApplyItemsToPage(ctx, doc, pageIndex, items);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        opts.do_compress = 1;
        opts.do_garbage = 1;

        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) {
            pdf_drop_document(ctx, doc);
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        logf("PdfApplyHeaderFooter failed: %s\n", Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfApplyBatesNumbering(Str srcPath, Str destPath, const PdfBatesParams& params) {
    if (len(srcPath) == 0 || len(destPath) == 0) {
        return false;
    }
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) {
        return false;
    }
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        return false;
    }
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted PDF not supported");
        }
        int pageCount = pdf_count_pages(ctx, doc);
        Vec<int> pages;
        if (!ResolvePageRange(params.pagesSpec, pageCount, pages)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "invalid page range");
        }

        float marginX = 36.f;
        float marginY = 36.f;
        int digits = std::max(1, std::min(12, params.digitCount));

        for (int i = 0; i < len(pages); i++) {
            int pNum = pages[i];
            int pageIndex = pNum - 1;
            if (pageIndex < 0 || pageIndex >= pageCount) {
                continue;
            }
            pdf_page* page = pdf_load_page(ctx, doc, pageIndex);
            fz_rect bound = pdf_bound_page(ctx, page, FZ_CROP_BOX);
            float pageW = bound.x1 - bound.x0;
            float pageH = bound.y1 - bound.y0;
            fz_drop_page(ctx, (fz_page*)page);

            int batesNum = params.startNumber + i;
            char numBuf[32];
            snprintf(numBuf, sizeof(numBuf), "%0*d", digits, batesNum);
            TempStr batesText = fmt("%s%s", params.prefix, Str(numBuf));

            float x = pageW - marginX;
            float y = pageH - marginY;
            int align = 2; // right

            switch (params.position) {
                case PdfBatesPos::TopLeft:
                    x = marginX;
                    y = marginY + params.fontSize * 0.8f;
                    align = 0;
                    break;
                case PdfBatesPos::TopCenter:
                    x = pageW / 2.f;
                    y = marginY + params.fontSize * 0.8f;
                    align = 1;
                    break;
                case PdfBatesPos::TopRight:
                    x = pageW - marginX;
                    y = marginY + params.fontSize * 0.8f;
                    align = 2;
                    break;
                case PdfBatesPos::BottomLeft:
                    x = marginX;
                    y = pageH - marginY;
                    align = 0;
                    break;
                case PdfBatesPos::BottomCenter:
                    x = pageW / 2.f;
                    y = pageH - marginY;
                    align = 1;
                    break;
                case PdfBatesPos::BottomRight:
                    x = pageW - marginX;
                    y = pageH - marginY;
                    align = 2;
                    break;
            }

            PageStampDrawItem item;
            item.text = batesText;
            item.fontSize = params.fontSize;
            item.opacity = 1.0f;
            item.angle = 0.f;
            item.x = x;
            item.y = y;
            item.align = align;
            item.color = params.color;
            item.rotateCenter = false;

            Vec<PageStampDrawItem> items;
            VecAppend(items, item);
            ApplyItemsToPage(ctx, doc, pageIndex, items);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        opts.do_compress = 1;
        opts.do_garbage = 1;

        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) {
            pdf_drop_document(ctx, doc);
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        logf("PdfApplyBatesNumbering failed: %s\n", Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    return ok;
}

// --- Watermark Dialog ---

struct PdfWatermarkDialog : PdfToolDialog {
    Edit* textEdit = nullptr;
    Edit* opacityEdit = nullptr;
    Edit* angleEdit = nullptr;
    Edit* sizeEdit = nullptr;
    DropDown* dropPos = nullptr;
    DropDown* dropColor = nullptr;

    Checkbox* radioAll = nullptr;
    Checkbox* radioCurrent = nullptr;
    Checkbox* radioCustom = nullptr;
    Edit* pagesEdit = nullptr;

    int pageCount = 0;
    int currentPage = 1;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void OnPagesModeChanged();
    void UpdateButton();
};

void PdfWatermarkDialog::OnPagesModeChanged() {
    bool custom = radioCustom && radioCustom->IsChecked();
    if (pagesEdit) {
        pagesEdit->SetIsEnabled(custom);
    }
    UpdateButton();
}

void PdfWatermarkDialog::UpdateButton() {
    if (!actionBtn) return;
    TempStr txt = textEdit ? textEdit->GetTextTemp() : TempStr{};
    bool ok = len(txt) > 0;
    if (radioCustom && radioCustom->IsChecked()) {
        TempStr pages = pagesEdit ? pagesEdit->GetTextTemp() : TempStr{};
        Vec<int> p;
        ok = ok && ParseDeletePages(pages, pageCount, p);
    }
    actionBtn->SetFlag(vwfEnabled, ok);
}

void PdfWatermarkDialog::DoIt(VirtMouseEvent*) {
    PdfToolEdit edit(win, srcPath);
    if (!edit.IsReady()) return;
    Str destPath = edit.outputPath;
    if (len(destPath) == 0) {
        return;
    }
    TempStr text = textEdit->GetTextTemp();
    if (len(text) == 0) {
        return;
    }

    PdfWatermarkParams params;
    params.text = text;

    TempStr opStr = opacityEdit ? opacityEdit->GetTextTemp() : TempStr{};
    int opVal = 50;
    str::Parse(opStr, "%d", &opVal);
    params.opacity = std::max(1, std::min(100, opVal)) / 100.0f;

    TempStr angStr = angleEdit ? angleEdit->GetTextTemp() : TempStr{};
    int angVal = 45;
    str::Parse(angStr, "%d", &angVal);
    params.angle = (float)angVal;

    TempStr szStr = sizeEdit ? sizeEdit->GetTextTemp() : TempStr{};
    int szVal = 48;
    str::Parse(szStr, "%d", &szVal);
    params.fontSize = (float)std::max(6, std::min(144, szVal));

    int posIdx = dropPos ? CbGetCurrentSelection(dropPos) : 0;
    params.position = (PdfWatermarkPos)posIdx;

    int colIdx = dropColor ? CbGetCurrentSelection(dropColor) : 0;
    switch (colIdx) {
        case 1:
            params.color = RGB(220, 30, 30);
            break; // Red
        case 2:
            params.color = RGB(30, 80, 220);
            break; // Blue
        case 3:
            params.color = RGB(0, 0, 0);
            break; // Black
        case 4:
            params.color = RGB(30, 150, 40);
            break; // Green
        default:
            params.color = RGB(128, 128, 128);
            break; // Gray
    }

    if (radioCurrent && radioCurrent->IsChecked()) {
        params.pagesSpec = fmt("%d", currentPage);
    } else if (radioCustom && radioCustom->IsChecked()) {
        params.pagesSpec = pagesEdit ? pagesEdit->GetTextTemp() : TempStr{};
    } else {
        params.pagesSpec = StrL("all");
    }

    Str inputPath = edit.inputPath;

    logf("PdfWatermarkDialog::DoIt: applying watermark to '%s'\n", destPath);
    bool ok = PdfApplyWatermark(inputPath, destPath, params);

    if (ok) {
        if (edit.Apply("Add watermark")) Close();
    } else {
        MessageBoxWarning(hwnd, StrL("Failed to add watermark to PDF file."), Tr("Add Watermark"));
    }
}

bool PdfWatermarkDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Add Watermark"))) {
        return false;
    }
    pageCount = w->ctrl ? w->ctrl->PageCount() : 1;
    currentPage = w->ctrl ? w->ctrl->CurrentPageNo() : 1;

    AddPathRow();

    textEdit = AddLabeledEdit(Tr("Watermark Text:"), StrL("CONFIDENTIAL"));

    HBox* optRow = AddRow();
    optRow->gap = font->averageCharWidth;

    optRow->AddChild(NewVirtText({.s = Tr("Opacity (%):"), .font = font, .isRtl = IsUIRtl()}));
    Edit::CreateArgs opArgs;
    opArgs.parent = hwnd;
    opArgs.withBorder = true;
    opArgs.font = font;
    opArgs.text = StrL("50");
    opArgs.isRtl = IsUIRtl();
    opacityEdit = new Edit();
    opacityEdit->Create(opArgs);
    optRow->AddChild(opacityEdit, 1);

    optRow->AddChild(new Spacer(gap, 0));
    optRow->AddChild(NewVirtText({.s = Tr("Angle (°):"), .font = font, .isRtl = IsUIRtl()}));
    Edit::CreateArgs angArgs;
    angArgs.parent = hwnd;
    angArgs.withBorder = true;
    angArgs.font = font;
    angArgs.text = StrL("45");
    angArgs.isRtl = IsUIRtl();
    angleEdit = new Edit();
    angleEdit->Create(angArgs);
    optRow->AddChild(angleEdit, 1);

    optRow->AddChild(new Spacer(gap, 0));
    optRow->AddChild(NewVirtText({.s = Tr("Size (pt):"), .font = font, .isRtl = IsUIRtl()}));
    Edit::CreateArgs szArgs;
    szArgs.parent = hwnd;
    szArgs.withBorder = true;
    szArgs.font = font;
    szArgs.text = StrL("48");
    szArgs.isRtl = IsUIRtl();
    sizeEdit = new Edit();
    sizeEdit->Create(szArgs);
    optRow->AddChild(sizeEdit, 1);

    HBox* pcRow = AddRow();
    pcRow->gap = font->averageCharWidth;

    pcRow->AddChild(NewVirtText({.s = Tr("Position:"), .font = font, .isRtl = IsUIRtl()}));
    {
        dropPos = new DropDown();
        DropDown::CreateArgs ddargs;
        ddargs.parent = hwnd;
        ddargs.font = font;
        ddargs.isRtl = IsUIRtl();
        dropPos->Create(ddargs);
        StrVec posItems;
        posItems.Append(Tr("Center"));
        posItems.Append(Tr("Top-Left"));
        posItems.Append(Tr("Top-Center"));
        posItems.Append(Tr("Top-Right"));
        posItems.Append(Tr("Center-Left"));
        posItems.Append(Tr("Center-Right"));
        posItems.Append(Tr("Bottom-Left"));
        posItems.Append(Tr("Bottom-Center"));
        posItems.Append(Tr("Bottom-Right"));
        dropPos->SetItems(posItems);
        CbSetCurrentSelection(dropPos, 0);
        dropPos->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
        pcRow->AddChild(dropPos, 1);
    }

    pcRow->AddChild(new Spacer(gap, 0));
    pcRow->AddChild(NewVirtText({.s = Tr("Color:"), .font = font, .isRtl = IsUIRtl()}));
    {
        dropColor = new DropDown();
        DropDown::CreateArgs ddargs;
        ddargs.parent = hwnd;
        ddargs.font = font;
        ddargs.isRtl = IsUIRtl();
        dropColor->Create(ddargs);
        StrVec colItems;
        colItems.Append(Tr("Gray"));
        colItems.Append(Tr("Red"));
        colItems.Append(Tr("Blue"));
        colItems.Append(Tr("Black"));
        colItems.Append(Tr("Green"));
        dropColor->SetItems(colItems);
        CbSetCurrentSelection(dropColor, 0);
        dropColor->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
        pcRow->AddChild(dropColor, 1);
    }

    HBox* pRow = AddRow();
    pRow->gap = font->averageCharWidth;
    pRow->AddChild(NewVirtText({.s = Tr("Pages:"), .font = font, .isRtl = IsUIRtl()}));

    radioAll = NewPagesRadio(hwnd, font, Tr("All"), true, true);
    radioAll->onStateChanged = MkMethod0<PdfWatermarkDialog, &PdfWatermarkDialog::OnPagesModeChanged>(this);
    pRow->AddChild(radioAll);

    radioCurrent = NewPagesRadio(hwnd, font, Tr("Current"), false, false);
    radioCurrent->onStateChanged = MkMethod0<PdfWatermarkDialog, &PdfWatermarkDialog::OnPagesModeChanged>(this);
    pRow->AddChild(radioCurrent);

    radioCustom = NewPagesRadio(hwnd, font, Tr("Custom"), false, false);
    radioCustom->onStateChanged = MkMethod0<PdfWatermarkDialog, &PdfWatermarkDialog::OnPagesModeChanged>(this);
    pRow->AddChild(radioCustom);

    Edit::CreateArgs peArgs;
    peArgs.parent = hwnd;
    peArgs.withBorder = true;
    peArgs.font = font;
    peArgs.text = fmt("1-%d", pageCount);
    peArgs.isRtl = IsUIRtl();
    pagesEdit = new Edit();
    pagesEdit->Create(peArgs);
    pagesEdit->SetIsEnabled(false);
    pRow->AddChild(pagesEdit, 1);

    AddButtonsRow(Tr("Add Watermark"));
    FinishDialog(textEdit);

    textEdit->onTextChanged = MkMethod0<PdfWatermarkDialog, &PdfWatermarkDialog::UpdateButton>(this);
    pagesEdit->onTextChanged = MkMethod0<PdfWatermarkDialog, &PdfWatermarkDialog::UpdateButton>(this);
    UpdateButton();
    return true;
}

void ShowPdfAddWatermarkDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0 || !IsPdfDoc(tab)) {
        return;
    }
    logf("ShowPdfAddWatermarkDialog: opening for '%s'\n", tab->filePath);
    auto* dlg = new PdfWatermarkDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Header / Footer Dialog ---

struct PdfHeaderFooterDialog : PdfToolDialog {
    Edit* headerLeftEdit = nullptr;
    Edit* headerCenterEdit = nullptr;
    Edit* headerRightEdit = nullptr;
    Edit* footerLeftEdit = nullptr;
    Edit* footerCenterEdit = nullptr;
    Edit* footerRightEdit = nullptr;

    Checkbox* radioAll = nullptr;
    Checkbox* radioCurrent = nullptr;
    Checkbox* radioCustom = nullptr;
    Edit* pagesEdit = nullptr;

    int pageCount = 0;
    int currentPage = 1;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void OnPagesModeChanged();
    void UpdateButton();
};

void PdfHeaderFooterDialog::OnPagesModeChanged() {
    bool custom = radioCustom && radioCustom->IsChecked();
    if (pagesEdit) {
        pagesEdit->SetIsEnabled(custom);
    }
    UpdateButton();
}

void PdfHeaderFooterDialog::UpdateButton() {
    if (!actionBtn) return;
    bool hasAny = (headerLeftEdit && len(headerLeftEdit->GetTextTemp()) > 0) ||
                  (headerCenterEdit && len(headerCenterEdit->GetTextTemp()) > 0) ||
                  (headerRightEdit && len(headerRightEdit->GetTextTemp()) > 0) ||
                  (footerLeftEdit && len(footerLeftEdit->GetTextTemp()) > 0) ||
                  (footerCenterEdit && len(footerCenterEdit->GetTextTemp()) > 0) ||
                  (footerRightEdit && len(footerRightEdit->GetTextTemp()) > 0);
    bool ok = hasAny;
    if (radioCustom && radioCustom->IsChecked()) {
        TempStr pages = pagesEdit ? pagesEdit->GetTextTemp() : TempStr{};
        Vec<int> p;
        ok = ok && ParseDeletePages(pages, pageCount, p);
    }
    actionBtn->SetFlag(vwfEnabled, ok);
}

void PdfHeaderFooterDialog::DoIt(VirtMouseEvent*) {
    PdfToolEdit edit(win, srcPath);
    if (!edit.IsReady()) return;
    Str destPath = edit.outputPath;
    if (len(destPath) == 0) {
        return;
    }

    PdfHeaderFooterParams params;
    params.headerLeft = headerLeftEdit ? headerLeftEdit->GetTextTemp() : TempStr{};
    params.headerCenter = headerCenterEdit ? headerCenterEdit->GetTextTemp() : TempStr{};
    params.headerRight = headerRightEdit ? headerRightEdit->GetTextTemp() : TempStr{};
    params.footerLeft = footerLeftEdit ? footerLeftEdit->GetTextTemp() : TempStr{};
    params.footerCenter = footerCenterEdit ? footerCenterEdit->GetTextTemp() : TempStr{};
    params.footerRight = footerRightEdit ? footerRightEdit->GetTextTemp() : TempStr{};

    if (radioCurrent && radioCurrent->IsChecked()) {
        params.pagesSpec = fmt("%d", currentPage);
    } else if (radioCustom && radioCustom->IsChecked()) {
        params.pagesSpec = pagesEdit ? pagesEdit->GetTextTemp() : TempStr{};
    } else {
        params.pagesSpec = StrL("all");
    }

    Str inputPath = edit.inputPath;

    logf("PdfHeaderFooterDialog::DoIt: applying header/footer to '%s'\n", destPath);
    bool ok = PdfApplyHeaderFooter(inputPath, destPath, params);

    if (ok) {
        if (edit.Apply("Add header/footer")) Close();
    } else {
        MessageBoxWarning(hwnd, StrL("Failed to add header/footer to PDF file."), Tr("Add Header / Footer"));
    }
}

bool PdfHeaderFooterDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Add Header / Footer"))) {
        return false;
    }
    pageCount = w->ctrl ? w->ctrl->PageCount() : 1;
    currentPage = w->ctrl ? w->ctrl->CurrentPageNo() : 1;

    AddPathRow();

    HBox* hRow = AddRow();
    hRow->gap = font->averageCharWidth;
    hRow->AddChild(NewVirtText({.s = Tr("Header (L / C / R):"), .font = font, .isRtl = IsUIRtl()}));

    Edit::CreateArgs hLArgs;
    hLArgs.parent = hwnd;
    hLArgs.withBorder = true;
    hLArgs.font = font;
    hLArgs.text = {};
    hLArgs.isRtl = IsUIRtl();
    headerLeftEdit = new Edit();
    headerLeftEdit->Create(hLArgs);
    hRow->AddChild(headerLeftEdit, 1);

    Edit::CreateArgs hCArgs = hLArgs;
    headerCenterEdit = new Edit();
    headerCenterEdit->Create(hCArgs);
    hRow->AddChild(headerCenterEdit, 1);

    Edit::CreateArgs hRArgs = hLArgs;
    headerRightEdit = new Edit();
    headerRightEdit->Create(hRArgs);
    hRow->AddChild(headerRightEdit, 1);

    HBox* fRow = AddRow();
    fRow->gap = font->averageCharWidth;
    fRow->AddChild(NewVirtText({.s = Tr("Footer (L / C / R):"), .font = font, .isRtl = IsUIRtl()}));

    Edit::CreateArgs fLArgs = hLArgs;
    footerLeftEdit = new Edit();
    footerLeftEdit->Create(fLArgs);
    fRow->AddChild(footerLeftEdit, 1);

    Edit::CreateArgs fCArgs = hLArgs;
    fCArgs.text = StrL("Page {page} of {pages}");
    footerCenterEdit = new Edit();
    footerCenterEdit->Create(fCArgs);
    fRow->AddChild(footerCenterEdit, 1);

    Edit::CreateArgs fRArgs = hLArgs;
    footerRightEdit = new Edit();
    footerRightEdit->Create(fRArgs);
    fRow->AddChild(footerRightEdit, 1);

    HBox* hintRow = AddRow();
    hintRow->AddChild(
        NewVirtText({.s = Tr("Tokens: {page}, {pages}, {date}, {filename}"), .font = font, .isRtl = IsUIRtl()}));

    HBox* pRow = AddRow();
    pRow->gap = font->averageCharWidth;
    pRow->AddChild(NewVirtText({.s = Tr("Pages:"), .font = font, .isRtl = IsUIRtl()}));

    radioAll = NewPagesRadio(hwnd, font, Tr("All"), true, true);
    radioAll->onStateChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::OnPagesModeChanged>(this);
    pRow->AddChild(radioAll);

    radioCurrent = NewPagesRadio(hwnd, font, Tr("Current"), false, false);
    radioCurrent->onStateChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::OnPagesModeChanged>(this);
    pRow->AddChild(radioCurrent);

    radioCustom = NewPagesRadio(hwnd, font, Tr("Custom"), false, false);
    radioCustom->onStateChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::OnPagesModeChanged>(this);
    pRow->AddChild(radioCustom);

    Edit::CreateArgs peArgs;
    peArgs.parent = hwnd;
    peArgs.withBorder = true;
    peArgs.font = font;
    peArgs.text = fmt("1-%d", pageCount);
    peArgs.isRtl = IsUIRtl();
    pagesEdit = new Edit();
    pagesEdit->Create(peArgs);
    pagesEdit->SetIsEnabled(false);
    pRow->AddChild(pagesEdit, 1);

    AddButtonsRow(Tr("Add Header / Footer"));
    FinishDialog(footerCenterEdit);

    headerLeftEdit->onTextChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::UpdateButton>(this);
    headerCenterEdit->onTextChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::UpdateButton>(this);
    headerRightEdit->onTextChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::UpdateButton>(this);
    footerLeftEdit->onTextChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::UpdateButton>(this);
    footerCenterEdit->onTextChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::UpdateButton>(this);
    footerRightEdit->onTextChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::UpdateButton>(this);
    pagesEdit->onTextChanged = MkMethod0<PdfHeaderFooterDialog, &PdfHeaderFooterDialog::UpdateButton>(this);
    UpdateButton();
    return true;
}

void ShowPdfAddHeaderFooterDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0 || !IsPdfDoc(tab)) {
        return;
    }
    logf("ShowPdfAddHeaderFooterDialog: opening for '%s'\n", tab->filePath);
    auto* dlg = new PdfHeaderFooterDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

// --- Bates Numbering Dialog ---

struct PdfBatesDialog : PdfToolDialog {
    Edit* prefixEdit = nullptr;
    Edit* startNumEdit = nullptr;
    Edit* digitsEdit = nullptr;
    DropDown* dropPos = nullptr;

    Checkbox* radioAll = nullptr;
    Checkbox* radioCurrent = nullptr;
    Checkbox* radioCustom = nullptr;
    Edit* pagesEdit = nullptr;

    int pageCount = 0;
    int currentPage = 1;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
    void OnPagesModeChanged();
    void UpdateButton();
};

void PdfBatesDialog::OnPagesModeChanged() {
    bool custom = radioCustom && radioCustom->IsChecked();
    if (pagesEdit) {
        pagesEdit->SetIsEnabled(custom);
    }
    UpdateButton();
}

void PdfBatesDialog::UpdateButton() {
    if (!actionBtn) return;
    bool ok = true;
    if (radioCustom && radioCustom->IsChecked()) {
        TempStr pages = pagesEdit ? pagesEdit->GetTextTemp() : TempStr{};
        Vec<int> p;
        ok = ok && ParseDeletePages(pages, pageCount, p);
    }
    actionBtn->SetFlag(vwfEnabled, ok);
}

void PdfBatesDialog::DoIt(VirtMouseEvent*) {
    PdfToolEdit edit(win, srcPath);
    if (!edit.IsReady()) return;
    Str destPath = edit.outputPath;
    if (len(destPath) == 0) {
        return;
    }

    PdfBatesParams params;
    params.prefix = prefixEdit ? prefixEdit->GetTextTemp() : TempStr{};

    TempStr stStr = startNumEdit ? startNumEdit->GetTextTemp() : TempStr{};
    int stVal = 1;
    str::Parse(stStr, "%d", &stVal);
    params.startNumber = std::max(0, stVal);

    TempStr digStr = digitsEdit ? digitsEdit->GetTextTemp() : TempStr{};
    int digVal = 6;
    str::Parse(digStr, "%d", &digVal);
    params.digitCount = std::max(1, std::min(12, digVal));

    int posIdx = dropPos ? CbGetCurrentSelection(dropPos) : 0;
    params.position = (PdfBatesPos)posIdx;

    if (radioCurrent && radioCurrent->IsChecked()) {
        params.pagesSpec = fmt("%d", currentPage);
    } else if (radioCustom && radioCustom->IsChecked()) {
        params.pagesSpec = pagesEdit ? pagesEdit->GetTextTemp() : TempStr{};
    } else {
        params.pagesSpec = StrL("all");
    }

    Str inputPath = edit.inputPath;

    logf("PdfBatesDialog::DoIt: applying bates numbering to '%s'\n", destPath);
    bool ok = PdfApplyBatesNumbering(inputPath, destPath, params);

    if (ok) {
        if (edit.Apply("Bates numbering")) Close();
    } else {
        MessageBoxWarning(hwnd, StrL("Failed to add Bates numbering to PDF file."), Tr("Bates Numbering"));
    }
}

bool PdfBatesDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Bates Numbering"))) {
        return false;
    }
    pageCount = w->ctrl ? w->ctrl->PageCount() : 1;
    currentPage = w->ctrl ? w->ctrl->CurrentPageNo() : 1;

    AddPathRow();

    HBox* pRow = AddRow();
    pRow->gap = font->averageCharWidth;

    pRow->AddChild(NewVirtText({.s = Tr("Prefix:"), .font = font, .isRtl = IsUIRtl()}));
    Edit::CreateArgs pfxArgs;
    pfxArgs.parent = hwnd;
    pfxArgs.withBorder = true;
    pfxArgs.font = font;
    pfxArgs.text = StrL("BATES-");
    pfxArgs.isRtl = IsUIRtl();
    prefixEdit = new Edit();
    prefixEdit->Create(pfxArgs);
    pRow->AddChild(prefixEdit, 1);

    pRow->AddChild(new Spacer(gap, 0));
    pRow->AddChild(NewVirtText({.s = Tr("Start:"), .font = font, .isRtl = IsUIRtl()}));
    Edit::CreateArgs stArgs = pfxArgs;
    stArgs.text = StrL("1");
    startNumEdit = new Edit();
    startNumEdit->Create(stArgs);
    pRow->AddChild(startNumEdit, 1);

    pRow->AddChild(new Spacer(gap, 0));
    pRow->AddChild(NewVirtText({.s = Tr("Digits:"), .font = font, .isRtl = IsUIRtl()}));
    Edit::CreateArgs digArgs = pfxArgs;
    digArgs.text = StrL("6");
    digitsEdit = new Edit();
    digitsEdit->Create(digArgs);
    pRow->AddChild(digitsEdit, 1);

    HBox* posRow = AddRow();
    posRow->gap = font->averageCharWidth;
    posRow->AddChild(NewVirtText({.s = Tr("Position:"), .font = font, .isRtl = IsUIRtl()}));
    {
        dropPos = new DropDown();
        DropDown::CreateArgs ddargs;
        ddargs.parent = hwnd;
        ddargs.font = font;
        ddargs.isRtl = IsUIRtl();
        dropPos->Create(ddargs);
        StrVec posItems;
        posItems.Append(Tr("Bottom-Right"));
        posItems.Append(Tr("Bottom-Center"));
        posItems.Append(Tr("Bottom-Left"));
        posItems.Append(Tr("Top-Right"));
        posItems.Append(Tr("Top-Center"));
        posItems.Append(Tr("Top-Left"));
        dropPos->SetItems(posItems);
        CbSetCurrentSelection(dropPos, 0);
        dropPos->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
        posRow->AddChild(dropPos, 1);
    }

    HBox* pgRow = AddRow();
    pgRow->gap = font->averageCharWidth;
    pgRow->AddChild(NewVirtText({.s = Tr("Pages:"), .font = font, .isRtl = IsUIRtl()}));

    radioAll = NewPagesRadio(hwnd, font, Tr("All"), true, true);
    radioAll->onStateChanged = MkMethod0<PdfBatesDialog, &PdfBatesDialog::OnPagesModeChanged>(this);
    pgRow->AddChild(radioAll);

    radioCurrent = NewPagesRadio(hwnd, font, Tr("Current"), false, false);
    radioCurrent->onStateChanged = MkMethod0<PdfBatesDialog, &PdfBatesDialog::OnPagesModeChanged>(this);
    pgRow->AddChild(radioCurrent);

    radioCustom = NewPagesRadio(hwnd, font, Tr("Custom"), false, false);
    radioCustom->onStateChanged = MkMethod0<PdfBatesDialog, &PdfBatesDialog::OnPagesModeChanged>(this);
    pgRow->AddChild(radioCustom);

    Edit::CreateArgs peArgs;
    peArgs.parent = hwnd;
    peArgs.withBorder = true;
    peArgs.font = font;
    peArgs.text = fmt("1-%d", pageCount);
    peArgs.isRtl = IsUIRtl();
    pagesEdit = new Edit();
    pagesEdit->Create(peArgs);
    pagesEdit->SetIsEnabled(false);
    pgRow->AddChild(pagesEdit, 1);

    AddButtonsRow(Tr("Add Bates Numbering"));
    FinishDialog(prefixEdit);

    prefixEdit->onTextChanged = MkMethod0<PdfBatesDialog, &PdfBatesDialog::UpdateButton>(this);
    startNumEdit->onTextChanged = MkMethod0<PdfBatesDialog, &PdfBatesDialog::UpdateButton>(this);
    digitsEdit->onTextChanged = MkMethod0<PdfBatesDialog, &PdfBatesDialog::UpdateButton>(this);
    pagesEdit->onTextChanged = MkMethod0<PdfBatesDialog, &PdfBatesDialog::UpdateButton>(this);
    UpdateButton();
    return true;
}

void ShowPdfBatesNumberingDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0 || !IsPdfDoc(tab)) {
        return;
    }
    logf("ShowPdfBatesNumberingDialog: opening for '%s'\n", tab->filePath);
    auto* dlg = new PdfBatesDialog();
    if (!dlg->Create(win, tab)) {
        delete dlg;
    }
}

struct PdfPrivacyDialog : PdfToolDialog {
    bool redact = false;
    Checkbox* options[7]{};
    Checkbox* regex = nullptr;
    Edit* term = nullptr;
    Edit* preview = nullptr;
    Edit* selection = nullptr;
    Edit* overlay = nullptr;
    Edit* reason = nullptr;
    Str reviewPath;
    Vec<PdfRedactMatch> matches;

    ~PdfPrivacyDialog() {
        if (len(reviewPath)) file::Delete(reviewPath);
        str::Free(reviewPath);
    }
    void ReviewRedactions(WindowTab* tab);
    void OnPattern(VirtMouseEvent* ev);

    bool Create(MainWindow* w, WindowTab* tab, bool isRedact);
    Checkbox* AddCheck(Str label, bool checked);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

Checkbox* PdfPrivacyDialog::AddCheck(Str label, bool checked) {
    Checkbox::CreateArgs args;
    args.parent = hwnd;
    args.font = font;
    args.text = label;
    args.isRtl = IsUIRtl();
    args.initialState = checked ? Checkbox::State::Checked : Checkbox::State::Unchecked;
    auto* check = new Checkbox();
    check->Create(args);
    check->SetColors(ThemeWindowTextColor(), DarkModeDialogBgColor());
    AddRow()->AddChild(check);
    return check;
}

bool PdfPrivacyDialog::Create(MainWindow* w, WindowTab* tab, bool isRedact) {
    redact = isRedact;
    if (!CreateToolDialog(w, tab, redact ? Tr("Search and Redact") : Tr("Sanitize Document"))) return false;
    AddPathRow();
    Str stem = srcPath;
    if (str::EndsWithI(stem, StrL(".pdf"))) stem.len -= 4;
    AddDestRow(MakeUniqueFilePathTemp(fmt("%s_%s.pdf", stem, redact ? StrL("redacted") : StrL("sanitized"))),
               WStrL(L"PDF Files\0*.pdf\0All Files\0*.*\0"), WStrL(L"pdf"));
    if (redact) {
        term = AddLabeledEdit(Tr("Find:"), {});
        regex = AddCheck(Tr("Regular expression (case sensitive)"), false);
        HBox* presets = AddRow();
        for (int i = 0; i < 2; i++) {
            auto* button = NewButton(i ? Tr("SSN pattern") : Tr("Email pattern"), false);
            button->id = i;
            button->onClick = MkMethod1<PdfPrivacyDialog, VirtMouseEvent*, &PdfPrivacyDialog::OnPattern>(this);
            presets->AddChild(button);
        }
        Edit::CreateArgs args;
        args.parent = hwnd;
        args.font = font;
        args.isMultiLine = true;
        args.withBorder = true;
        args.idealSizeLines = 6;
        preview = new Edit();
        preview->Create(args);
        SendMessageW(preview->hwnd, EM_SETREADONLY, TRUE, 0);
        AddRow()->AddChild(preview, 1);
        selection = AddLabeledEdit(Tr("Match IDs (comma separated):"), {});
        reason = AddLabeledEdit(Tr("Reason (used as label if overlay is empty):"), {});
        overlay = AddLabeledEdit(Tr("Overlay text (optional):"), {});
        AddRow()->AddChild(
            NewVirtText({.s = Tr("Apply removes metadata, attachments, scripts, comments and tags."), .font = font}));
        AddRow()->AddChild(NewVirtText(
            {.s = Tr("Searches all pages, including invisible text. Images are not OCR searched."), .font = font}));
        AddRow()->AddChild(NewVirtText(
            {.s = Tr("Matching areas lose text, touching images and graphics. Save a separate copy."), .font = font}));
        AddRow()->AddChild(NewVirtText({.s = Tr("Form fields are flattened before searching."), .font = font}));
    } else {
        options[0] = AddCheck(Tr("Metadata"), true);
        options[1] = AddCheck(Tr("Hidden text/layers (rasterizes pages; removes forms, comments and links)"), false);
        options[2] = AddCheck(Tr("Embedded JavaScript (also removes XFA form packets)"), true);
        options[3] = AddCheck(Tr("Attachments"), true);
        options[4] = AddCheck(Tr("Comments"), true);
        options[5] = AddCheck(Tr("Form data and fields"), true);
        options[6] = AddCheck(Tr("Thumbnails"), true);
    }
    AddButtonsRow(redact ? Tr("Preview Matches") : Tr("Sanitize"));
    FinishDialog(redact ? term : destEdit, 760);
    return true;
}

void PdfPrivacyDialog::DoIt(VirtMouseEvent*) {
    TempStr dest = destEdit->GetTextTemp();
    if (!len(dest)) return;
    if (path::IsSame(dest, srcPath)) {
        MessageBoxWarning(hwnd, Tr("Choose a different file name for the saved copy."), Tr("Save Copy"));
        return;
    }
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!tab || !path::IsSame(tab->filePath, srcPath)) return;
    if (redact) {
        ReviewRedactions(tab);
        return;
    }
    EngineBase* engine = tab->GetEngine();
    TempStr snapshot;
    Str input = srcPath;
    if (engine && EngineHasUnsavedAnnotations(engine)) {
        snapshot = GetTempFilePathTemp(StrL("privacy-input"));
        if (!len(snapshot) || !EngineMupdfSaveCopy(engine, snapshot)) {
            file::Delete(snapshot);
            MessageBoxWarning(hwnd, Tr("Failed to save copy of PDF file."), Tr("Save Copy"));
            return;
        }
        input = snapshot;
    }
    PdfMergeSource source{input, EngineMupdfGetPassword(engine)};
    Str error;
    int count = 0;
    bool ok;
    if (redact) {
        ok = EngineMupdfRedactMatches(source, dest, term->GetTextTemp(),
                                      regex->IsChecked() ? PdfSearchMode::Regex : PdfSearchMode::Text, &count, &error);
    } else {
        PdfSanitizeOptions opts;
        opts.metadata = options[0]->IsChecked();
        opts.hiddenContent = options[1]->IsChecked();
        opts.javascript = options[2]->IsChecked();
        opts.attachments = options[3]->IsChecked();
        opts.comments = options[4]->IsChecked();
        opts.forms = options[5]->IsChecked();
        opts.thumbnails = options[6]->IsChecked();
        ok = EngineMupdfSanitizePdf(source, dest, opts, &error);
    }
    if (len(snapshot)) file::Delete(snapshot);
    if (!ok) {
        MessageBoxWarning(hwnd, len(error) ? error : Tr("Failed to save PDF copy."),
                          redact ? Tr("Search and Redact") : Tr("Sanitize Document"));
        return;
    }
    MainWindow* w = win;
    TempStr saved = str::DupTemp(dest);
    bool wasRedact = redact;
    Close();
    LoadArgs args(saved, w);
    StartLoadDocument(&args);
    if (wasRedact)
        ShowTemporaryNotification(w->hwndCanvas, fmt(Tr("Redacted %d matches. Saved a separate copy.").s, count));
}

void PdfPrivacyDialog::OnPattern(VirtMouseEvent* ev) {
    if (len(reviewPath)) return;
    term->SetText(ev->target->id ? StrL("[0-9]{3}-[0-9]{2}-[0-9]{4}")
                                 : StrL("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"));
    regex->SetState(Checkbox::State::Checked);
}

void PdfPrivacyDialog::ReviewRedactions(WindowTab* tab) {
    Str error;
    if (!len(reviewPath)) {
        TempStr snapshot = GetTempFilePathTemp(StrL("redact-review"));
        if (!EngineMupdfSaveCopy(tab->GetEngine(), snapshot)) {
            file::Delete(snapshot);
            MessageBoxWarning(hwnd, Tr("Failed to save copy of PDF file."), Tr("Redaction"));
            return;
        }
        PdfMergeSource source{snapshot, EngineMupdfGetPassword(tab->GetEngine())};
        if (!EngineMupdfPreviewRedactions(source, term->GetTextTemp(),
                                          regex->IsChecked() ? PdfSearchMode::Regex : PdfSearchMode::Text, matches,
                                          &error)) {
            file::Delete(snapshot);
            MessageBoxWarning(hwnd, error, Tr("Redaction"));
            return;
        }
        if (!len(matches)) {
            file::Delete(snapshot);
            preview->SetText(Tr("No matches. Try another search."));
            return;
        }
        reviewPath = str::Dup(snapshot);
        str::Builder rows, ids;
        for (int i = 0; i < len(matches); i++) {
            const auto& m = matches[i];
            rows.Append(fmt("%d: page %d  box [%.2f, %.2f, %.2f, %.2f]\r\n", i + 1, m.pageNo, m.box.x, m.box.y,
                            m.box.x + m.box.dx, m.box.y + m.box.dy));
            if (i) ids.AppendChar(',');
            ids.Append(fmt("%d", i + 1));
        }
        preview->SetText(ToStr(rows));
        selection->SetText(ToStr(ids));
        term->SetIsEnabled(false);
        regex->SetIsEnabled(false);
        actionBtn->SetText(Tr("Apply Selected Matches"));
        return;
    }
    Vec<int> selected;
    const char* cursor = CStrTemp(selection->GetTextTemp());
    while (*cursor) {
        char* end;
        long id = strtol(cursor, &end, 10);
        if (end == cursor || id < 1 || id > len(matches) || (*end && *end != ',')) {
            MessageBoxWarning(hwnd, Tr("Enter match IDs from the preview, separated by commas."), Tr("Redaction"));
            return;
        }
        if (VecFind(selected, (int)id) < 0) VecAppend(selected, (int)id);
        cursor = *end ? end + 1 : end;
        if (*end && !*cursor) return;
    }
    PdfMergeSource source{reviewPath, EngineMupdfGetPassword(tab->GetEngine())};
    int count = 0;
    TempStr dest = destEdit->GetTextTemp();
    if (!EngineMupdfRedactMatches(source, dest, term->GetTextTemp(),
                                  regex->IsChecked() ? PdfSearchMode::Regex : PdfSearchMode::Text, &count, &error,
                                  &selected, overlay->GetTextTemp(), reason->GetTextTemp())) {
        MessageBoxWarning(hwnd, error, Tr("Redaction"));
        return;
    }
    MainWindow* w = win;
    TempStr saved = str::DupTemp(dest);
    Close();
    LoadArgs args(saved, w);
    StartLoadDocument(&args);
    ShowTemporaryNotification(w->hwndCanvas, fmt(Tr("Redacted %d matches. Saved a separate copy.").s, count));
}

void ShowPdfSanitizeDialog(MainWindow* win) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dialog = new PdfPrivacyDialog();
    if (!dialog->Create(win, tab, false)) delete dialog;
}

void ShowPdfRedactSearchDialog(MainWindow* win) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dialog = new PdfPrivacyDialog();
    if (!dialog->Create(win, tab, true)) delete dialog;
}

struct PdfObjectDialog : PdfToolDialog {
    int pageNo = 1;
    Edit* objectName = nullptr;
    Edit* imagePath = nullptr;
    Edit* scaleX = nullptr;
    Edit* scaleY = nullptr;
    Edit* rotation = nullptr;

    bool Create(MainWindow* w, WindowTab* tab) {
        if (!CreateToolDialog(w, tab, Tr("Edit Images and Objects"))) return false;
        pageNo = tab->ctrl->CurrentPageNo();
        AddPathRow();
        AddDestRow(MakeUniqueFilePathTemp(srcPath), WStrL(L"PDF Files\0*.pdf\0All Files\0*.*\0"), WStrL(L"pdf"));
        AddRow()->AddChild(NewVirtText({.s = fmt(Tr("Objects on page %d:").s, pageNo), .font = font}));
        Edit::CreateArgs args;
        args.parent = hwnd;
        args.font = font;
        args.isMultiLine = true;
        args.withBorder = true;
        args.idealSizeLines = 5;
        args.text = EngineMupdfObjectList(tab->GetEngine(), pageNo);
        auto* list = new Edit();
        list->Create(args);
        SendMessageW(list->hwnd, EM_SETREADONLY, TRUE, 0);
        AddRow()->AddChild(list, 1);
        objectName = AddLabeledEdit(Tr("Object name (from list):"), {});
        imagePath = AddLabeledEdit(Tr("Replacement image file (optional):"), {});
        scaleX = AddLabeledEdit(Tr("Width percent (negative flips):"), StrL("100"));
        scaleY = AddLabeledEdit(Tr("Height percent (negative flips):"), StrL("100"));
        rotation = AddLabeledEdit(Tr("Rotation degrees:"), StrL("0"));
        AddRow()->AddChild(NewVirtText(
            {.s = Tr("Changes every use of this object on this page. Other pages stay unchanged."), .font = font}));
        AddButtonsRow(Tr("Save Edited Copy"));
        FinishDialog(objectName, 740);
        return true;
    }

    void DoIt(VirtMouseEvent*) override {
        WindowTab* tab = win ? win->CurrentTab() : nullptr;
        if (!tab || !path::IsSame(tab->filePath, srcPath)) return;
        int x = 0, y = 0, angle = 0;
        if (str::IsNull(str::Parse(scaleX->GetTextTemp(), "%d%$", &x)) ||
            str::IsNull(str::Parse(scaleY->GetTextTemp(), "%d%$", &y)) ||
            str::IsNull(str::Parse(rotation->GetTextTemp(), "%d%$", &angle))) {
            MessageBoxWarning(hwnd, Tr("Enter whole numbers for scale and rotation."), Tr("Edit Images and Objects"));
            return;
        }
        Str error;
        Str destination = destEdit->GetTextTemp();
        if (!EngineMupdfEditObjectCopy(tab->GetEngine(), destination, pageNo, objectName->GetTextTemp(),
                                       imagePath->GetTextTemp(), x / 100.f, y / 100.f, (float)angle, &error)) {
            MessageBoxWarning(hwnd, error, Tr("Edit Images and Objects"));
            return;
        }
        MainWindow* w = win;
        TempStr saved = str::DupTemp(destination);
        Close();
        LoadArgs args(saved, w);
        StartLoadDocument(&args);
    }
};

void ShowPdfObjectDialog(MainWindow* win) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dialog = new PdfObjectDialog();
    if (!dialog->Create(win, tab)) delete dialog;
}

struct PdfFontDialog : PdfToolDialog {
    Edit* family = nullptr;

    bool Create(MainWindow* w, WindowTab* tab) {
        if (!CreateToolDialog(w, tab, Tr("Font Manager"))) return false;
        AddPathRow();
        Edit::CreateArgs args;
        args.parent = hwnd;
        args.font = font;
        args.isMultiLine = true;
        args.withBorder = true;
        args.idealSizeLines = 10;
        args.text = str::ReplaceTemp(tab->GetEngine()->GetPropertyTemp(DocProp::FontList), StrL("\n"), StrL("\r\n"));
        auto* list = new Edit();
        list->Create(args);
        SendMessageW(list->hwnd, EM_SETREADONLY, TRUE, 0);
        AddRow()->AddChild(list, 1);
        AddRow()->AddChild(NewVirtText({.s = Tr("Fonts without an embedded label are not embedded."), .font = font}));
        family = AddLabeledEdit(Tr("Installed font family:"), StrL("Arial"));
        auto* choose = NewButton(Tr("Choose..."), false);
        choose->onClick = MkMethod1<PdfFontDialog, VirtMouseEvent*, &PdfFontDialog::OnChoose>(this);
        lastRow->AddChild(choose);
        AddRow()->AddChild(NewVirtText(
            {.s = Tr("Select text on one page. Replacement affects complete selected lines."), .font = font}));
        AddButtonsRow(Tr("Replace Selection Font"));
        FinishDialog(family, 700);
        return true;
    }

    void OnChoose(VirtMouseEvent*) {
        LOGFONTW lf{};
        wstr::BufSet(WStr(lf.lfFaceName, LF_FACESIZE), ToWStrTemp(family->GetTextTemp()));
        CHOOSEFONTW choice{};
        choice.lStructSize = sizeof(choice);
        choice.hwndOwner = hwnd;
        choice.lpLogFont = &lf;
        choice.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
        if (ChooseFontW(&choice)) family->SetText(ToUtf8Temp(lf.lfFaceName));
    }

    void DoIt(VirtMouseEvent*) override {
        WindowTab* tab = win ? win->CurrentTab() : nullptr;
        if (!tab || !path::IsSame(tab->filePath, srcPath)) return;
        auto* selection = tab->selectionOnPage;
        if (!selection || !len(*selection)) {
            MessageBoxWarning(hwnd, Tr("Select the text lines to change first."), Tr("Font Manager"));
            return;
        }
        int pageNo = (*selection)[0].pageNo;
        RectF area = (*selection)[0].rect;
        for (const SelectionOnPage& part : *selection) {
            if (part.pageNo != pageNo) {
                MessageBoxWarning(hwnd, Tr("Select text on one page only."), Tr("Font Manager"));
                return;
            }
            area = area.Union(part.rect);
        }
        PdfTextRun run;
        Str name = family->GetTextTemp();
        if (!len(name) || len(name) >= sizeof(run.replacementFont)) return;
        if (!EngineMupdfGetTextRun(tab->GetEngine(), pageNo, area, TextRunScope::Lines, &run)) {
            str::Free(run.text);
            return;
        }
        str::BufSet(Str(run.replacementFont, sizeof(run.replacementFont)), name);
        bool ok = ReplaceTextInTab(tab, pageNo, run, run.text);
        str::Free(run.text);
        if (ok) Close();
    }
};

void ShowPdfFontManager(MainWindow* win) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dialog = new PdfFontDialog();
    if (!dialog->Create(win, tab)) delete dialog;
}

struct ReplaceJob {
    AtomicInt refs = 1;
    AtomicInt done = 0;
    PdfWorkProgress progress;
    EngineBase* engine = nullptr;
    Str destination, find, replacement, error;
    int count = 0;
    bool ok = false;
    ~ReplaceJob() {
        SafeEngineRelease(&engine);
        str::Free(destination);
        str::Free(find);
        str::Free(replacement);
        str::Free(error);
    }
    void Release() {
        if (AtomicIntDec(&refs) == 0) delete this;
    }
};

static void RunReplace(ReplaceJob* job) {
    Str error;
    job->ok = EngineMupdfReplaceAllText(job->engine, job->destination, job->find, job->replacement, &job->count, &error,
                                        &job->progress);
    job->error = str::Dup(error);
    AtomicIntSet(&job->done, 1);
    job->Release();
}

struct PdfReplaceDialog : PdfToolDialog {
    Edit* find = nullptr;
    Edit* replacement = nullptr;
    VirtFixedLinesText* status = nullptr;
    ReplaceJob* job = nullptr;
    ~PdfReplaceDialog() override {
        if (job) {
            AtomicIntSet(&job->progress.cancelled, 1);
            job->Release();
        }
    }
    void Closed(WindowBase::CloseEvent*) {
        if (job && !AtomicIntGet(&job->done)) AtomicIntSet(&job->progress.cancelled, 1);
        if (IsMainWindowValidAndNotClosing(win)) SetActiveWindow(win->hwndFrame);
        ScheduleDelete();
    }
    void Cancel(VirtMouseEvent*) {
        if (job && !AtomicIntGet(&job->done)) {
            AtomicIntSet(&job->progress.cancelled, 1);
            status->SetText(Tr("Cancelling..."));
        } else
            Close();
    }
    void Poll(WindowBase::TimerEvent*) {
        if (!job) return;
        if (!AtomicIntGet(&job->done)) {
            if (!job->progress.IsCancelled())
                status->SetText(fmt(Tr("Replacing: %d of %d pages").s, AtomicIntGet(&job->progress.completed),
                                    AtomicIntGet(&job->progress.total)));
            return;
        }
        KillTimer(hwnd, 1);
        actionBtn->SetIsEnabled(true);
        cancelBtn->SetText(Tr("Close"));
        if (!job->ok) {
            status->SetText(len(job->error) ? job->error : Tr("Replacement failed. No copy was written."));
            return;
        }
        MainWindow* w = IsMainWindowValidAndNotClosing(win) ? win : nullptr;
        TempStr saved = str::DupTemp(job->destination);
        int count = job->count;
        Close();
        LoadArgs args(saved, w);
        StartLoadDocument(&args);
        if (w)
            ShowTemporaryNotification(w->hwndCanvas, fmt(Tr("Replaced %d matches. Saved a separate copy.").s, count));
    }

    bool Create(MainWindow* w, WindowTab* tab) {
        if (!CreateToolDialog(w, tab, Tr("Find and Replace Text"))) return false;
        onClose = MkMethod1<PdfReplaceDialog, WindowBase::CloseEvent*, &PdfReplaceDialog::Closed>(this);
        AddPathRow();
        Str stem = srcPath;
        if (str::EndsWithI(stem, StrL(".pdf"))) stem.len -= 4;
        AddDestRow(MakeUniqueFilePathTemp(fmt("%s_replaced.pdf", stem)), WStrL(L"PDF Files\0*.pdf\0All Files\0*.*\0"),
                   WStrL(L"pdf"));
        find = AddLabeledEdit(Tr("Find (case sensitive):"), {});
        replacement = AddLabeledEdit(Tr("Replace with:"), {});
        AddRow()->AddChild(NewVirtText(
            {.s = Tr("Matches within paragraphs on all pages. Empty replacement deletes matches."), .font = font}));
        AddRow()->AddChild(NewVirtText(
            {.s = Tr("Unsupported blocks stop the operation. The original document is kept."), .font = font}));
        status = new VirtFixedLinesText();
        status->font = font;
        status->nLines = 2;
        AddRow()->AddChild(status, 1);
        AddButtonsRow(Tr("Replace All in Copy"));
        cancelBtn->onClick = MkMethod1<PdfReplaceDialog, VirtMouseEvent*, &PdfReplaceDialog::Cancel>(this);
        onTimer = MkMethod1<PdfReplaceDialog, WindowBase::TimerEvent*, &PdfReplaceDialog::Poll>(this);
        FinishDialog(find, 740);
        return true;
    }

    void DoIt(VirtMouseEvent*) override {
        WindowTab* tab = IsMainWindowValidAndNotClosing(win) ? win->CurrentTab() : nullptr;
        if (!tab || !path::IsSame(tab->filePath, srcPath)) return;
        if (job && !AtomicIntGet(&job->done)) return;
        if (job) job->Release();
        job = new ReplaceJob();
        job->engine = tab->GetEngine();
        job->engine->AddRef();
        job->destination = str::Dup(destEdit->GetTextTemp());
        job->find = str::Dup(find->GetTextTemp());
        job->replacement = str::Dup(replacement->GetTextTemp());
        actionBtn->SetIsEnabled(false);
        cancelBtn->SetText(Tr("Cancel"));
        status->SetText(Tr("Preparing replacement copy..."));
        AtomicIntInc(&job->refs);
        ThreadHandle thread = StartThread(MkFunc0(RunReplace, job), StrL("PDF replace"));
        if (!thread) {
            AtomicIntSet(&job->done, 1);
            job->Release();
        }
        SafeCloseThreadHandle(&thread);
        SetTimer(hwnd, 1, 150, nullptr);
    }
};

void ShowPdfReplaceTextDialog(MainWindow* win) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dialog = new PdfReplaceDialog();
    if (!dialog->Create(win, tab)) delete dialog;
}

struct ConversionJob {
    volatile LONG refs = 1;
    volatile LONG done = 0;
    ConversionProgress progress;
    StrVec sources;
    Str destination;
    Str temporaryInput;
    Str range;
    ExportFormat format = ExportFormat::Docx;
    bool create = false;
    bool ok = false;
    ~ConversionJob() {
        if (len(temporaryInput)) file::Delete(temporaryInput);
        str::Free(temporaryInput);
        str::Free(destination);
        str::Free(range);
    }
    void Release() {
        if (InterlockedDecrement(&refs) == 0) delete this;
    }
};

static void RunConversion(ConversionJob* job) {
    job->ok = job->create ? CreatePdfFiles(job->sources, job->destination, job->range, job->progress)
                          : ExportPdf(job->sources[0], job->destination, job->format, job->range, job->progress);
    InterlockedExchange(&job->done, 1);
    job->Release();
}

struct ExportPdfDialog : PdfToolDialog {
    DropDown* formats = nullptr;
    Edit* range = nullptr;
    VirtText* status = nullptr;
    ConversionJob* job = nullptr;
    StrVec inputs;
    bool create = false;
    Str clipboardPath;
    ~ExportPdfDialog() override {
        if (job) {
            InterlockedExchange(&job->progress.cancelled, 1);
            job->Release();
        }
        if (len(clipboardPath)) file::Delete(clipboardPath);
        str::Free(clipboardPath);
    }
    void ChangeFormat();
    void DoIt(VirtMouseEvent*) override;
    void Poll(WindowBase::TimerEvent*);
    void Cancel(VirtMouseEvent*);
    bool Create(MainWindow*, bool fromClipboard);
};

static const char* kExportExtensions[] = {"docx", "pptx", "xlsx", "html", "rtf", "md", "txt", "json", "zip"};
void ExportPdfDialog::ChangeFormat() {
    int index = CbGetCurrentSelection(formats);
    if (index < 0 || index >= dimof(kExportExtensions)) return;
    destEdit->SetText(fmt("%s.%s", path::GetPathNoExtTemp(destEdit->GetTextTemp()), Str(kExportExtensions[index])));
    // Browse extensions must outlive the temporary string arena.
    browseDefExt = index == 0   ? WStrL(L"docx")
                   : index == 1 ? WStrL(L"pptx")
                   : index == 2 ? WStrL(L"xlsx")
                   : index == 3 ? WStrL(L"html")
                   : index == 4 ? WStrL(L"rtf")
                   : index == 5 ? WStrL(L"md")
                   : index == 6 ? WStrL(L"txt")
                   : index == 7 ? WStrL(L"json")
                                : WStrL(L"zip");
}
void ExportPdfDialog::Cancel(VirtMouseEvent*) {
    if (job && !InterlockedCompareExchange(&job->done, 0, 0)) {
        InterlockedExchange(&job->progress.cancelled, 1);
        status->SetText(Tr("Cancelling... Office may need to finish its current call."));
        return;
    }
    Close();
}
void ExportPdfDialog::Poll(WindowBase::TimerEvent*) {
    if (!job) return;
    if (!InterlockedCompareExchange(&job->done, 0, 0)) {
        if (!InterlockedCompareExchange(&job->progress.cancelled, 0, 0))
            status->SetText(fmt(Tr("Converting: %d of %d").s,
                                (int)InterlockedCompareExchange(&job->progress.completed, 0, 0),
                                (int)InterlockedCompareExchange(&job->progress.total, 0, 0)));
        return;
    }
    KillTimer(hwnd, 1);
    actionBtn->SetIsEnabled(true);
    cancelBtn->SetText(Tr("Close"));
    status->SetText(job->ok ? Tr("Saved. Review the converted document before using it.") : job->progress.error);
    if (!job->ok) MessageBoxWarning(hwnd, job->progress.error, Tr("Conversion"));
}
void ExportPdfDialog::DoIt(VirtMouseEvent*) {
    if (job && !InterlockedCompareExchange(&job->done, 0, 0)) return;
    if (job) {
        job->Release();
        job = nullptr;
    }
    Str destination = destEdit->GetTextTemp();
    if (!len(destination)) {
        status->SetText(Tr("Choose a destination file."));
        return;
    }
    if (file::Exists(destination) && MessageBoxW(hwnd, ToWStrTemp(Tr("Replace the existing destination file?")).s,
                                                 ToWStrTemp(Tr("Conversion")).s, MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    job = new ConversionJob();
    job->create = create;
    job->destination = str::Dup(destination);
    job->range = str::Dup(range->GetTextTemp());
    if (len(clipboardPath)) {
        job->temporaryInput = str::Dup(GetTempFilePathTemp(StrL("conversion.bmp")));
        if (!CopyFileW(ToWStrTemp(clipboardPath).s, ToWStrTemp(job->temporaryInput).s, FALSE)) {
            job->Release();
            job = nullptr;
            status->SetText(Tr("Cannot copy the clipboard image for conversion."));
            return;
        }
        job->sources.Append(job->temporaryInput);
    } else
        for (Str input : inputs) job->sources.Append(input);
    if (!create) job->format = (ExportFormat)CbGetCurrentSelection(formats);
    actionBtn->SetIsEnabled(false);
    cancelBtn->SetText(Tr("Cancel"));
    status->SetText(Tr("Starting conversion..."));
    InterlockedIncrement(&job->refs);
    ThreadHandle thread = StartThread(MkFunc0(RunConversion, job), StrL("PDF conversion"));
    if (!thread) {
        job->progress.error = str::Dup(Tr("Could not start conversion."));
        InterlockedExchange(&job->done, 1);
        job->Release();
    }
    SafeCloseThreadHandle(&thread);
    SetTimer(hwnd, 1, 150, nullptr);
}

static bool ChooseCreateInputs(HWND owner, StrVec& paths) {
    WCHAR files[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = files;
    ofn.nMaxFile = dimof(files);
    ofn.lpstrFilter =
        L"Supported "
        L"documents\0*.pdf;*.doc;*.docx;*.xls;*.xlsx;*.ppt;*.pptx;*.odt;*.ods;*.odp;*.txt;*.rtf;*.html;*.htm;*.md;*."
        L"markdown;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp\0All files\0*.*\0";
    ofn.lpstrTitle = L"Select files to create or combine into a PDF";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    const WCHAR* next = files + wcslen(files) + 1;
    if (!*next) {
        paths.Append(ToUtf8Temp(files));
        return true;
    }
    for (; *next; next += wcslen(next) + 1) paths.Append(path::JoinTemp(ToUtf8Temp(files), ToUtf8Temp(next)));
    return len(paths) > 0;
}

bool ExportPdfDialog::Create(MainWindow* w, bool fromClipboard) {
    if (create) {
        if (fromClipboard) {
            Pixmap* image = GetClipboardImageAsPixmap();
            if (!image) {
                MessageBoxWarning(w->hwndFrame, Tr("The clipboard does not contain an image."), Tr("Create PDF"));
                return false;
            }
            Str bmp = PixmapToBmpFormat(image);
            FreePixmap(image);
            clipboardPath = str::Dup(GetTempFilePathTemp(StrL("clipboard.bmp")));
            bool ok = file::WriteFile(clipboardPath, bmp);
            str::Free(bmp);
            if (!ok) return false;
            inputs.Append(clipboardPath);
        } else if (!ChooseCreateInputs(w->hwndFrame, inputs))
            return false;
    } else
        inputs.Append(w->CurrentTab()->filePath);
    if (!CreateToolDialog(w, create ? nullptr : w->CurrentTab(), create ? Tr("Create PDF") : Tr("Export a PDF")))
        return false;
    if (create) srcPath = str::Dup(inputs[0]);
    AddPathRow();
    if (create && len(inputs) > 1)
        AddRow()->AddChild(NewVirtText(
            {.s = fmt(Tr("Combine %d files in selection order into one PDF.").s, len(inputs)), .font = font}));
    AddDestRow(fmt("%s-converted.%s", path::GetPathNoExtTemp(inputs[0]), create ? StrL("pdf") : StrL("docx")),
               WStrL(L"Converted document\0*.*\0"), create ? WStrL(L"pdf") : WStrL(L"docx"));
    if (!create) {
        formats = new DropDown();
        DropDown::CreateArgs args;
        args.parent = hwnd;
        args.font = font;
        formats->Create(args);
        StrVec items;
        const char* names[] = {"Word (.docx)",
                               "PowerPoint (.pptx)",
                               "Excel (.xlsx)",
                               "HTML (.html)",
                               "Rich text (.rtf)",
                               "Markdown (.md)",
                               "Text with spacing (.txt)",
                               "JSON: text and bounding boxes (.json)",
                               "SVG pages in ZIP (.zip)"};
        for (auto name : names) items.Append(Str(name));
        formats->SetItems(items);
        CbSetCurrentSelection(formats, 0);
        formats->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
        formats->onSelectionChanged = MkMethod0<ExportPdfDialog, &ExportPdfDialog::ChangeFormat>(this);
        AddRow()->AddChild(formats, 1);
    }
    range = AddLabeledEdit(create ? Tr("Pages (each input):") : Tr("Pages:"), StrL("all"));
    AddRow()->AddChild(NewVirtText(
        {.s = Tr("Page range: all, 1-3,5. Exports use the saved file; save changes first."), .font = font}));
    AddRow()->AddChild(NewVirtText(
        {.s = create
                  ? Tr("Office needs Microsoft Office or LibreOffice. RTF imports text; HTML uses print layout.")
                  : Tr("Layout is approximate. Headings, lists and ruled tables are inferred. Scans need OCR first."),
         .font = font}));
    AddRow()->AddChild(NewVirtText(
        {.s = create ? Tr("Combining preserves visible pages, not forms, bookmarks or interactive links.")
                     : Tr("Excel keeps table cells as text. SVG ZIP preserves appearance; use To Images for PNG/JPEG."),
         .font = font}));
    status = NewVirtText({.s = Tr("Ready"), .font = font});
    AddRow()->AddChild(status, 1);
    AddButtonsRow(create ? Tr("Create PDF") : Tr("Export"));
    cancelBtn->onClick = MkMethod1<ExportPdfDialog, VirtMouseEvent*, &ExportPdfDialog::Cancel>(this);
    onTimer = MkMethod1<ExportPdfDialog, WindowBase::TimerEvent*, &ExportPdfDialog::Poll>(this);
    FinishDialog(destEdit, 650);
    return true;
}
void ShowExportPdfDialog(MainWindow* win) {
    if (!win || !CanAccessDisk() || !HasPermission(Perm::CopySelection) || !win->IsDocLoaded() ||
        !IsPdfDoc(win->CurrentTab()))
        return;
    auto* dlg = new ExportPdfDialog();
    if (!dlg->Create(win, false)) delete dlg;
}
void ShowCreatePdfFilesDialog(MainWindow* win, bool clipboard) {
    if (!win || !CanAccessDisk()) return;
    auto* dlg = new ExportPdfDialog();
    dlg->create = true;
    if (!dlg->Create(win, clipboard)) delete dlg;
}
