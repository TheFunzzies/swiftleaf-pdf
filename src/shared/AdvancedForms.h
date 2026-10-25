/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

struct FormCandidate {
    int pageNo;
    RectF rect;
    FormFieldKind kind = FormFieldKind::Text;
};

enum class FormDataFormat {
    Fdf,
    Xfdf,
    Csv,
    Json
};
enum class FormTabOrder {
    Row,
    Column,
    Structure,
    Manual
};

bool DetectFormFields(EngineBase*, Vec<FormCandidate>&, Str& error);
bool SetFormTabOrder(EngineBase*, FormTabOrder, Str manualNames, Str& error);
TempStr GetFormTabNames(EngineBase*);
TempStr ExportFormData(EngineBase*, FormDataFormat, Str& error);
bool ImportFormData(EngineBase*, FormDataFormat, Str data, Str& error);
bool ClearFormData(EngineBase*, Str& error);
bool ResetFormData(EngineBase*, Str& error);
bool SaveFlattenedForm(EngineBase*, Str path, Str& error);
