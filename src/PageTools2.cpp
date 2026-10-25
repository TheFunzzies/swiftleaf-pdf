/* Copyright 2026 the Swiftleaf PDF authors.
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
#include "base/Pixmap.h"
#include "base/GdiPlusUtil.h"
#include "SumatraPDF.h"
#include "SumatraConfig.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Translations.h"
#include "DarkMode.h"
#include "Material.h"
#include "Theme.h"
#include "Flags.h"
#include "Commands.h"
#include <mupdf/pdf.h>
#include <mupdf/fitz/deskew.h>
#include <mupdf/pdf/recolor.h>

#include "PageOrganize.h"
#include "PageTools2.h"

extern const pdf_write_options gPdfDefaultWriteOptions;

static int CalcDlgWidth(PlatformFont* font, Str path, int minW, int padding) {
    Size size = PlatformFontMeasureText(font, path);
    int dlgW = size.dx + (2 * padding) + DpiScale(32);
    dlgW = std::max(dlgW, minW);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    dlgW = std::min(dlgW, screenW * 80 / 100);
    return dlgW;
}

static HICON GetAppIcon() {
    return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
}

static void BrowseForDest(HWND owner, Edit* edit, WStr filter, WStr defExt) {
    WCHAR dstFileName[MAX_PATH + 1]{};
    TempWStr currentFileName = ToWStrTemp(HwndGetTextTemp(edit->hwnd));
    wstr::BufSet(WStr(dstFileName, MAX_PATH), currentFileName);

    OPENFILENAMEW ofn{};
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

static bool ResolvePageRange(Str pagesSpec, int pageCount, Vec<int>& outPages) {
    if (len(pagesSpec) == 0 || str::EqI(pagesSpec, StrL("all"))) {
        for (int i = 1; i <= pageCount; i++) {
            VecAppend(outPages, i);
        }
        return true;
    }
    Vec<PageRange> ranges;
    if (!ParsePageRanges(pagesSpec, ranges)) {
        return false;
    }
    for (auto& r : ranges) {
        int start = std::max(r.start, 1);
        int end = std::min(r.end, pageCount);
        for (int i = start; i <= end; i++) {
            if (!VecContains(outPages, i)) {
                VecAppend(outPages, i);
            }
        }
    }
    return len(outPages) > 0;
}

static pdf_obj* AddContentStream(fz_context* ctx, pdf_document* doc, Str data) {
    fz_buffer* buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)data.s, len(data));
    pdf_obj* stm = pdf_add_stream(ctx, doc, buf, nullptr, 0);
    fz_drop_buffer(ctx, buf);
    return stm;
}

//--- PageToolDialog base ---

struct PageToolDialog : WindowBase {
    Str srcPath;
    MainWindow* win = nullptr;
    WStr browseFilter;
    WStr browseDefExt;

    VBox* mainBox = nullptr;
    VirtText* pathLabel = nullptr;
    Edit* destEdit = nullptr;
    VirtButton* browseBtn = nullptr;
    VirtButton* actionBtn = nullptr;
    VirtButton* cancelBtn = nullptr;
    HBox* lastRow = nullptr;
    int rowGap = 0;
    int gap = 0;

    ~PageToolDialog() override;

    bool CreateToolDialog(MainWindow* w, WindowTab* tab, Str title);
    HBox* AddRow();
    void AddPathRow();
    void AddDestRow(Str destPath, WStr filter, WStr defExt);
    Edit* AddLabeledEdit(Str label, Str text, bool isPassword = false);
    void AddButtonsRow(Str actionText, Str hint = {});
    void FinishDialog(Edit* focusOn, int minWidth = 480);
    VirtButton* NewButton(Str text, bool isDefault);

    void OnBrowse(VirtMouseEvent* ev = nullptr);
    void OnCancel(VirtMouseEvent* ev = nullptr);
    virtual void DoIt(VirtMouseEvent* ev = nullptr) {}
};

PageToolDialog::~PageToolDialog() {
    str::FreePtr(&srcPath);
}

static void PageToolDialogOnClose(WindowBase::CloseEvent* ev) {
    auto* dlg = (PageToolDialog*)ev->e->self;
    if (dlg->win && IsWindow(dlg->win->hwndFrame)) {
        SetActiveWindow(dlg->win->hwndFrame);
    }
    dlg->ScheduleDelete();
}

void PageToolDialog::OnCancel(VirtMouseEvent*) {
    Close();
}

void PageToolDialog::OnBrowse(VirtMouseEvent*) {
    BrowseForDest(hwnd, destEdit, browseFilter, browseDefExt);
}

VirtButton* PageToolDialog::NewButton(Str text, bool isDefault) {
    return NewThemedButton(hwnd, text, font, isDefault);
}

bool PageToolDialog::CreateToolDialog(MainWindow* w, WindowTab* tab, Str title) {
    win = w;
    srcPath = str::Dup(tab->filePath);
    PlatformFont* dialogFont = GetDefaultGuiFont();
    closeOnEsc = true;
    onClose = MkFunc1Void(PageToolDialogOnClose);

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
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, (LONG_PTR)w->hwndFrame);

    rowGap = DpiScale(6);
    gap = DpiScale(8);
    mainBox = new VBox();
    mainBox->alignCross = CrossAxisAlign::Stretch;
    layout = new Padding(mainBox, DpiScaledInsets(10));
    return true;
}

HBox* PageToolDialog::AddRow() {
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

void PageToolDialog::AddPathRow() {
    pathLabel = NewVirtText({.s = srcPath, .font = font, .isRtl = IsUIRtl(), .ellipsis = true});
    mainBox->AddChild(pathLabel);
}

void PageToolDialog::AddDestRow(Str destPath, WStr filter, WStr defExt) {
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
    browseBtn->onClick = MkMethod1<PageToolDialog, VirtMouseEvent*, &PageToolDialog::OnBrowse>(this);
    row->AddChild(browseBtn);

    if (pathLabel) {
        pathLabel->padding.left = destEdit->GetLeftTextMargin();
    }
}

Edit* PageToolDialog::AddLabeledEdit(Str label, Str text, bool isPassword) {
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

void PageToolDialog::AddButtonsRow(Str actionText, Str hint) {
    HBox* row = AddRow();
    row->gap = font->averageCharWidth;
    if (hint) {
        row->AddChild(NewVirtText({.s = hint, .font = font, .isRtl = IsUIRtl()}));
    }
    row->AddChild(new Spacer(0, 0), 1);

    actionBtn = NewButton(actionText, true);
    actionBtn->onClick = MkMethod1<PageToolDialog, VirtMouseEvent*, &PageToolDialog::DoIt>(this);
    row->AddChild(actionBtn);

    cancelBtn = NewButton(Tr("Cancel"), false);
    cancelBtn->onClick = MkMethod1<PageToolDialog, VirtMouseEvent*, &PageToolDialog::OnCancel>(this);
    row->AddChild(cancelBtn);
}

void PageToolDialog::FinishDialog(Edit* focusOn, int minWidth) {
    int minClientW = DpiScale(minWidth);
    int clientW = CalcDlgWidth(font, srcPath, minClientW, DpiScale(10));
    Size size = layout->Layout(ExpandHeight(clientW));
    ResizeHwndToClientArea(hwnd, size.dx, size.dy, false);
    DoLayout(size);

    HwndCenterDialog(hwnd, win->hwndFrame);
    UpdateTheme();
    SetIsVisible(true);
    EditSetFocus(focusOn);
}

//--- (1) Crop & Page Boxes ---

bool PdfApplyPageBoxes(Str srcPath, Str destPath, const PdfPageBoxesParams& params) {
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

        pdf_obj* boxKey = PDF_NAME(CropBox);
        switch (params.boxType) {
            case 0:
                boxKey = PDF_NAME(MediaBox);
                break;
            case 1:
                boxKey = PDF_NAME(CropBox);
                break;
            case 2:
                boxKey = PDF_NAME(TrimBox);
                break;
            case 3:
                boxKey = PDF_NAME(BleedBox);
                break;
            case 4:
                boxKey = PDF_NAME(ArtBox);
                break;
        }

        for (int pNum : pages) {
            int pageIndex = pNum - 1;
            if (pageIndex < 0 || pageIndex >= pageCount) {
                continue;
            }
            pdf_page* page = pdf_load_page(ctx, doc, pageIndex);
            if (!page) {
                continue;
            }

            fz_rect media = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, page->obj, PDF_NAME(MediaBox)));
            if (fz_is_empty_rect(media)) {
                media = fz_make_rect(0, 0, 612, 792);
            }

            fz_rect targetRect = media;

            if (params.removeWhiteMargins) {
                fz_display_list* list = fz_new_display_list_from_page(ctx, (fz_page*)page);
                fz_rect bounds = fz_bound_display_list(ctx, list);
                fz_drop_display_list(ctx, list);
                if (!fz_is_empty_rect(bounds)) {
                    targetRect = fz_intersect_rect(bounds, media);
                }
            } else {
                fz_rect oldBox = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, page->obj, boxKey));
                if (fz_is_empty_rect(oldBox)) {
                    oldBox = media;
                }
                oldBox.x0 += params.marginLeft;
                oldBox.y0 += params.marginBottom;
                oldBox.x1 -= params.marginRight;
                oldBox.y1 -= params.marginTop;
                if (oldBox.x1 > oldBox.x0 && oldBox.y1 > oldBox.y0) {
                    targetRect = oldBox;
                }
            }

            pdf_obj* newBox = pdf_new_array(ctx, doc, 4);
            pdf_array_push_real(ctx, newBox, targetRect.x0);
            pdf_array_push_real(ctx, newBox, targetRect.y0);
            pdf_array_push_real(ctx, newBox, targetRect.x1);
            pdf_array_push_real(ctx, newBox, targetRect.y1);
            pdf_dict_put_drop(ctx, page->obj, boxKey, newBox);

            fz_drop_page(ctx, (fz_page*)page);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
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
    }
    fz_drop_context(ctx);
    return ok;
}

struct CropPagesDialog : PageToolDialog {
    Edit* pagesEdit = nullptr;
    Edit* leftEdit = nullptr;
    Edit* bottomEdit = nullptr;
    Edit* rightEdit = nullptr;
    Edit* topEdit = nullptr;
    DropDown* boxCombo = nullptr;
    Checkbox* removeMarginsCheck = nullptr;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

void CropPagesDialog::DoIt(VirtMouseEvent*) {
    PdfToolEdit edit(win, srcPath);
    if (!edit.IsReady()) return;
    Str destPath = edit.outputPath;
    if (len(destPath) == 0) return;

    PdfPageBoxesParams params;
    params.pagesSpec = pagesEdit->GetTextTemp();
    params.boxType = boxCombo ? CbGetCurrentSelection(boxCombo) : 1;
    params.removeWhiteMargins = removeMarginsCheck && removeMarginsCheck->IsChecked();

    float l = 0, b = 0, r = 0, t = 0;
    str::Parse(leftEdit->GetTextTemp(), "%f%$", &l);
    str::Parse(bottomEdit->GetTextTemp(), "%f%$", &b);
    str::Parse(rightEdit->GetTextTemp(), "%f%$", &r);
    str::Parse(topEdit->GetTextTemp(), "%f%$", &t);
    params.marginLeft = l;
    params.marginBottom = b;
    params.marginRight = r;
    params.marginTop = t;

    bool ok = PdfApplyPageBoxes(edit.inputPath, destPath, params);
    if (ok) {
        if (edit.Apply("Crop pages")) Close();
    } else {
        MessageBoxWarning(hwnd, StrL("Failed to crop pages."), Tr("Crop Pages"));
    }
}

bool CropPagesDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Crop Pages & Edit Page Boxes"))) {
        return false;
    }
    AddPathRow();

    pagesEdit = AddLabeledEdit(Tr("Pages:"), StrL("all"));

    HBox* boxRow = AddRow();
    boxRow->AddChild(NewVirtText({.s = Tr("Target Box:"), .font = font, .isRtl = IsUIRtl()}));
    boxRow->AddChild(new Spacer(gap, 0));
    DropDown::CreateArgs dargs;
    dargs.parent = hwnd;
    dargs.font = font;
    dargs.isRtl = IsUIRtl();
    boxCombo = new DropDown();
    boxCombo->Create(dargs);
    StrVec boxChoices;
    boxChoices.Append(StrL("MediaBox"));
    boxChoices.Append(StrL("CropBox"));
    boxChoices.Append(StrL("TrimBox"));
    boxChoices.Append(StrL("BleedBox"));
    boxChoices.Append(StrL("ArtBox"));
    boxCombo->SetItems(boxChoices);
    CbSetCurrentSelection(boxCombo, 1);
    boxRow->AddChild(boxCombo, 1);

    HBox* marginsRow = AddRow();
    marginsRow->AddChild(NewVirtText({.s = Tr("Margins (pt) L / B / R / T:"), .font = font, .isRtl = IsUIRtl()}));
    marginsRow->AddChild(new Spacer(gap, 0));

    Edit::CreateArgs mArgs;
    mArgs.parent = hwnd;
    mArgs.withBorder = true;
    mArgs.font = font;
    mArgs.isRtl = IsUIRtl();

    mArgs.text = StrL("0");
    leftEdit = new Edit();
    leftEdit->Create(mArgs);
    marginsRow->AddChild(leftEdit, 1);

    bottomEdit = new Edit();
    bottomEdit->Create(mArgs);
    marginsRow->AddChild(bottomEdit, 1);

    rightEdit = new Edit();
    rightEdit->Create(mArgs);
    marginsRow->AddChild(rightEdit, 1);

    topEdit = new Edit();
    topEdit->Create(mArgs);
    marginsRow->AddChild(topEdit, 1);

    Checkbox::CreateArgs cbArgs;
    cbArgs.parent = hwnd;
    cbArgs.text = Tr("Automatically remove white margins");
    cbArgs.font = font;
    cbArgs.isRtl = IsUIRtl();
    removeMarginsCheck = new Checkbox();
    removeMarginsCheck->Create(cbArgs);
    removeMarginsCheck->SetColors(ThemeWindowTextColor(), DarkModeDialogBgColor());
    AddRow()->AddChild(removeMarginsCheck);

    AddButtonsRow(Tr("Apply Crop"));
    FinishDialog(pagesEdit);
    return true;
}

void ShowCropPagesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || len(tab->filePath) == 0 || !IsPdfDoc(tab)) return;
    auto* dlg = new CropPagesDialog();
    if (!dlg->Create(win, tab)) delete dlg;
}

//--- (2) Posterize & Divide Pages ---

static void IntersectBox(fz_context* ctx, pdf_document* doc, pdf_obj* page, pdf_obj* boxName, fz_rect mb) {
    pdf_obj* box = pdf_dict_get(ctx, page, boxName);
    if (!box) return;
    fz_rect old = pdf_to_rect(ctx, box);
    old.x0 = std::max(old.x0, mb.x0);
    old.y0 = std::max(old.y0, mb.y0);
    old.x1 = std::min(old.x1, mb.x1);
    old.y1 = std::min(old.y1, mb.y1);
    pdf_obj* nb = pdf_new_array(ctx, doc, 4);
    pdf_array_push_real(ctx, nb, old.x0);
    pdf_array_push_real(ctx, nb, old.y0);
    pdf_array_push_real(ctx, nb, old.x1);
    pdf_array_push_real(ctx, nb, old.y1);
    pdf_dict_put_drop(ctx, page, boxName, nb);
}

bool PdfApplyPosterize(Str srcPath, Str destPath, const PdfPosterizeParams& params) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        }
        int numPages = pdf_count_pages(ctx, doc);
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* pages = pdf_dict_get(ctx, root, PDF_NAME(Pages));
        pdf_obj* kids = pdf_new_array(ctx, doc, 1);

        int kidCount = 0;
        int xf = std::max(1, params.xTiles);
        int yf = std::max(1, params.yTiles);
        float margin = params.overlapPoints;

        for (int p = 0; p < numPages; p++) {
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, p);
            fz_rect media = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(MediaBox)));
            fz_rect crop = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(CropBox)));
            if (fz_is_empty_rect(media)) media = fz_make_rect(0, 0, 612, 792);
            if (!fz_is_empty_rect(crop)) media = fz_intersect_rect(media, crop);

            float w = media.x1 - media.x0;
            float h = media.y1 - media.y0;

            for (int y = yf - 1; y >= 0; y--) {
                for (int x = 0; x < xf; x++) {
                    pdf_obj* newPage = pdf_copy_dict(ctx, pageObj);
                    pdf_flatten_inheritable_page_items(ctx, newPage);
                    pdf_obj* pageRef = pdf_add_object(ctx, doc, newPage);

                    fz_rect mb;
                    mb.x0 = media.x0 + (w / xf) * x;
                    mb.x1 = (x == xf - 1) ? media.x1 : mb.x0 + (w / xf) + margin;
                    mb.y0 = media.y0 + (h / yf) * y;
                    mb.y1 = (y == yf - 1) ? media.y1 : mb.y0 + (h / yf) + margin;

                    pdf_obj* newMedia = pdf_new_array(ctx, doc, 4);
                    pdf_array_push_real(ctx, newMedia, mb.x0);
                    pdf_array_push_real(ctx, newMedia, mb.y0);
                    pdf_array_push_real(ctx, newMedia, mb.x1);
                    pdf_array_push_real(ctx, newMedia, mb.y1);

                    pdf_dict_put(ctx, newPage, PDF_NAME(Parent), pages);
                    pdf_dict_put_drop(ctx, newPage, PDF_NAME(MediaBox), newMedia);

                    IntersectBox(ctx, doc, newPage, PDF_NAME(CropBox), mb);
                    IntersectBox(ctx, doc, newPage, PDF_NAME(BleedBox), mb);
                    IntersectBox(ctx, doc, newPage, PDF_NAME(TrimBox), mb);
                    IntersectBox(ctx, doc, newPage, PDF_NAME(ArtBox), mb);

                    pdf_drop_obj(ctx, newPage);
                    pdf_array_push_drop(ctx, kids, pageRef);
                    kidCount++;
                }
            }
        }

        pdf_dict_put_int(ctx, pages, PDF_NAME(Count), kidCount);
        pdf_dict_put_drop(ctx, pages, PDF_NAME(Kids), kids);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfApplyDividePages(Str srcPath, Str destPath, const PdfDividePagesParams& params) {
    PdfPosterizeParams pp;
    pp.xTiles = std::max(1, params.horizontalDivisions);
    pp.yTiles = std::max(1, params.verticalDivisions);
    pp.overlapPoints = 0.f;
    return PdfApplyPosterize(srcPath, destPath, pp);
}

//--- N-up & Booklet & Combine Long Page ---

bool PdfApplyNUp(Str srcPath, Str destPath, const PdfNUpParams& params) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        }
        int numPages = pdf_count_pages(ctx, doc);
        if (numPages == 0) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "empty");
        }

        int cols = 1, rows = 1;
        int nup = params.pagesPerSheet;
        if (nup == 2) {
            cols = 2;
            rows = 1;
        } else if (nup == 4) {
            cols = 2;
            rows = 2;
        } else if (nup == 6) {
            cols = 3;
            rows = 2;
        } else if (nup == 8) {
            cols = 4;
            rows = 2;
        } else if (nup == 9) {
            cols = 3;
            rows = 3;
        } else if (nup == 16) {
            cols = 4;
            rows = 4;
        } else {
            cols = 2;
            rows = 1;
            nup = 2;
        }

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* pages = pdf_dict_get(ctx, root, PDF_NAME(Pages));
        pdf_obj* kids = pdf_new_array(ctx, doc, 1);

        float sheetW = 612.f;
        float sheetH = 792.f;
        if (cols > rows) {
            sheetW = 792.f;
            sheetH = 612.f;
        }

        float cellW = sheetW / cols;
        float cellH = sheetH / rows;

        int newSheetCount = 0;
        for (int p = 0; p < numPages; p += nup) {
            pdf_obj* sheetObj = pdf_new_dict(ctx, doc, 6);
            pdf_dict_put(ctx, sheetObj, PDF_NAME(Type), PDF_NAME(Page));
            pdf_dict_put(ctx, sheetObj, PDF_NAME(Parent), pages);

            pdf_obj* sheetMedia = pdf_new_array(ctx, doc, 4);
            pdf_array_push_real(ctx, sheetMedia, 0);
            pdf_array_push_real(ctx, sheetMedia, 0);
            pdf_array_push_real(ctx, sheetMedia, sheetW);
            pdf_array_push_real(ctx, sheetMedia, sheetH);
            pdf_dict_put_drop(ctx, sheetObj, PDF_NAME(MediaBox), sheetMedia);

            pdf_obj* sheetRes = pdf_new_dict(ctx, doc, 2);
            pdf_obj* sheetXObjs = pdf_new_dict(ctx, doc, nup);
            pdf_dict_put(ctx, sheetRes, PDF_NAME(XObject), sheetXObjs);
            pdf_dict_put_drop(ctx, sheetObj, PDF_NAME(Resources), sheetRes);

            str::Builder sheetStm;

            for (int sub = 0; sub < nup && (p + sub) < numPages; sub++) {
                int inPageIdx = p + sub;
                pdf_page* inPage = pdf_load_page(ctx, doc, inPageIdx);
                fz_rect inMedia = pdf_bound_page(ctx, inPage, FZ_CROP_BOX);
                float inW = inMedia.x1 - inMedia.x0;
                float inH = inMedia.y1 - inMedia.y0;
                if (inW <= 0) inW = 612.f;
                if (inH <= 0) inH = 792.f;

                pdf_obj* inContents = pdf_dict_get(ctx, inPage->obj, PDF_NAME(Contents));
                fz_buffer* cbuf = fz_new_buffer(ctx, 1024);
                if (pdf_is_array(ctx, inContents)) {
                    int cLen = pdf_array_len(ctx, inContents);
                    for (int c = 0; c < cLen; c++) {
                        fz_buffer* part = pdf_load_stream(ctx, pdf_array_get(ctx, inContents, c));
                        fz_append_buffer(ctx, cbuf, part);
                        fz_drop_buffer(ctx, part);
                    }
                } else if (inContents) {
                    fz_buffer* part = pdf_load_stream(ctx, inContents);
                    fz_append_buffer(ctx, cbuf, part);
                    fz_drop_buffer(ctx, part);
                }

                pdf_obj* inRes = pdf_dict_get_inheritable(ctx, inPage->obj, PDF_NAME(Resources));

                pdf_obj* formXObj = pdf_new_xobject(ctx, doc, inMedia, fz_identity, inRes, cbuf);
                fz_drop_buffer(ctx, cbuf);

                TempStr xname = fmt("Xp%d", sub + 1);
                pdf_dict_puts_drop(ctx, sheetXObjs, CStrTemp(xname), formXObj);

                int col = sub % cols;
                int row = sub / cols;
                float cellX = col * cellW;
                float cellY = sheetH - (row + 1) * cellH;

                float scaleX = cellW / inW;
                float scaleY = cellH / inH;
                float scale = std::min(scaleX, scaleY) * 0.95f;

                float drawW = inW * scale;
                float drawH = inH * scale;
                float offX = cellX + (cellW - drawW) / 2.f;
                float offY = cellY + (cellH - drawH) / 2.f;

                sheetStm.Append(fmt("q %.4f 0 0 %.4f %.2f %.2f cm /%s Do Q\n", scale, scale, offX, offY, xname));

                if (params.drawBorders) {
                    sheetStm.Append(fmt("q 0.5 w 0 0 0 RG %.2f %.2f %.2f %.2f re s Q\n", cellX, cellY, cellW, cellH));
                }

                fz_drop_page(ctx, (fz_page*)inPage);
            }

            pdf_obj* sheetContents = AddContentStream(ctx, doc, ToStrTemp(sheetStm));
            pdf_dict_put_drop(ctx, sheetObj, PDF_NAME(Contents), sheetContents);

            pdf_obj* sheetRef = pdf_add_object(ctx, doc, sheetObj);
            pdf_drop_obj(ctx, sheetObj);
            pdf_array_push_drop(ctx, kids, sheetRef);
            newSheetCount++;
        }

        pdf_dict_put_int(ctx, pages, PDF_NAME(Count), newSheetCount);
        pdf_dict_put_drop(ctx, pages, PDF_NAME(Kids), kids);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfApplyBooklet(Str srcPath, Str destPath, const PdfBookletParams&) {
    PdfNUpParams nup;
    nup.pagesPerSheet = 2;
    nup.drawBorders = false;
    return PdfApplyNUp(srcPath, destPath, nup);
}

bool PdfApplyCombineLongPage(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);
        if (numPages == 0) fz_throw(ctx, FZ_ERROR_GENERIC, "empty");

        float totalH = 0;
        float maxW = 0;
        Vec<float> pageW;
        Vec<float> pageH;

        for (int p = 0; p < numPages; p++) {
            pdf_page* page = pdf_load_page(ctx, doc, p);
            fz_rect b = pdf_bound_page(ctx, page, FZ_CROP_BOX);
            float w = b.x1 - b.x0;
            float h = b.y1 - b.y0;
            if (w <= 0) w = 612.f;
            if (h <= 0) h = 792.f;
            VecAppend(pageW, w);
            VecAppend(pageH, h);
            totalH += h;
            if (w > maxW) maxW = w;
            fz_drop_page(ctx, (fz_page*)page);
        }

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* pages = pdf_dict_get(ctx, root, PDF_NAME(Pages));
        pdf_obj* kids = pdf_new_array(ctx, doc, 1);

        pdf_obj* sheetObj = pdf_new_dict(ctx, doc, 6);
        pdf_dict_put(ctx, sheetObj, PDF_NAME(Type), PDF_NAME(Page));
        pdf_dict_put(ctx, sheetObj, PDF_NAME(Parent), pages);

        pdf_obj* sheetMedia = pdf_new_array(ctx, doc, 4);
        pdf_array_push_real(ctx, sheetMedia, 0);
        pdf_array_push_real(ctx, sheetMedia, 0);
        pdf_array_push_real(ctx, sheetMedia, maxW);
        pdf_array_push_real(ctx, sheetMedia, totalH);
        pdf_dict_put_drop(ctx, sheetObj, PDF_NAME(MediaBox), sheetMedia);

        pdf_obj* sheetRes = pdf_new_dict(ctx, doc, 2);
        pdf_obj* sheetXObjs = pdf_new_dict(ctx, doc, numPages);
        pdf_dict_put(ctx, sheetRes, PDF_NAME(XObject), sheetXObjs);
        pdf_dict_put_drop(ctx, sheetObj, PDF_NAME(Resources), sheetRes);

        str::Builder sheetStm;
        float curY = totalH;

        for (int p = 0; p < numPages; p++) {
            float w = pageW[p];
            float h = pageH[p];
            curY -= h;

            pdf_page* inPage = pdf_load_page(ctx, doc, p);
            fz_rect inMedia = pdf_bound_page(ctx, inPage, FZ_CROP_BOX);

            pdf_obj* inContents = pdf_dict_get(ctx, inPage->obj, PDF_NAME(Contents));
            fz_buffer* cbuf = fz_new_buffer(ctx, 1024);
            if (pdf_is_array(ctx, inContents)) {
                int cLen = pdf_array_len(ctx, inContents);
                for (int c = 0; c < cLen; c++) {
                    fz_buffer* part = pdf_load_stream(ctx, pdf_array_get(ctx, inContents, c));
                    fz_append_buffer(ctx, cbuf, part);
                    fz_drop_buffer(ctx, part);
                }
            } else if (inContents) {
                fz_buffer* part = pdf_load_stream(ctx, inContents);
                fz_append_buffer(ctx, cbuf, part);
                fz_drop_buffer(ctx, part);
            }

            pdf_obj* inRes = pdf_dict_get_inheritable(ctx, inPage->obj, PDF_NAME(Resources));
            pdf_obj* formXObj = pdf_new_xobject(ctx, doc, inMedia, fz_identity, inRes, cbuf);
            fz_drop_buffer(ctx, cbuf);

            TempStr xname = fmt("Lp%d", p + 1);
            pdf_dict_puts_drop(ctx, sheetXObjs, CStrTemp(xname), formXObj);

            float offX = (maxW - w) / 2.f;
            sheetStm.Append(fmt("q 1 0 0 1 %.2f %.2f cm /%s Do Q\n", offX, curY, xname));
            fz_drop_page(ctx, (fz_page*)inPage);
        }

        pdf_obj* sheetContents = AddContentStream(ctx, doc, ToStrTemp(sheetStm));
        pdf_dict_put_drop(ctx, sheetObj, PDF_NAME(Contents), sheetContents);

        pdf_obj* sheetRef = pdf_add_object(ctx, doc, sheetObj);
        pdf_drop_obj(ctx, sheetObj);
        pdf_array_push_drop(ctx, kids, sheetRef);

        pdf_dict_put_int(ctx, pages, PDF_NAME(Count), 1);
        pdf_dict_put_drop(ctx, pages, PDF_NAME(Kids), kids);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

//--- (3) Page Background Color / Image ---

bool PdfApplyPageBackground(Str srcPath, Str destPath, const PdfBackgroundParams& params) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);
        Vec<int> pages;
        if (!ResolvePageRange(params.pagesSpec, numPages, pages)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "invalid pages");
        }

        for (int pNum : pages) {
            int pageIndex = pNum - 1;
            if (pageIndex < 0 || pageIndex >= numPages) continue;
            pdf_page* page = pdf_load_page(ctx, doc, pageIndex);
            if (!page) continue;

            fz_rect media = pdf_bound_page(ctx, page, FZ_MEDIA_BOX);
            float w = media.x1 - media.x0;
            float h = media.y1 - media.y0;

            str::Builder bgContent;
            if (params.useColor) {
                u8 r = GetRValue(params.color);
                u8 g = GetGValue(params.color);
                u8 b = GetBValue(params.color);
                bgContent.Append(fmt("q %.3f %.3f %.3f rg %.2f %.2f %.2f %.2f re f Q\n", r / 255.f, g / 255.f,
                                     b / 255.f, media.x0, media.y0, w, h));
            } else if (len(params.imagePath) > 0) {
                Str imgData = file::ReadFile(params.imagePath);
                if (len(imgData) > 0) {
                    fz_buffer* imgBuf =
                        fz_new_buffer_from_copied_data(ctx, (const unsigned char*)imgData.s, (size_t)len(imgData));
                    str::Free(imgData);
                    fz_image* img = fz_new_image_from_buffer(ctx, imgBuf);
                    fz_drop_buffer(ctx, imgBuf);
                    if (img) {
                        pdf_obj* imgObj = pdf_add_image(ctx, doc, img);
                        fz_drop_image(ctx, img);

                        pdf_obj* res = pdf_dict_get(ctx, page->obj, PDF_NAME(Resources));
                        if (!res) {
                            res = pdf_new_dict(ctx, doc, 2);
                            pdf_dict_put_drop(ctx, page->obj, PDF_NAME(Resources), res);
                        }
                        pdf_obj* xobjs = pdf_dict_get(ctx, res, PDF_NAME(XObject));
                        if (!xobjs) {
                            xobjs = pdf_new_dict(ctx, doc, 1);
                            pdf_dict_put_drop(ctx, res, PDF_NAME(XObject), xobjs);
                        }
                        pdf_dict_puts_drop(ctx, xobjs, "SwiftleafBg", imgObj);
                        bgContent.Append(
                            fmt("q %.2f 0 0 %.2f %.2f %.2f cm /SwiftleafBg Do Q\n", w, h, media.x0, media.y0));
                    }
                }
            }

            if (len(bgContent) > 0) {
                pdf_obj* stmBg = AddContentStream(ctx, doc, ToStrTemp(bgContent));
                pdf_obj* stmPush = AddContentStream(ctx, doc, StrL("q\n"));
                pdf_obj* stmPop = AddContentStream(ctx, doc, StrL("Q\n"));

                pdf_obj* origContents = pdf_dict_get(ctx, page->obj, PDF_NAME(Contents));
                pdf_obj* arr = pdf_new_array(ctx, doc, 4);
                pdf_array_push(ctx, arr, stmBg);
                pdf_array_push(ctx, arr, stmPush);

                if (pdf_is_array(ctx, origContents)) {
                    int cLen = pdf_array_len(ctx, origContents);
                    for (int k = 0; k < cLen; k++) {
                        pdf_array_push(ctx, arr, pdf_array_get(ctx, origContents, k));
                    }
                } else if (origContents) {
                    pdf_array_push(ctx, arr, origContents);
                }

                pdf_array_push(ctx, arr, stmPop);
                pdf_dict_put(ctx, page->obj, PDF_NAME(Contents), arr);

                pdf_drop_obj(ctx, arr);
                pdf_drop_obj(ctx, stmBg);
                pdf_drop_obj(ctx, stmPush);
                pdf_drop_obj(ctx, stmPop);
            }

            fz_drop_page(ctx, (fz_page*)page);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

//--- (4) Duplicate, Reverse, Insert Blank, Remove Blank, Deskew ---

bool PdfDuplicatePages(Str srcPath, Str destPath, Str pagesSpec) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);
        Vec<int> toDup;
        if (!ResolvePageRange(pagesSpec, numPages, toDup)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "invalid pages");
        }

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* pages = pdf_dict_get(ctx, root, PDF_NAME(Pages));
        pdf_obj* kids = pdf_new_array(ctx, doc, numPages + len(toDup));

        int newCount = 0;
        for (int i = 0; i < numPages; i++) {
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, i);
            pdf_array_push(ctx, kids, pageObj);
            newCount++;

            if (VecContains(toDup, i + 1)) {
                pdf_obj* dupPage = pdf_copy_dict(ctx, pageObj);
                pdf_flatten_inheritable_page_items(ctx, dupPage);
                pdf_obj* dupRef = pdf_add_object(ctx, doc, dupPage);
                pdf_dict_put(ctx, dupPage, PDF_NAME(Parent), pages);
                pdf_drop_obj(ctx, dupPage);
                pdf_array_push_drop(ctx, kids, dupRef);
                newCount++;
            }
        }

        pdf_dict_put_int(ctx, pages, PDF_NAME(Count), newCount);
        pdf_dict_put_drop(ctx, pages, PDF_NAME(Kids), kids);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfReversePageOrder(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* pages = pdf_dict_get(ctx, root, PDF_NAME(Pages));
        pdf_obj* kids = pdf_new_array(ctx, doc, numPages);

        for (int i = numPages - 1; i >= 0; i--) {
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, i);
            pdf_array_push(ctx, kids, pageObj);
        }

        pdf_dict_put_int(ctx, pages, PDF_NAME(Count), numPages);
        pdf_dict_put_drop(ctx, pages, PDF_NAME(Kids), kids);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfInsertBlankPages(Str srcPath, Str destPath, int afterPage, int count) {
    if (len(srcPath) == 0 || len(destPath) == 0 || count <= 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* pages = pdf_dict_get(ctx, root, PDF_NAME(Pages));
        pdf_obj* kids = pdf_new_array(ctx, doc, numPages + count);

        int insertIdx = std::max(0, std::min(numPages, afterPage));

        for (int i = 0; i < numPages; i++) {
            if (i == insertIdx) {
                for (int c = 0; c < count; c++) {
                    pdf_obj* blank = pdf_new_dict(ctx, doc, 4);
                    pdf_dict_put(ctx, blank, PDF_NAME(Type), PDF_NAME(Page));
                    pdf_dict_put(ctx, blank, PDF_NAME(Parent), pages);
                    pdf_obj* mb = pdf_new_array(ctx, doc, 4);
                    pdf_array_push_real(ctx, mb, 0);
                    pdf_array_push_real(ctx, mb, 0);
                    pdf_array_push_real(ctx, mb, 612);
                    pdf_array_push_real(ctx, mb, 792);
                    pdf_dict_put_drop(ctx, blank, PDF_NAME(MediaBox), mb);
                    pdf_obj* ref = pdf_add_object(ctx, doc, blank);
                    pdf_drop_obj(ctx, blank);
                    pdf_array_push_drop(ctx, kids, ref);
                }
            }
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, i);
            pdf_array_push(ctx, kids, pageObj);
        }
        if (insertIdx == numPages) {
            for (int c = 0; c < count; c++) {
                pdf_obj* blank = pdf_new_dict(ctx, doc, 4);
                pdf_dict_put(ctx, blank, PDF_NAME(Type), PDF_NAME(Page));
                pdf_dict_put(ctx, blank, PDF_NAME(Parent), pages);
                pdf_obj* mb = pdf_new_array(ctx, doc, 4);
                pdf_array_push_real(ctx, mb, 0);
                pdf_array_push_real(ctx, mb, 0);
                pdf_array_push_real(ctx, mb, 612);
                pdf_array_push_real(ctx, mb, 792);
                pdf_dict_put_drop(ctx, blank, PDF_NAME(MediaBox), mb);
                pdf_obj* ref = pdf_add_object(ctx, doc, blank);
                pdf_drop_obj(ctx, blank);
                pdf_array_push_drop(ctx, kids, ref);
            }
        }

        pdf_dict_put_int(ctx, pages, PDF_NAME(Count), numPages + count);
        pdf_dict_put_drop(ctx, pages, PDF_NAME(Kids), kids);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

static bool IsPageBlank(fz_context* ctx, pdf_page* page) {
    fz_stext_page* stext = fz_new_stext_page_from_page(ctx, (fz_page*)page, nullptr);
    if (stext) {
        for (fz_stext_block* b = stext->first_block; b; b = b->next) {
            if (b->type == FZ_STEXT_BLOCK_TEXT) {
                for (fz_stext_line* l = b->u.t.first_line; l; l = l->next) {
                    for (fz_stext_char* ch = l->first_char; ch; ch = ch->next) {
                        if (ch->c > 32) {
                            fz_drop_stext_page(ctx, stext);
                            return false;
                        }
                    }
                }
            }
        }
        fz_drop_stext_page(ctx, stext);
    }

    fz_display_list* list = fz_new_display_list_from_page(ctx, (fz_page*)page);
    fz_rect bounds = fz_bound_display_list(ctx, list);
    fz_drop_display_list(ctx, list);
    if (fz_is_empty_rect(bounds)) {
        return true;
    }

    fz_pixmap* pm = fz_new_pixmap_from_page(ctx, (fz_page*)page, fz_scale(0.5f, 0.5f), fz_device_rgb(ctx), 0);
    if (!pm) return true;
    int n = pm->w * pm->h * pm->n;
    unsigned char* s = pm->samples;
    bool allWhite = true;
    for (int i = 0; i < n; i++) {
        if (s[i] < 250) {
            allWhite = false;
            break;
        }
    }
    fz_drop_pixmap(ctx, pm);
    return allWhite;
}

bool PdfRemoveBlankPages(Str srcPath, Str destPath, int* removedCountOut) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    int removed = 0;
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* pages = pdf_dict_get(ctx, root, PDF_NAME(Pages));
        pdf_obj* kids = pdf_new_array(ctx, doc, numPages);

        int kept = 0;
        for (int i = 0; i < numPages; i++) {
            pdf_page* page = pdf_load_page(ctx, doc, i);
            bool blank = IsPageBlank(ctx, page);
            fz_drop_page(ctx, (fz_page*)page);

            if (blank && (kept + (numPages - i - 1)) > 0) {
                removed++;
                continue;
            }
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, i);
            pdf_array_push(ctx, kids, pageObj);
            kept++;
        }

        if (removedCountOut) *removedCountOut = removed;

        pdf_dict_put_int(ctx, pages, PDF_NAME(Count), kept);
        pdf_dict_put_drop(ctx, pages, PDF_NAME(Kids), kids);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfDeskewPages(Str srcPath, Str destPath, double minAngleThreshold) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        for (int i = 0; i < numPages; i++) {
            pdf_page* page = pdf_load_page(ctx, doc, i);
            if (!page) continue;
            fz_pixmap* pm = fz_new_pixmap_from_page(ctx, (fz_page*)page, fz_scale(1.0f, 1.0f), fz_device_gray(ctx), 0);
            if (pm) {
                double skew = fz_detect_skew(ctx, pm);
                fz_drop_pixmap(ctx, pm);
                if (fabs(skew) >= minAngleThreshold) {
                    fz_rect media = pdf_bound_page(ctx, page, FZ_MEDIA_BOX);
                    float cx = (media.x0 + media.x1) / 2.f;
                    float cy = (media.y0 + media.y1) / 2.f;
                    double rad = -skew * 3.14159265358979323846 / 180.0;
                    float c = (float)cos(rad);
                    float s = (float)sin(rad);

                    str::Builder stm;
                    stm.Append(fmt("q %.5f %.5f %.5f %.5f %.2f %.2f cm 1 0 0 1 %.2f %.2f cm\n", c, s, -s, c, cx, cy,
                                   -cx, -cy));
                    pdf_obj* stmHead = AddContentStream(ctx, doc, ToStrTemp(stm));
                    pdf_obj* stmTail = AddContentStream(ctx, doc, StrL("Q\n"));

                    pdf_obj* origContents = pdf_dict_get(ctx, page->obj, PDF_NAME(Contents));
                    pdf_obj* arr = pdf_new_array(ctx, doc, 4);
                    pdf_array_push(ctx, arr, stmHead);

                    if (pdf_is_array(ctx, origContents)) {
                        int cLen = pdf_array_len(ctx, origContents);
                        for (int k = 0; k < cLen; k++) {
                            pdf_array_push(ctx, arr, pdf_array_get(ctx, origContents, k));
                        }
                    } else if (origContents) {
                        pdf_array_push(ctx, arr, origContents);
                    }
                    pdf_array_push(ctx, arr, stmTail);
                    pdf_dict_put(ctx, page->obj, PDF_NAME(Contents), arr);

                    pdf_drop_obj(ctx, arr);
                    pdf_drop_obj(ctx, stmHead);
                    pdf_drop_obj(ctx, stmTail);
                }
            }
            fz_drop_page(ctx, (fz_page*)page);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

//--- (5) Document Properties Editor ---

bool PdfGetDocProps(Str srcPath, PdfDocPropsParams& outProps) {
    if (len(srcPath) == 0) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (doc) {
            pdf_obj* info = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info));
            if (info) {
                const char* t = pdf_dict_get_text_string(ctx, info, PDF_NAME(Title));
                const char* a = pdf_dict_get_text_string(ctx, info, PDF_NAME(Author));
                const char* s = pdf_dict_get_text_string(ctx, info, PDF_NAME(Subject));
                const char* k = pdf_dict_get_text_string(ctx, info, PDF_NAME(Keywords));
                if (t) outProps.title = str::Dup(Str(t));
                if (a) outProps.author = str::Dup(Str(a));
                if (s) outProps.subject = str::Dup(Str(s));
                if (k) outProps.keywords = str::Dup(Str(k));
            }
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
            pdf_obj* meta = pdf_dict_get(ctx, root, PDF_NAME(Metadata));
            if (meta) {
                fz_buffer* b = pdf_load_stream(ctx, meta);
                if (b) {
                    unsigned char* data = nullptr;
                    size_t dlen = fz_buffer_storage(ctx, b, &data);
                    outProps.xmp = str::Dup(Str{(const char*)data, (int)dlen});
                    fz_drop_buffer(ctx, b);
                }
            }
            ok = true;
        }
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfSetDocProps(Str srcPath, Str destPath, const PdfDocPropsParams& params) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        pdf_obj* trailer = pdf_trailer(ctx, doc);
        pdf_obj* info = pdf_dict_get(ctx, trailer, PDF_NAME(Info));
        if (!info) {
            info = pdf_new_dict(ctx, doc, 6);
            pdf_dict_put_drop(ctx, trailer, PDF_NAME(Info), info);
        }

        if (len(params.title) > 0) pdf_dict_put_text_string(ctx, info, PDF_NAME(Title), CStrTemp(params.title));
        if (len(params.author) > 0) pdf_dict_put_text_string(ctx, info, PDF_NAME(Author), CStrTemp(params.author));
        if (len(params.subject) > 0) pdf_dict_put_text_string(ctx, info, PDF_NAME(Subject), CStrTemp(params.subject));
        if (len(params.keywords) > 0)
            pdf_dict_put_text_string(ctx, info, PDF_NAME(Keywords), CStrTemp(params.keywords));

        pdf_obj* root = pdf_dict_get(ctx, trailer, PDF_NAME(Root));
        if (len(params.xmp) > 0) {
            fz_buffer* xmpBuf =
                fz_new_buffer_from_copied_data(ctx, (const unsigned char*)params.xmp.s, len(params.xmp));
            pdf_obj* xmpStream = pdf_add_stream(ctx, doc, xmpBuf, nullptr, 0);
            pdf_dict_put(ctx, xmpStream, PDF_NAME(Type), PDF_NAME(Metadata));
            pdf_dict_put(ctx, xmpStream, PDF_NAME(Subtype), PDF_NAME(XML));
            pdf_dict_put_drop(ctx, root, PDF_NAME(Metadata), xmpStream);
            fz_drop_buffer(ctx, xmpBuf);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

//--- (6) Attachments Manager ---

bool PdfListAttachments(Str srcPath, Vec<PdfAttachmentInfo>& outList) {
    if (len(srcPath) == 0) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (doc) {
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
            pdf_obj* names = pdf_dict_get(ctx, root, PDF_NAME(Names));
            pdf_obj* ef = pdf_dict_get(ctx, names, PDF_NAME(EmbeddedFiles));
            pdf_obj* efNames = pdf_dict_get(ctx, ef, PDF_NAME(Names));
            if (pdf_is_array(ctx, efNames)) {
                int n = pdf_array_len(ctx, efNames);
                for (int i = 0; i < n; i += 2) {
                    const char* nameStr = pdf_to_text_string(ctx, pdf_array_get(ctx, efNames, i));
                    pdf_obj* fs = pdf_array_get(ctx, efNames, i + 1);
                    pdf_filespec_params fsp{};
                    pdf_get_filespec_params(ctx, fs, &fsp);
                    PdfAttachmentInfo info;
                    info.name = str::Dup(nameStr ? Str(nameStr) : Str());
                    info.size = fsp.size;
                    VecAppend(outList, info);
                }
            }
            ok = true;
        }
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfAddAttachment(Str srcPath, Str destPath, Str attachFilePath) {
    if (len(srcPath) == 0 || len(destPath) == 0 || len(attachFilePath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        Str bytes = file::ReadFile(attachFilePath);
        if (len(bytes) == 0) fz_throw(ctx, FZ_ERROR_GENERIC, "empty attachment file");
        fz_buffer* fbuf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)bytes.s, (size_t)len(bytes));
        str::Free(bytes);

        TempStr baseName = path::GetBaseNameTemp(attachFilePath);
        pdf_obj* fs = pdf_add_embedded_file(ctx, doc, CStrTemp(baseName), nullptr, fbuf, -1, -1, 1);
        fz_drop_buffer(ctx, fbuf);

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* names = pdf_dict_get(ctx, root, PDF_NAME(Names));
        if (!names) {
            names = pdf_new_dict(ctx, doc, 2);
            pdf_dict_put_drop(ctx, root, PDF_NAME(Names), names);
        }
        pdf_obj* ef = pdf_dict_get(ctx, names, PDF_NAME(EmbeddedFiles));
        if (!ef) {
            ef = pdf_new_dict(ctx, doc, 2);
            pdf_dict_put_drop(ctx, names, PDF_NAME(EmbeddedFiles), ef);
        }
        pdf_obj* efNames = pdf_dict_get(ctx, ef, PDF_NAME(Names));
        if (!efNames) {
            efNames = pdf_new_array(ctx, doc, 2);
            pdf_dict_put_drop(ctx, ef, PDF_NAME(Names), efNames);
        }

        pdf_array_push_text_string(ctx, efNames, CStrTemp(baseName));
        pdf_array_push(ctx, efNames, fs);
        pdf_drop_obj(ctx, fs);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfExtractAttachment(Str srcPath, Str attachName, Str extractDestPath) {
    if (len(srcPath) == 0 || len(attachName) == 0 || len(extractDestPath) == 0) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (doc) {
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
            pdf_obj* names = pdf_dict_get(ctx, root, PDF_NAME(Names));
            pdf_obj* ef = pdf_dict_get(ctx, names, PDF_NAME(EmbeddedFiles));
            pdf_obj* efNames = pdf_dict_get(ctx, ef, PDF_NAME(Names));
            if (pdf_is_array(ctx, efNames)) {
                int n = pdf_array_len(ctx, efNames);
                for (int i = 0; i < n; i += 2) {
                    const char* cur = pdf_to_text_string(ctx, pdf_array_get(ctx, efNames, i));
                    if (str::EqI(Str(cur), attachName)) {
                        pdf_obj* fs = pdf_array_get(ctx, efNames, i + 1);
                        fz_buffer* b = pdf_load_embedded_file_contents(ctx, fs);
                        if (b) {
                            unsigned char* d = nullptr;
                            size_t sz = fz_buffer_storage(ctx, b, &d);
                            file::WriteFile(extractDestPath, Str((const char*)d, (int)sz));
                            fz_drop_buffer(ctx, b);
                            ok = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfDeleteAttachment(Str srcPath, Str destPath, Str attachName) {
    if (len(srcPath) == 0 || len(destPath) == 0 || len(attachName) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_obj* names = pdf_dict_get(ctx, root, PDF_NAME(Names));
        pdf_obj* ef = pdf_dict_get(ctx, names, PDF_NAME(EmbeddedFiles));
        pdf_obj* efNames = pdf_dict_get(ctx, ef, PDF_NAME(Names));

        if (pdf_is_array(ctx, efNames)) {
            pdf_obj* newNames = pdf_new_array(ctx, doc, pdf_array_len(ctx, efNames));
            int n = pdf_array_len(ctx, efNames);
            for (int i = 0; i < n; i += 2) {
                const char* cur = pdf_to_text_string(ctx, pdf_array_get(ctx, efNames, i));
                if (!str::EqI(Str(cur), attachName)) {
                    pdf_array_push(ctx, newNames, pdf_array_get(ctx, efNames, i));
                    pdf_array_push(ctx, newNames, pdf_array_get(ctx, efNames, i + 1));
                }
            }
            pdf_dict_put_drop(ctx, ef, PDF_NAME(Names), newNames);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

//--- (7) Bookmark / Outline Editor ---

static void CollectOutlinesRec(fz_context* ctx, fz_outline* node, int level, Vec<PdfBookmarkItem>& outList) {
    for (fz_outline* cur = node; cur; cur = cur->next) {
        PdfBookmarkItem item;
        item.title = str::Dup(cur->title ? Str(cur->title) : Str());
        item.pageNo = cur->page.page + 1;
        item.level = level;
        VecAppend(outList, item);
        if (cur->down) {
            CollectOutlinesRec(ctx, cur->down, level + 1, outList);
        }
    }
}

bool PdfListBookmarks(Str srcPath, Vec<PdfBookmarkItem>& outList) {
    if (len(srcPath) == 0) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    fz_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);

    fz_try(ctx) {
        doc = fz_open_document(ctx, CStrTemp(srcPath));
        if (doc) {
            fz_outline* outline = fz_load_outline(ctx, doc);
            if (outline) {
                CollectOutlinesRec(ctx, outline, 0, outList);
                fz_drop_outline(ctx, outline);
            }
            ok = true;
        }
    }
    fz_always(ctx) {
        if (doc) fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfSaveBookmarks(Str srcPath, Str destPath, const Vec<PdfBookmarkItem>& items) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        if (len(items) == 0) {
            pdf_dict_del(ctx, root, PDF_NAME(Outlines));
        } else {
            pdf_obj* outlines = pdf_new_dict(ctx, doc, 4);
            pdf_dict_put(ctx, outlines, PDF_NAME(Type), PDF_NAME(Outlines));
            pdf_dict_put_drop(ctx, root, PDF_NAME(Outlines), outlines);

            pdf_obj* prev = nullptr;
            pdf_obj* first = nullptr;

            for (const auto& item : items) {
                pdf_obj* node = pdf_new_dict(ctx, doc, 6);
                pdf_dict_put_text_string(ctx, node, PDF_NAME(Title), CStrTemp(item.title));
                pdf_dict_put(ctx, node, PDF_NAME(Parent), outlines);

                int pIdx = std::max(0, item.pageNo - 1);
                pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, pIdx);
                if (pageObj) {
                    pdf_obj* dest = pdf_new_array(ctx, doc, 3);
                    pdf_array_push(ctx, dest, pageObj);
                    pdf_array_push(ctx, dest, PDF_NAME(Fit));
                    pdf_dict_put_drop(ctx, node, PDF_NAME(Dest), dest);
                }

                pdf_obj* nodeRef = pdf_add_object(ctx, doc, node);
                pdf_drop_obj(ctx, node);

                if (!first) {
                    first = nodeRef;
                    pdf_dict_put(ctx, outlines, PDF_NAME(First), first);
                }
                if (prev) {
                    pdf_dict_put(ctx, prev, PDF_NAME(Next), nodeRef);
                    pdf_dict_put(ctx, nodeRef, PDF_NAME(Prev), prev);
                }
                prev = nodeRef;
            }
            if (prev) {
                pdf_dict_put(ctx, outlines, PDF_NAME(Last), prev);
            }
            pdf_dict_put_int(ctx, outlines, PDF_NAME(Count), len(items));
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfAutoGenerateBookmarksFromHeadings(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    Vec<PdfBookmarkItem> items;
    fz_var(doc);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (doc) {
            int numPages = pdf_count_pages(ctx, doc);
            for (int p = 0; p < numPages; p++) {
                pdf_page* page = pdf_load_page(ctx, doc, p);
                if (!page) continue;
                fz_stext_page* stext = fz_new_stext_page_from_page(ctx, (fz_page*)page, nullptr);
                if (stext) {
                    float maxFs = 0;
                    TempStr bestHeading;
                    for (fz_stext_block* b = stext->first_block; b; b = b->next) {
                        if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
                        for (fz_stext_line* l = b->u.t.first_line; l; l = l->next) {
                            if (!l->first_char) continue;
                            float fs = l->first_char->size;
                            if (fs > maxFs && fs >= 14.f) {
                                str::Builder lineText;
                                for (fz_stext_char* ch = l->first_char; ch; ch = ch->next) {
                                    if (ch->c >= 32 && ch->c < 127) lineText.AppendChar((char)ch->c);
                                }
                                Str s = ToStrTemp(lineText);
                                str::TrimWsBoth(s);
                                if (len(s) > 2 && len(s) < 80) {
                                    maxFs = fs;
                                    bestHeading = str::DupTemp(s);
                                }
                            }
                        }
                    }
                    fz_drop_stext_page(ctx, stext);
                    if (len(bestHeading) > 0) {
                        PdfBookmarkItem bm;
                        bm.title = str::Dup(bestHeading);
                        bm.pageNo = p + 1;
                        bm.level = 0;
                        VecAppend(items, bm);
                    }
                }
                fz_drop_page(ctx, (fz_page*)page);
            }
        }
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);

    if (len(items) == 0) {
        PdfBookmarkItem bm;
        bm.title = str::Dup(StrL("Document"));
        bm.pageNo = 1;
        bm.level = 0;
        VecAppend(items, bm);
    }
    bool ok = PdfSaveBookmarks(srcPath, destPath, items);
    for (auto& it : items) str::Free(it.title);
    return ok;
}

//--- (8) Page Labels / Numbering Ranges ---

bool PdfSetPageLabelRanges(Str srcPath, Str destPath, const Vec<PdfPageLabelRange>& ranges) {
    if (len(srcPath) == 0 || len(destPath) == 0 || len(ranges) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        for (const auto& r : ranges) {
            pdf_page_label_style st = PDF_PAGE_LABEL_DECIMAL;
            if (r.style == 'R')
                st = PDF_PAGE_LABEL_ROMAN_UC;
            else if (r.style == 'r')
                st = PDF_PAGE_LABEL_ROMAN_LC;
            else if (r.style == 'A')
                st = PDF_PAGE_LABEL_ALPHA_UC;
            else if (r.style == 'a')
                st = PDF_PAGE_LABEL_ALPHA_LC;

            const char* pfx = len(r.prefix) > 0 ? CStrTemp(r.prefix) : nullptr;
            pdf_set_page_labels(ctx, doc, r.startPageIndex, st, pfx, r.startNum);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfGetPageLabels(Str srcPath, Vec<Str>& outLabels) {
    if (len(srcPath) == 0) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (doc) {
            int numPages = pdf_count_pages(ctx, doc);
            char buf[128]{};
            for (int i = 0; i < numPages; i++) {
                pdf_page_label(ctx, doc, i, buf, sizeof(buf));
                VecAppend(outLabels, str::Dup(Str(buf)));
            }
            ok = true;
        }
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

//--- (9) Production / Linearize, Repair, Rasterize, Grayscale, Invert, Remove Annots/Links ---

bool PdfLinearize(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        pdf_write_options opts = gPdfDefaultWriteOptions;
        opts.do_clean = 1;
        opts.do_garbage = 2;
        opts.do_compress = 1;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfRepair(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        pdf_write_options opts = gPdfDefaultWriteOptions;
        opts.do_clean = 1;
        opts.do_garbage = 3;
        opts.do_compress = 1;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfRasterize(Str srcPath, Str destPath, int dpi) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    if (dpi < 72) dpi = 150;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* srcDoc = nullptr;
    pdf_document* dstDoc = nullptr;
    bool ok = false;
    fz_var(srcDoc);
    fz_var(dstDoc);
    fz_var(ok);

    fz_try(ctx) {
        srcDoc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, srcDoc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, srcDoc);

        dstDoc = pdf_create_document(ctx);
        float scale = dpi / 72.0f;

        for (int p = 0; p < numPages; p++) {
            fz_page* spage = fz_load_page(ctx, (fz_document*)srcDoc, p);
            if (!spage) continue;
            fz_pixmap* pm = fz_new_pixmap_from_page(ctx, spage, fz_scale(scale, scale), fz_device_rgb(ctx), 0);
            fz_drop_page(ctx, spage);
            if (!pm) continue;

            float w = pm->w / scale;
            float h = pm->h / scale;
            fz_rect mb = fz_make_rect(0, 0, w, h);

            pdf_obj* pageObj = pdf_add_page(ctx, dstDoc, mb, 0, nullptr, nullptr);
            fz_image* img = fz_new_image_from_pixmap(ctx, pm, nullptr);
            pdf_obj* imgObj = pdf_add_image(ctx, dstDoc, img);
            fz_drop_image(ctx, img);
            fz_drop_pixmap(ctx, pm);

            pdf_obj* res = pdf_dict_get(ctx, pageObj, PDF_NAME(Resources));
            if (!res) {
                res = pdf_new_dict(ctx, dstDoc, 2);
                pdf_dict_put_drop(ctx, pageObj, PDF_NAME(Resources), res);
            }
            pdf_obj* xobjs = pdf_dict_get(ctx, res, PDF_NAME(XObject));
            if (!xobjs) {
                xobjs = pdf_new_dict(ctx, dstDoc, 1);
                pdf_dict_put_drop(ctx, res, PDF_NAME(XObject), xobjs);
            }
            pdf_dict_puts_drop(ctx, xobjs, "RImg", imgObj);

            TempStr stm = fmt("q %.2f 0 0 %.2f 0 0 cm /RImg Do Q\n", w, h);
            pdf_obj* cstm = AddContentStream(ctx, dstDoc, stm);
            pdf_dict_put_drop(ctx, pageObj, PDF_NAME(Contents), cstm);
            pdf_insert_page(ctx, dstDoc, -1, pageObj);
            pdf_drop_obj(ctx, pageObj);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, dstDoc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, srcDoc);
        pdf_drop_document(ctx, dstDoc);
        srcDoc = nullptr;
        dstDoc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (srcDoc) pdf_drop_document(ctx, srcDoc);
        if (dstDoc) pdf_drop_document(ctx, dstDoc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfConvertToGrayscale(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        pdf_recolor_options ropts{1};
        for (int i = 0; i < numPages; i++) {
            pdf_recolor_page(ctx, doc, i, &ropts);
        }
        pdf_remove_output_intents(ctx, doc);

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfInvertColors(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        for (int i = 0; i < numPages; i++) {
            pdf_page* page = pdf_load_page(ctx, doc, i);
            if (!page) continue;
            fz_rect media = pdf_bound_page(ctx, page, FZ_MEDIA_BOX);

            pdf_obj* res = pdf_dict_get(ctx, page->obj, PDF_NAME(Resources));
            if (!res) {
                res = pdf_new_dict(ctx, doc, 2);
                pdf_dict_put_drop(ctx, page->obj, PDF_NAME(Resources), res);
            }
            pdf_obj* egs = pdf_dict_get(ctx, res, PDF_NAME(ExtGState));
            if (!egs) {
                egs = pdf_new_dict(ctx, doc, 1);
                pdf_dict_put_drop(ctx, res, PDF_NAME(ExtGState), egs);
            }
            pdf_obj* gsDiff = pdf_new_dict(ctx, doc, 2);
            pdf_dict_put(ctx, gsDiff, PDF_NAME(Type), PDF_NAME(ExtGState));
            pdf_dict_puts_drop(ctx, gsDiff, "BM", pdf_new_name(ctx, "Difference"));
            pdf_dict_puts_drop(ctx, egs, "GSDiff", gsDiff);

            str::Builder overlay;
            overlay.Append(fmt("q /GSDiff gs 1 1 1 rg %.2f %.2f %.2f %.2f re f Q\n", media.x0, media.y0,
                               media.x1 - media.x0, media.y1 - media.y0));
            pdf_obj* stm = AddContentStream(ctx, doc, ToStrTemp(overlay));

            pdf_obj* origContents = pdf_dict_get(ctx, page->obj, PDF_NAME(Contents));
            pdf_obj* arr = pdf_new_array(ctx, doc, 3);
            pdf_obj* push = AddContentStream(ctx, doc, StrL("q\n"));
            pdf_obj* pop = AddContentStream(ctx, doc, StrL("Q\n"));

            pdf_array_push(ctx, arr, push);
            if (pdf_is_array(ctx, origContents)) {
                int cLen = pdf_array_len(ctx, origContents);
                for (int k = 0; k < cLen; k++) pdf_array_push(ctx, arr, pdf_array_get(ctx, origContents, k));
            } else if (origContents) {
                pdf_array_push(ctx, arr, origContents);
            }
            pdf_array_push(ctx, arr, pop);
            pdf_array_push(ctx, arr, stm);
            pdf_dict_put(ctx, page->obj, PDF_NAME(Contents), arr);

            pdf_drop_obj(ctx, arr);
            pdf_drop_obj(ctx, push);
            pdf_drop_obj(ctx, pop);
            pdf_drop_obj(ctx, stm);
            fz_drop_page(ctx, (fz_page*)page);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfRemoveAnnotations(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        for (int i = 0; i < numPages; i++) {
            pdf_page* page = pdf_load_page(ctx, doc, i);
            if (!page) continue;
            pdf_annot* a = pdf_first_annot(ctx, page);
            while (a) {
                pdf_annot* next = pdf_next_annot(ctx, a);
                if (pdf_annot_type(ctx, a) != PDF_ANNOT_LINK) {
                    pdf_delete_annot(ctx, page, a);
                }
                a = next;
            }
            fz_drop_page(ctx, (fz_page*)page);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfRemoveAllLinks(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");
        int numPages = pdf_count_pages(ctx, doc);

        for (int i = 0; i < numPages; i++) {
            pdf_page* page = pdf_load_page(ctx, doc, i);
            if (!page) continue;
            pdf_annot* a = pdf_first_annot(ctx, page);
            while (a) {
                pdf_annot* next = pdf_next_annot(ctx, a);
                if (pdf_annot_type(ctx, a) == PDF_ANNOT_LINK) {
                    pdf_delete_annot(ctx, page, a);
                }
                a = next;
            }
            fz_drop_page(ctx, (fz_page*)page);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

//--- (10) Layers (OCG) Panel ---

bool PdfListLayers(Str srcPath, Vec<PdfLayerInfo>& outLayers) {
    if (len(srcPath) == 0) return false;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (doc) {
            int n = pdf_count_layers(ctx, doc);
            for (int i = 0; i < n; i++) {
                const char* name = pdf_layer_name(ctx, doc, i);
                int enabled = pdf_layer_is_enabled(ctx, doc, i);
                PdfLayerInfo info;
                info.name = str::Dup(name ? Str(name) : Str());
                info.isVisible = (enabled != 0);
                info.index = i;
                VecAppend(outLayers, info);
            }
            ok = true;
        }
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfSetLayersVisibility(Str srcPath, Str destPath, const Vec<bool>& visibility) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        int n = pdf_count_layers(ctx, doc);
        for (int i = 0; i < n && i < len(visibility); i++) {
            pdf_enable_layer(ctx, doc, i, visibility[i] ? 1 : 0);
        }

        pdf_write_options opts = gPdfDefaultWriteOptions;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

bool PdfFlattenLayers(Str srcPath, Str destPath) {
    if (len(srcPath) == 0 || len(destPath) == 0) return false;
    file::AtomicFile output(destPath);
    if (len(output.tempPath) == 0) return false;

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return false;
    fz_register_document_handlers(ctx);
    pdf_document* doc = nullptr;
    bool ok = false;
    fz_var(doc);
    fz_var(ok);

    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(srcPath));
        if (pdf_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "encrypted");

        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_dict_del(ctx, root, PDF_NAME(OCProperties));

        pdf_write_options opts = gPdfDefaultWriteOptions;
        opts.do_clean = 1;
        pdf_save_document(ctx, doc, CStrTemp(output.tempPath), &opts);
        pdf_drop_document(ctx, doc);
        doc = nullptr;
        ok = output.Commit();
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

//--- Dialog Classes for Features 2-10 ---

typedef bool (*PageToolRunner)(Str srcPath, Str destPath);

struct GenericActionDialog : PageToolDialog {
    Str actionName;
    PageToolRunner runner = nullptr;

    bool Create(MainWindow* w, WindowTab* tab, Str title, Str action, PageToolRunner fn);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

void GenericActionDialog::DoIt(VirtMouseEvent*) {
    if (!runner) {
        Close();
        return;
    }
    PdfToolEdit edit(win, srcPath);
    if (!edit.IsReady()) return;
    Str destPath = edit.outputPath;
    if (len(destPath) == 0) return;
    if (runner && runner(edit.inputPath, destPath)) {
        if (edit.Apply(CStrTemp(actionName))) Close();
    } else {
        MessageBoxWarning(hwnd, fmt("Failed to execute %s", actionName), actionName);
    }
}

bool GenericActionDialog::Create(MainWindow* w, WindowTab* tab, Str title, Str action, PageToolRunner fn) {
    actionName = action;
    runner = fn;
    if (!CreateToolDialog(w, tab, title)) return false;
    AddPathRow();

    AddButtonsRow(action);
    FinishDialog(nullptr);
    return true;
}

void ShowPdfNUpDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("N-Up / Grid Pages"), Tr("Generate N-Up"), [](Str s, Str d) {
            PdfNUpParams p;
            p.pagesPerSheet = 2;
            p.drawBorders = true;
            return PdfApplyNUp(s, d, p);
        }))
        delete dlg;
}

void ShowPdfBookletDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Create Booklet"), Tr("Create Booklet"), [](Str s, Str d) {
            PdfBookletParams p;
            return PdfApplyBooklet(s, d, p);
        }))
        delete dlg;
}

void ShowPdfPosterizeDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Posterize Pages"), Tr("Posterize"), [](Str s, Str d) {
            PdfPosterizeParams p;
            p.xTiles = 2;
            p.yTiles = 2;
            p.overlapPoints = 18.f;
            return PdfApplyPosterize(s, d, p);
        }))
        delete dlg;
}

void ShowPdfDividePagesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Divide Pages"), Tr("Divide"), [](Str s, Str d) {
            PdfDividePagesParams p;
            p.horizontalDivisions = 2;
            p.verticalDivisions = 1;
            return PdfApplyDividePages(s, d, p);
        }))
        delete dlg;
}

void ShowPdfCombineLongPageDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Combine into Long Page"), Tr("Combine"),
                     [](Str s, Str d) { return PdfApplyCombineLongPage(s, d); }))
        delete dlg;
}

void ShowPdfPageBackgroundDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Page Background"), Tr("Apply Background"), [](Str s, Str d) {
            PdfBackgroundParams p;
            p.useColor = true;
            p.color = RGB(253, 246, 227); // Soft solarized cream
            p.pagesSpec = StrL("all");
            return PdfApplyPageBackground(s, d, p);
        }))
        delete dlg;
}

struct DuplicatePagesDialog : PageToolDialog {
    int pageNo = 1;
    bool Create(MainWindow* w, WindowTab* tab, int currPage) {
        pageNo = currPage;
        if (!CreateToolDialog(w, tab, Tr("Duplicate Pages"))) return false;
        AddPathRow();

        AddButtonsRow(Tr("Duplicate"));
        FinishDialog(nullptr);
        return true;
    }
    void DoIt(VirtMouseEvent*) override {
        PdfToolEdit edit(win, srcPath);
        if (!edit.IsReady()) return;
        Str destPath = edit.outputPath;
        if (len(destPath) == 0) return;
        if (PdfDuplicatePages(edit.inputPath, destPath, fmt("%d", pageNo))) {
            if (edit.Apply("Duplicate pages")) Close();
        } else {
            MessageBoxWarning(hwnd, StrL("Failed to duplicate pages"), Tr("Duplicate Pages"));
        }
    }
};

void ShowDuplicatePagesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    int curr = win->ctrl ? win->ctrl->CurrentPageNo() : 1;
    auto* dlg = new DuplicatePagesDialog();
    if (!dlg->Create(win, tab, curr)) delete dlg;
}

void ShowReversePageOrderDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Reverse Page Order"), Tr("Reverse Order"),
                     [](Str s, Str d) { return PdfReversePageOrder(s, d); }))
        delete dlg;
}

struct InsertBlankPagesDialog : PageToolDialog {
    int pageNo = 1;
    bool Create(MainWindow* w, WindowTab* tab, int currPage) {
        pageNo = currPage;
        if (!CreateToolDialog(w, tab, Tr("Insert Blank Page"))) return false;
        AddPathRow();

        AddButtonsRow(Tr("Insert Blank Page"));
        FinishDialog(nullptr);
        return true;
    }
    void DoIt(VirtMouseEvent*) override {
        PdfToolEdit edit(win, srcPath);
        if (!edit.IsReady()) return;
        Str destPath = edit.outputPath;
        if (len(destPath) == 0) return;
        if (PdfInsertBlankPages(edit.inputPath, destPath, pageNo, 1)) {
            if (edit.Apply("Insert blank pages")) Close();
        } else {
            MessageBoxWarning(hwnd, StrL("Failed to insert blank page"), Tr("Insert Blank Page"));
        }
    }
};

void ShowInsertBlankPagesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    int curr = win->ctrl ? win->ctrl->CurrentPageNo() : 1;
    auto* dlg = new InsertBlankPagesDialog();
    if (!dlg->Create(win, tab, curr)) delete dlg;
}

void ShowRemoveBlankPagesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Remove Blank Pages"), Tr("Remove Blank"), [](Str s, Str d) {
            int rm = 0;
            return PdfRemoveBlankPages(s, d, &rm);
        }))
        delete dlg;
}

void ShowDeskewPagesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Deskew Pages"), Tr("Deskew"),
                     [](Str s, Str d) { return PdfDeskewPages(s, d, 0.2); }))
        delete dlg;
}

//--- Document Properties Dialog ---

struct DocPropsDialog : PageToolDialog {
    Edit* titleEdit = nullptr;
    Edit* authorEdit = nullptr;
    Edit* subjectEdit = nullptr;
    Edit* keywordsEdit = nullptr;

    bool Create(MainWindow* win, WindowTab* tab);
    void DoIt(VirtMouseEvent* ev = nullptr) override;
};

void DocPropsDialog::DoIt(VirtMouseEvent*) {
    PdfToolEdit edit(win, srcPath);
    if (!edit.IsReady()) return;
    Str destPath = edit.outputPath;
    if (len(destPath) == 0) return;

    PdfDocPropsParams params;
    params.title = titleEdit->GetTextTemp();
    params.author = authorEdit->GetTextTemp();
    params.subject = subjectEdit->GetTextTemp();
    params.keywords = keywordsEdit->GetTextTemp();

    if (PdfSetDocProps(edit.inputPath, destPath, params)) {
        if (edit.Apply("Edit properties")) Close();
    } else {
        MessageBoxWarning(hwnd, StrL("Failed to save document properties."), Tr("Properties"));
    }
}

bool DocPropsDialog::Create(MainWindow* w, WindowTab* tab) {
    if (!CreateToolDialog(w, tab, Tr("Edit Document Properties"))) return false;
    AddPathRow();

    PdfDocPropsParams curProps;
    auto* engine = tab->GetEngine();
    curProps.title = str::Dup(engine->GetPropertyTemp(DocProp::Title));
    curProps.author = str::Dup(engine->GetPropertyTemp(DocProp::Author));
    curProps.subject = str::Dup(engine->GetPropertyTemp(DocProp::Subject));
    curProps.keywords = str::Dup(engine->GetPropertyTemp(DocProp::Keywords));

    titleEdit = AddLabeledEdit(Tr("Title:"), curProps.title);
    authorEdit = AddLabeledEdit(Tr("Author:"), curProps.author);
    subjectEdit = AddLabeledEdit(Tr("Subject:"), curProps.subject);
    keywordsEdit = AddLabeledEdit(Tr("Keywords:"), curProps.keywords);

    AddButtonsRow(Tr("Save Properties"));
    FinishDialog(titleEdit);

    str::Free(curProps.title);
    str::Free(curProps.author);
    str::Free(curProps.subject);
    str::Free(curProps.keywords);
    str::Free(curProps.xmp);
    return true;
}

void ShowEditDocumentPropertiesDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new DocPropsDialog();
    if (!dlg->Create(win, tab)) delete dlg;
}

void ShowManageAttachmentsDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Manage Attachments"), Tr("Close"), nullptr)) delete dlg;
}

void ShowEditBookmarksDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Edit Bookmarks"), Tr("Auto-generate from Headings"),
                     [](Str s, Str d) { return PdfAutoGenerateBookmarksFromHeadings(s, d); }))
        delete dlg;
}

void ShowEditPageLabelsDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Page Numbering & Labels"), Tr("Apply Numbering"), [](Str s, Str d) {
            Vec<PdfPageLabelRange> r;
            PdfPageLabelRange r0{0, 'D', {}, 1};
            VecAppend(r, r0);
            return PdfSetPageLabelRanges(s, d, r);
        }))
        delete dlg;
}

void ShowPdfLinearizeDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Linearize PDF (Fast Web View)"), Tr("Linearize"),
                     [](Str s, Str d) { return PdfLinearize(s, d); }))
        delete dlg;
}

void ShowPdfRepairDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Repair Damaged PDF"), Tr("Repair PDF"),
                     [](Str s, Str d) { return PdfRepair(s, d); }))
        delete dlg;
}

void ShowPdfRasterizeDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Rasterize Document"), Tr("Rasterize"),
                     [](Str s, Str d) { return PdfRasterize(s, d, 150); }))
        delete dlg;
}

void ShowPdfGrayscaleDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Convert to Grayscale"), Tr("Convert"),
                     [](Str s, Str d) { return PdfConvertToGrayscale(s, d); }))
        delete dlg;
}

void ShowPdfInvertColorsDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Invert Document Colors"), Tr("Invert Colors"),
                     [](Str s, Str d) { return PdfInvertColors(s, d); }))
        delete dlg;
}

void ShowPdfRemoveAnnotationsDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Remove Annotations"), Tr("Remove"),
                     [](Str s, Str d) { return PdfRemoveAnnotations(s, d); }))
        delete dlg;
}

void ShowPdfRemoveLinksDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Remove All Links"), Tr("Remove Links"),
                     [](Str s, Str d) { return PdfRemoveAllLinks(s, d); }))
        delete dlg;
}

void ShowPdfManageLayersDialog(MainWindow* win) {
    if (!win || !win->IsDocLoaded()) return;
    WindowTab* tab = win->CurrentTab();
    if (!tab || !IsPdfDoc(tab)) return;
    auto* dlg = new GenericActionDialog();
    if (!dlg->Create(win, tab, Tr("Document Layers (OCG)"), Tr("Flatten Layers"),
                     [](Str s, Str d) { return PdfFlattenLayers(s, d); }))
        delete dlg;
}
