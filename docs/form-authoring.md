# Form authoring

Choose **Prepare a form** in All tools, Fill & Sign, or the command palette.
Drag a rectangle on a PDF page, or click its opposite corners. The dialog
creates a text field, checkbox, radio group, dropdown, or unsigned signature
field. Escape cancels placement; Cancel closes the dialog without creating a
field.

Set the name, default value, required/read-only flags, font size (0 for
automatic), and choice options. Checkbox defaults are `Yes` or `Off`.
Dropdown and radio options are unique values, one per line. A radio group
starts with at least two buttons, arranged vertically inside the rectangle.
Its default is an option value or `Off`.

In **Edit PDF**, click a field to select it. Drag it to move, drag its handles
to resize, or use the property toolbar to edit its properties or delete it.
Radio buttons can be moved, resized, and deleted individually. Turn Edit PDF
off to fill fields or sign an unsigned signature field. Save writes AcroForm
widgets and appearance streams; other PDF readers and Highlight fields can
detect them.

Direct placement commands:

- `CmdPrepareForm`
- `CmdCreateFormText`
- `CmdCreateFormCheckbox`
- `CmdCreateFormRadio`
- `CmdCreateFormCombo`
- `CmdCreateFormSignature`

Limits: field names are unique and contain no dots. Radio options can be
renamed, but their count stays fixed when editing properties; create another
group to change its size. Labels are separate page text, not generated from
option values. Properties editing supports simple fields and radio groups;
existing nested field hierarchies and signed signatures are excluded. Advanced
formatting, validation scripts, calculations, XFA, and push buttons are outside
this feature. Editing a default value also applies it as the current value.

Verification: `bun tests/form-authoring.ts --no-build` exercises placement,
the dialog, move/resize, save/reopen, and filling. The Debug unit tests cover
all five types, rotated pages, flags, defaults, options, deletion, and direct
MuPDF inspection of the saved widgets.
