/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"
#include "base/GuessFileType.h"
#include "gui/UIModels.h"
#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "Annotation.h"
#include "AdvancedForms.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "SumatraPDF.h"
#include "Commands.h"
#include "Toolbar.h"
#include "DocumentPanel.h"
#include "FormFields.h"
#include "Translations.h"
#include "resource.h"
#include "FormAuthoring.h"

struct FormPreview {
    Vec<FormCandidate> candidates;
    Vec<int> selected;
    Str names;
    Str manual;
    FormTabOrder order = FormTabOrder::Row;
    bool tabOrder = false;
};

static INT_PTR CALLBACK FormPreviewProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* data = (FormPreview*)GetWindowLongPtrW(dlg, DWLP_USER);
    if (msg == WM_INITDIALOG) {
        data = (FormPreview*)lp;
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        HwndSetText(dlg, data->tabOrder ? Tr("Tab order") : Tr("Detect fields — preview"));
        HWND list = GetDlgItem(dlg, IDC_FORM_CANDIDATES);
        ShowWindow(list, data->tabOrder ? SW_HIDE : SW_SHOW);
        ShowWindow(GetDlgItem(dlg, IDC_FORM_ORDER), data->tabOrder ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dlg, IDC_FORM_NAMES), data->tabOrder ? SW_SHOW : SW_HIDE);
        HwndSetText(GetDlgItem(dlg, IDC_FORM_SUMMARY),
                    data->tabOrder ? Tr("Choose order. For manual order, put every field name on its own line. "
                                        "Structure requires a tagged PDF.")
                                   : Tr("Review likely fields by page and rectangle (PDF points). Select the fields to "
                                        "create. Cancel leaves the PDF unchanged."));
        if (data->tabOrder) {
            const Str orders[] = {TrN("By row"), TrN("By column"), TrN("By structure"), TrN("Manual")};
            for (Str s : orders)
                SendDlgItemMessageW(dlg, IDC_FORM_ORDER, CB_ADDSTRING, 0,
                                    (LPARAM)CWStrTemp(ToWStrTemp(trans::GetTranslation(s))));
            SendDlgItemMessageW(dlg, IDC_FORM_ORDER, CB_SETCURSEL, 0, 0);
            HwndSetText(GetDlgItem(dlg, IDC_FORM_NAMES), data->names);
        } else {
            for (const FormCandidate& c : data->candidates) {
                Str s = fmt("Page %d — %s — x %.0f, y %.0f, width %.0f, height %.0f", c.pageNo,
                            c.kind == FormFieldKind::Checkbox ? Tr("Checkbox") : Tr("Text field"), c.rect.x, c.rect.y,
                            c.rect.dx, c.rect.dy);
                SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)CWStrTemp(ToWStrTemp(s)));
            }
            SendMessageW(list, LB_SETSEL, TRUE, -1);
        }
        return TRUE;
    }
    if (msg != WM_COMMAND || !data) return FALSE;
    if (LOWORD(wp) == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    if (LOWORD(wp) != IDOK) return FALSE;
    if (data->tabOrder) {
        data->order = (FormTabOrder)SendDlgItemMessageW(dlg, IDC_FORM_ORDER, CB_GETCURSEL, 0, 0);
        data->manual = str::Dup(HwndGetTextTemp(GetDlgItem(dlg, IDC_FORM_NAMES)));
    } else {
        for (int i = 0; i < len(data->candidates); i++)
            if (SendDlgItemMessageW(dlg, IDC_FORM_CANDIDATES, LB_GETSEL, i, 0) > 0) VecAppend(data->selected, i);
    }
    EndDialog(dlg, IDOK);
    return TRUE;
}

static void RefreshForms(MainWindow* win) {
    if (auto* tab = win->CurrentTab()) {
        tab->formFieldCount = -1;
        tab->formBannerDismissed = false;
    }
    MainWindowRerender(win);
    ToolbarUpdateStateForWindow(win, true);
    FormBannerUpdate(win);
}

bool StartFormAction(MainWindow* win, Annotation* widget) {
    FormFieldProperties props;
    if (!GetFormFieldProperties(widget, props) || props.readOnly) return false;
    if (props.kind != FormFieldKind::PushButton && props.kind != FormFieldKind::Image) return false;
    CommitFormFieldEdit(true);
    Str error;
    bool ok = true;
    if (props.kind == FormFieldKind::Image) {
        WCHAR path[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = win->hwndFrame;
        ofn.lpstrFile = path;
        ofn.nMaxFile = dimofi(path);
        ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return true;
        props.target = ToUtf8Temp(WStr(path));
        ok = SetFormFieldProperties(widget, props, error);
    } else if (str::Eq(props.action, StrL("reset"))) {
        ok = ResetFormData(win->AsFixed()->GetEngine(), error);
    } else if (str::Eq(props.action, StrL("link"))) {
        ok = SumatraLaunchBrowser(props.target);
    } else {
        error =
            Tr("This button contains a SubmitForm action for other PDF readers. SwiftLeaf cannot submit form data to a "
               "URL. Use Export Data.");
        ok = false;
    }
    RefreshForms(win);
    if (!ok)
        MessageBoxW(win->hwndFrame, CWStrTemp(ToWStrTemp(len(error) ? error : Tr("The button action failed."))),
                    L"Forms", MB_OK | MB_ICONWARNING);
    return true;
}

static Str FormDataPath(HWND owner, int cmd, FormDataFormat& format) {
    WCHAR path[MAX_PATH] = {};
    bool flatten = cmd == CmdFlattenForm;
    bool import = cmd == CmdImportFormData;
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = flatten
                          ? L"PDF (*.pdf)\0*.pdf\0"
                          : L"FDF (*.fdf)\0*.fdf\0XFDF (*.xfdf)\0*.xfdf\0CSV (*.csv)\0*.csv\0JSON (*.json)\0*.json\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimofi(path);
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (import ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    ofn.lpstrDefExt = flatten ? L"pdf" : L"fdf";
    if (!(import ? GetOpenFileNameW(&ofn) : GetSaveFileNameW(&ofn))) return {};
    Str result = ToUtf8Temp(WStr(path));
    if (str::EndsWithI(result, StrL(".xfdf")))
        format = FormDataFormat::Xfdf;
    else if (str::EndsWithI(result, StrL(".csv")))
        format = FormDataFormat::Csv;
    else if (str::EndsWithI(result, StrL(".json")))
        format = FormDataFormat::Json;
    else
        format = FormDataFormat::Fdf;
    return result;
}

void RunFormOperation(MainWindow* win, int cmd) {
    if (!win || !win->AsFixed()) return;
    auto* engine = win->AsFixed()->GetEngine();
    if (!EngineMupdfIsPdf(engine) || !EngineSupportsAnnotations(engine)) return;
    CommitFormFieldEdit(true);
    Str error;
    bool ok = true;
    if (cmd == CmdDetectFormFields || cmd == CmdFormTabOrder) {
        FormPreview data;
        data.tabOrder = cmd == CmdFormTabOrder;
        data.names = str::Dup(GetFormTabNames(engine));
        if (!data.tabOrder) {
            ok = DetectFormFields(engine, data.candidates, error);
            if (ok && !len(data.candidates)) {
                MessageBoxW(win->hwndFrame, L"No likely fields found. Use Prepare a Form to draw fields.",
                            L"Detect fields", MB_OK);
                str::Free(data.names);
                return;
            }
        }
        if (ok && DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_FORM_PREVIEW), win->hwndFrame,
                                  FormPreviewProc, (LPARAM)&data) == IDOK) {
            if (data.tabOrder)
                ok = SetFormTabOrder(engine, data.order, data.manual, error);
            else {
                StrVec names;
                Split(&names, data.names, StrL("\r\n"), true);
                int n = 1;
                for (int i : data.selected) {
                    FormFieldProperties props;
                    do {
                        props.name = fmt("Detected%d", n++);
                    } while (names.Contains(props.name));
                    const FormCandidate& c = data.candidates[i];
                    props.kind = c.kind;
                    if (!CreateFormField(engine, c.pageNo, c.rect, props, error)) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        str::Free(data.names);
        str::Free(data.manual);
    } else if (cmd == CmdClearForm) {
        if (MessageBoxW(win->hwndFrame,
                        L"Clear field values? Field definitions and defaults are kept. This can be undone.",
                        L"Clear form", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;
        ok = ClearFormData(engine, error);
    } else {
        FormDataFormat format = FormDataFormat::Fdf;
        Str path = FormDataPath(win->hwndFrame, cmd, format);
        if (!len(path)) return;
        if (cmd == CmdImportFormData) {
            Str data = file::ReadFile(path);
            ok = ImportFormData(engine, format, data, error);
            str::Free(data);
        } else if (cmd == CmdFlattenForm) {
            if (str::EqI(path, win->CurrentTab()->filePath)) {
                error = Tr("Choose a different file for the flattened copy.");
                ok = false;
            } else
                ok = SaveFlattenedForm(engine, path, error);
        } else {
            Str data = ExportFormData(engine, format, error);
            ok = len(data) && file::WriteFile(path, data);
        }
    }
    RefreshForms(win);
    if (!ok) {
        if (!len(error)) error = Tr("The form operation failed.");
        MessageBoxW(win->hwndFrame, CWStrTemp(ToWStrTemp(error)), CWStrTemp(ToWStrTemp(Tr("Forms"))),
                    MB_OK | MB_ICONWARNING);
    }
}
