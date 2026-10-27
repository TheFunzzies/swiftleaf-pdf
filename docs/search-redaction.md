# Search redaction

Open Search and Redact. Enter literal text or a case-sensitive regex; email
and US SSN buttons fill common patterns. These patterns find candidates,
not validated identities. Run OCR first for scanned pages.

Preview lists each occurrence with a stable ID, page and page-coordinate box.
The preview uses a frozen copy, including unsaved edits. Remove IDs from the
comma-separated selection to keep those occurrences. Cancel and reopen to
change the search. Apply Selected Matches writes a separate PDF.

Optional overlay text replaces each selected area. If empty, the reason is
used as the label. Labels use Helvetica; keep them short to fit the box.
Do not put sensitive text in replacement labels.

Selected areas lose text and touching images/graphics. Form fields are
flattened before searching; disabled layers and invisible text are searched.
After a removal, metadata, scripts, attachments, comments, thumbnails and
structure tags are discarded. This sacrifices interactive/tagged utility.
Unselected visible content remains. Saves rewrite the file with garbage
collection rather than append revisions. Keep the original separately.

Regression tests reopen saved copies and inspect extracted text and decoded
objects, including inline ActualText/Alt, old streams/resources and metadata.
This is a focused corpus, not a guarantee for arbitrary malformed PDFs.
