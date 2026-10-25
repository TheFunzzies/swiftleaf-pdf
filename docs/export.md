# Export and create PDF

Use **Convert > Export PDF** or **All tools > Export a PDF**. Save edits first:
conversion reads the saved document. Choose the format, destination and page range
(`all`, `1-3`, `1-3,5`). Existing destinations require confirmation. Conversion runs
in the background; Cancel leaves the destination unchanged.

| Format     | Contents                                                                                                             |
| ---------- | -------------------------------------------------------------------------------------------------------------------- |
| DOCX       | Editable paragraphs, inferred headings and list indentation, ruled tables, PNG images, web/mail hyperlinks           |
| PPTX       | One slide per selected page; editable positioned text, images, web/mail hyperlinks                                   |
| XLSX       | One sheet per selected page; ruled table cells plus remaining text rows. Values are text, never executable formulas. |
| HTML       | Paragraphs, headings, lists, tables, links and embedded PNG images                                                   |
| RTF        | Unicode text, headings and embedded images                                                                           |
| Markdown   | Headings, list markers, tables, links and embedded image data URLs                                                   |
| Plain text | Text with approximate horizontal spacing and page breaks                                                             |
| JSON       | Page/block/line/character bounds, Unicode text, font sizes, links and image data                                     |
| SVG ZIP    | A separate `page-N.svg` for each selected page, including vector paths and images                                    |

**To Images** retains the existing PNG/JPEG/BMP export dialog and its DPI options.

Conversion is approximate, not layout-perfect. PDF text has no reliable paragraph,
heading, list or table semantics. Heading sizes and axis-aligned ruling lines are
heuristics. One grid per page is inferred; disconnected tables, merged cells,
rotated text and decorative rectangles can be misinterpreted. Tables appear after
body text in DOCX/HTML/Markdown. Slide pages are fitted to a common 4:3 canvas.
Excel does not reproduce charts or page images. RTF does not preserve tables or
hyperlinks. Markdown image data URLs are not supported by every Markdown reader.
Typography, reading order and complex columns need review. Scans need OCR before
editable export. SVG preserves appearance with text outlines, not editable text.
Password-protected inputs must be saved as unlocked copies first.

## Create PDF

Use **Convert > From Files** or **All tools > Create a PDF**. Select one file or
select several to combine their visible pages into one PDF. The page range applies
to each input; a range invalid for any input aborts the entire conversion. The
file-picker selection order determines the combined order.

- Office files (`doc`, `docx`, `xls`, `xlsx`, `ppt`, `pptx`, `odt`, `ods`, `odp`):
  use the matching Microsoft Office application when its automation server is
  available. Otherwise, use LibreOffice from PATH or its normal Program Files
  installation. If neither is available, the dialog explains what to install.
- Text, HTML and Markdown: paginate with MuPDF's HTML story layout. Markdown uses
  the existing Markdown parser. HTML uses local relative resources and static print
  layout; it does not execute JavaScript. Wide unbreakable content can fail to fit.
- RTF: import Unicode plain text using Windows Rich Edit, then paginate with the
  same story layout. Rich RTF formatting is not retained.
- Images and PDFs: render their visible pages into the new PDF. Existing image
  collection conversion remains available through **From Image**.
- **Convert > From Clipboard**: create a PDF from the current clipboard image.

Combining creates visible pages; interactive forms, bookmarks, document metadata
and interactive links are not preserved. Source files are not modified. Temporary
Office output is isolated and removed after conversion. Microsoft Office automation
disables macros and opens source files read-only. Office's blocking export call
cannot be interrupted safely; Cancel takes effect when that call returns. A
LibreOffice conversion can be terminated and has a five-minute timeout.

## Validation

`docs/test/export-fixture.py` regenerates `export-fixture.pdf` with two pages,
Unicode-safe escaping cases, a ruled table, a hyperlink and an image. Debug unit
tests export all nine formats, unzip and parse OOXML parts, verify text/table/image
and link contents, page selection, formula-safe cells, cancellation and preservation
of existing destinations. They also create and combine HTML/Markdown PDFs and
export the existing `swiftleaf-sample.pdf` fixture.

Run `bun cmd/run-unit-tests.ts -dbg --no-build` after a Debug build. Office and
LibreOffice automation require an installed application and remain environment-
dependent integration checks; unit tests do not launch Office.
