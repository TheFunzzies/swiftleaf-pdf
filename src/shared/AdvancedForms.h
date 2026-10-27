/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

struct fz_context;
struct pdf_obj;
struct pdf_document;
class EngineMupdf;

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
void ReadFormActions(fz_context*, pdf_obj* field, FormFieldProperties& props);
void WriteFormActions(fz_context*, pdf_document*, pdf_obj* field, const FormFieldProperties& props);
void SyncFormCalculationOrder(fz_context*, EngineMupdf*);
bool SetFormTabOrder(EngineBase*, FormTabOrder, Str manualNames, Str& error);
TempStr GetFormTabNames(EngineBase*);
TempStr GetFormCalculationNames(EngineBase*);
bool SetFormCalculationOrder(EngineBase*, Str manualNames, Str& error);
TempStr ExportFormData(EngineBase*, FormDataFormat, Str& error);
bool ImportFormData(EngineBase*, FormDataFormat, Str data, Str& error);
bool ClearFormData(EngineBase*, Str& error);
bool ResetFormData(EngineBase*, Str& error);
bool SaveFlattenedForm(EngineBase*, Str path, Str& error);
