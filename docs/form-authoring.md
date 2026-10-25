# Form authoring

Choose **Prepare a Form** in All tools, Fill & Sign, or the command palette.
Draw a rectangle, then choose text, checkbox, radio group, dropdown, unsigned
signature, list box, push button, image or date. Escape cancels placement.
In Edit PDF, select a field and choose **Field properties** to edit it.
Save persists AcroForm fields and generated appearances.

## Detect fields

**Detect Fields** scans page text and vector artwork for underscore runs,
horizontal rules, rectangular boxes and space after label colons. The preview
lists page numbers and field rectangles in PDF points. Select the candidates
to create; Cancel does not change the PDF. Existing widgets are excluded.
This is a heuristic, capped at 2,048 candidates; scans need OCR first. Review
false positives and use Prepare a Form for missed fields. Small square boxes
become checkboxes; other candidates become text fields. Names are generated
as Detected1, Detected2, etc., avoiding existing names.

## Properties and field types

Properties include name, tooltip, required/read-only, default value, font,
font size (0 for automatic), text/border/background RGB, border width,
maximum length, multiline, comb and choice/radio options. Colors are three
space-separated numbers from 0 to 1. Fonts are Helv (Helvetica), Cour
(Courier) or TiRo (Times Roman). Comb requires a positive maximum length and
cannot be multiline. Editing a default also applies it as the current value.

Checkbox defaults are Yes or Off. Choice options are unique values, one per
line. Radio defaults are an option or Off. A radio group starts with at least
two buttons, arranged vertically; editing keeps its button count fixed.
List boxes have a single selected value. Labels are separate page text.

Push-button actions are reset, submit or link. Reset restores default values.
Submit and link require an HTTP/HTTPS target. SubmitForm is stored for other
readers; SwiftLeaf does not send form data over HTTP. Use Export Data instead.
Link opens the URL through SwiftLeaf's normal browser policy.

Image fields are push-button widgets with an embedded image appearance.
Enter an image path in Target when authoring, or click the field outside Edit
PDF to choose an image. The image fits inside the field without stretching.
Images are embedded; local paths are not saved in the PDF.

Date fields are text widgets with a descriptive format such as
`date:yyyy-mm-dd`. The Format property also accepts descriptive number,
percent, ZIP and phone formats. These descriptions are saved as SwiftLeaf
metadata; they do not mask or validate input in other readers.

## Tab order

**Tab Order** offers rows, columns, structure and manual order. Manual order
lists every field name, one per line; radio siblings share their field name.
Rows, columns and manual reorder widget entries in each page's Annots array.
SwiftLeaf follows that order for editable text and choice widgets. Structure
writes /Tabs /S and requires a structure tree; interpreting arbitrary tagged
structure order inside SwiftLeaf is not supported. Navigation currently wraps
within the current page. Other readers may apply their own tab-order policy.

## Data and flattening

**Export Data** and **Import Data** support FDF, XFDF, CSV and JSON. Choose a
file extension matching the format. CSV uses two columns headed `name,value`,
with one field per row and standard quote escaping. JSON is a flat object of
field names to string values. FDF/XFDF use named string values. Unicode and
line breaks round-trip. Signatures, push buttons and images are excluded.
Multi-select arrays are not supported.

Imports reject malformed data, duplicate/unknown names, read-only fields,
invalid choice/button values and text exceeding MaxLen. An import is one
undo operation and failures roll back all values. Imports are limited to
16 MB. **Clear Form** empties values (buttons become Off), keeping field
names and defaults; Undo restores them. Reset buttons restore defaults.

**Flatten Form** saves a separate PDF with widget appearances merged into
page content and interactive fields removed. Comments remain interactive.
Unsaved edits are included. The open document stays editable; review the
saved copy before replacing an original.

## Compatibility limits

Names created here are unique and contain no dots. Existing nested field
hierarchies and signed signatures cannot be edited in Field properties.
XFA and rich text are not supported. Existing inherited appearance styles
may be simplified when edited.

MuPDF's calculation, range-validation and formatting hooks in
`ext/mupdf/source/pdf/pdf-form.c` read JavaScript from AA/C/JS, AA/V/JS and
AA/F/JS. There is no supported JS-free sum/product/average/min/max or numeric
range action. SwiftLeaf therefore does not emit inert AA entries or claim
portable calculations, ranges or input masks. Use a reader supporting those
scripts to author them; this feature adds no JavaScript.

## Commands and verification

Placement: CmdPrepareForm, CmdCreateFormText, CmdCreateFormCheckbox,
CmdCreateFormRadio, CmdCreateFormCombo, CmdCreateFormSignature,
CmdCreateFormListBox, CmdCreateFormPushButton, CmdCreateFormImage,
CmdCreateFormDate. Operations: CmdDetectFormFields, CmdFormTabOrder,
CmdImportFormData, CmdExportFormData, CmdClearForm, CmdFlattenForm.

Debug unit tests create, save and reopen all nine field kinds; inspect
properties, values and tab order; round-trip all four data formats; reject
invalid imports; and reopen flattened output. `bun tests/form-authoring.ts
--no-build` smoke-tests placement, properties, move/resize and filling.
GUI execution is verified in Windows CI when Device Guard blocks local apps.
