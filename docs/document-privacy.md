# Merge and document privacy

Merge PDF and Insert File retain imported AcroForm fields, comments, links,
bookmarks and embedded files. Fields keep their hierarchy; colliding root names
and attachment names get an `SL<source>_<number>_` prefix. Internal destinations,
including named destinations, follow the selected pages through reordering.
Links to omitted pages are disabled. Base-document catalog data is retained.

Imported JavaScript is removed because field renaming can invalidate scripts.
Imported digital signature values are cleared: rewriting a signed PDF cannot
preserve its signature validity. Imported accessibility tags are omitted.
Unsupported XFA forms use appearance flattening for the form alone. A warning
lists these fallbacks after a successful merge or insertion. Ordinary AcroForm
fields and other annotations remain interactive.

## Sanitize document

Open **Protect > Sanitize**, **All tools > Sanitize document**, or the Tools menu.
Save a separate copy. Each option can be selected independently:

- Metadata: document information, XMP metadata and editing history dictionaries.
- Hidden text/layers: export the visible page at 150 dpi, discarding original
  text and graphics. This also removes forms, comments and links after rendering
  their visible appearances. It reduces image quality and removes searchable
  text. Other options determine whether comments/forms are removed before rendering.
- Embedded JavaScript: document, field, page and annotation script actions.
- Attachments: embedded-file name trees, associated files and attachment annotations.
- Comments: annotations except form widgets and links.
- Form data and fields: fields, default/current values and widget appearances.
- Thumbnails: embedded page previews.

Defaults select everything except hidden text/layers. Unselected categories can
still contain private information. Form removal clears the fields rather than
printing their values onto the page. Sanitizing does not remove text already
painted into ordinary page artwork; use redaction for that content.

## Redact all matches

Open **Protect > Search & Redact**, **All tools > Redact all matches**, or the
Tools menu. Enter literal text or enable regular expressions. Matching is case
sensitive and runs across all pages in extracted reading order, with newlines
between lines. Empty regex matches are skipped. Invalid or excessively expensive
patterns fail without replacing the destination.

Form fields are flattened before searching, so a matched value cannot remain in
the form tree or its old appearance. Disabled layers are included in the search;
their visibility settings are retained in the saved copy.

Redaction rewrites page content beneath each matched glyph. Touching images and
vector graphics are removed, so nearby artwork can disappear too. The result is
a black redaction, not an opaque annotation covering recoverable text. Saving
rewrites the complete PDF and garbage-collects obsolete objects; it never appends
an incremental revision. Tags and thumbnails are removed from affected exports
because they can retain copies of redacted content.

Search uses existing PDF text, including invisible OCR text. It does not OCR
images, search attachment contents or sanitize unrelated metadata.
Run Sanitize separately for those categories. Review the saved copy before
sharing it, especially when a document has unusual fonts or text reading order.

All outputs use the atomic-save helper described in [recovery.md](recovery.md).
Debug unit tests reopen saved PDFs and inspect destinations, fields, attachments
and extracted text: `bun cmd/run-unit-tests.ts -dbg --no-build`.
