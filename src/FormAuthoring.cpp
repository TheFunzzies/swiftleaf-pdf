/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/GuessFileType.h"
#include "gui/UIModels.h"
#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "Annotation.h"
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
#include "SumatraLog.h"

static const Str fieldKinds[] = {TrN("Text field"), TrN("Checkbox"), TrN("Radio group"), TrN("Dropdown"),
                                 TrN("Unsigned signature")};

struct FieldDialog {
    MainWindow* win;
    Annotation* widget = nullptr;
    Annotation* created = nullptr;
    int pageNo = 0;
    RectF rect;
    FormFieldProperties props;
};

bool IsFormAuthoringCommand(int cmdId) {
    return cmdId >= CmdPrepareForm && cmdId <= CmdCreateFormSignature;
}

static void UpdateFieldDialog(HWND dlg, FieldDialog* data) {
    int kind = (int)SendDlgItemMessageW(dlg, IDC_FIELD_TYPE, CB_GETCURSEL, 0, 0);
    bool choice = kind == (int)FormFieldKind::Combo || kind == (int)FormFieldKind::Radio;
    EnableWindow(GetDlgItem(dlg, IDC_FIELD_OPTIONS), choice);
    EnableWindow(GetDlgItem(dlg, IDC_FIELD_DEFAULT), kind != (int)FormFieldKind::Signature);
    EnableWindow(GetDlgItem(dlg, IDC_FIELD_SIZE),
                 kind == (int)FormFieldKind::Text || kind == (int)FormFieldKind::Combo);
    if (!data->widget && choice && !len(HwndGetTextTemp(GetDlgItem(dlg, IDC_FIELD_OPTIONS)))) {
        HwndSetText(GetDlgItem(dlg, IDC_FIELD_OPTIONS), StrL("Option1\r\nOption2"));
    }
}

static void FormAuthoringChanged(MainWindow* win) {
    WindowTab* tab = win->CurrentTab();
    if (tab) {
        tab->formFieldCount = -1;
        tab->formBannerDismissed = false;
    }
    MainWindowRerender(win);
    ToolbarUpdateStateForWindow(win, true);
    FormBannerUpdate(win);
}

static INT_PTR CALLBACK FieldDialogProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* data = (FieldDialog*)GetWindowLongPtrW(dlg, DWLP_USER);
    if (msg == WM_INITDIALOG) {
        data = (FieldDialog*)lp;
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        HwndSetText(dlg, data->widget ? Tr("Field properties") : Tr("Create form field"));
        const int labels[] = {IDC_FIELD_TYPE_LABEL,
                              IDC_FIELD_NAME_LABEL,
                              IDC_FIELD_DEFAULT_LABEL,
                              IDC_FIELD_SIZE_LABEL,
                              IDC_FIELD_OPTIONS_LABEL,
                              IDC_FIELD_REQUIRED,
                              IDC_FIELD_READONLY,
                              IDC_FIELD_HINT,
                              IDOK,
                              IDCANCEL};
        for (int id : labels) {
            HWND ctrl = GetDlgItem(dlg, id);
            HwndSetText(ctrl, trans::GetTranslation(HwndGetTextTemp(ctrl)));
        }
        for (Str kind : fieldKinds)
            SendDlgItemMessageW(dlg, IDC_FIELD_TYPE, CB_ADDSTRING, 0,
                                (LPARAM)CWStrTemp(ToWStrTemp(trans::GetTranslation(kind))));
        SendDlgItemMessageW(dlg, IDC_FIELD_TYPE, CB_SETCURSEL, (int)data->props.kind, 0);
        EnableWindow(GetDlgItem(dlg, IDC_FIELD_TYPE), !data->widget);
        HwndSetText(GetDlgItem(dlg, IDC_FIELD_NAME), data->props.name);
        HwndSetText(GetDlgItem(dlg, IDC_FIELD_DEFAULT), data->props.defaultValue);
        HwndSetText(GetDlgItem(dlg, IDC_FIELD_SIZE), fmt("%g", data->props.fontSize));
        HwndSetText(GetDlgItem(dlg, IDC_FIELD_OPTIONS), data->props.options);
        CheckDlgButton(dlg, IDC_FIELD_REQUIRED, data->props.required ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_FIELD_READONLY, data->props.readOnly ? BST_CHECKED : BST_UNCHECKED);
        UpdateFieldDialog(dlg, data);
        return TRUE;
    }
    if (msg != WM_COMMAND || !data) return FALSE;
    int id = LOWORD(wp);
    if (id == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    if (id == IDC_FIELD_TYPE && HIWORD(wp) == CBN_SELCHANGE) {
        UpdateFieldDialog(dlg, data);
        return TRUE;
    }
    if (id != IDOK) return FALSE;
    FormFieldProperties props;
    props.kind = (FormFieldKind)SendDlgItemMessageW(dlg, IDC_FIELD_TYPE, CB_GETCURSEL, 0, 0);
    props.name = HwndGetTextTemp(GetDlgItem(dlg, IDC_FIELD_NAME));
    props.defaultValue = HwndGetTextTemp(GetDlgItem(dlg, IDC_FIELD_DEFAULT));
    props.options = HwndGetTextTemp(GetDlgItem(dlg, IDC_FIELD_OPTIONS));
    props.required = IsDlgButtonChecked(dlg, IDC_FIELD_REQUIRED) == BST_CHECKED;
    props.readOnly = IsDlgButtonChecked(dlg, IDC_FIELD_READONLY) == BST_CHECKED;
    Str size = HwndGetTextTemp(GetDlgItem(dlg, IDC_FIELD_SIZE));
    char* end = nullptr;
    props.fontSize = strtof(CStrTemp(size), &end);
    Str error;
    if (!end || *end || !len(size)) error = Tr("Enter a numeric font size.");
    if (props.kind == FormFieldKind::Signature) props.defaultValue = {};
    if (!len(error)) {
        if (data->widget) {
            if (SetFormFieldProperties(data->widget, props, error)) {
                FormAuthoringChanged(data->win);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
        } else {
            data->created = CreateFormField(data->win->AsFixed()->GetEngine(), data->pageNo, data->rect, props, error);
            if (data->created) {
                FormAuthoringChanged(data->win);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
        }
    }
    if (!len(error)) error = Tr("This field cannot be edited.");
    MessageBoxW(dlg, CWStrTemp(ToWStrTemp(error)), CWStrTemp(ToWStrTemp(Tr("Form field"))), MB_OK | MB_ICONWARNING);
    return TRUE;
}

Annotation* PlaceFormField(MainWindow* win, int cmdId, int pageNo, RectF rect) {
    FieldDialog data{win};
    data.pageNo = pageNo;
    data.rect = rect;
    data.props.kind = cmdId == CmdPrepareForm ? FormFieldKind::Text : (FormFieldKind)(cmdId - CmdCreateFormText);
    data.props.name = fmt("Field%d", EngineMupdfCountFormFields(win->AsFixed()->GetEngine()) + 1);
    CommitFormFieldEdit(true);
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_FORM_FIELD), win->hwndFrame,
                                     FieldDialogProc, (LPARAM)&data);
    if (result == -1) logf("Create form field dialog failed: %d\n", (int)GetLastError());
    return data.created;
}

void EditFormFieldProperties(MainWindow* win, Annotation* widget) {
    FieldDialog data{win};
    data.widget = widget;
    if (!win || !GetFormFieldProperties(widget, data.props)) return;
    CommitFormFieldEdit(true);
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_FORM_FIELD), win->hwndFrame, FieldDialogProc,
                    (LPARAM)&data);
}
