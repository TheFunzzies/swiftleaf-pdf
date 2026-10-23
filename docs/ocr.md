# Scan & OCR

Open a PDF, then choose **Scan & OCR** from All tools, Edit or the command
palette (`CmdScanOcr`). Choose an installed OCR language, or Automatic to use
Windows profile languages. The language choice is remembered across launches.
Install additional OCR language packs through Windows Settings.

Choose **Current page**, **All pages**, or enter a range such as `1-3, 5` and
choose **Selected pages**. Page numbers are physical, one-based PDF pages.
Open-ended ranges such as `3-` extend to the last page. Invalid ranges are
rejected; repeated pages are processed once.

Review the recognized text, then choose **Save PDF...** and a new filename.
The saved copy has an aligned invisible text layer: reopen it to search,
select or copy recognized words. Copy text also copies the dialog results.
The original document stays unchanged. Unselected pages, images, annotations
and existing PDF content are retained, without rasterizing the document.
Non-PDF documents still support recognition and copying; PDF saving requires
a PDF source.

## Processing and cancellation

Rendering, recognition and PDF writing run on a worker thread. Progress shows
the processed page count. Cancel stops recognition between pages or during the
Windows OCR operation. Closing the dialog cancels the job; closing its source
tab is safe. A failed or cancelled recognition cannot be saved as a partial
PDF, but completed plain-text results remain available to copy.

Save writes a temporary file next to the chosen destination, then replaces
the destination only after success. Cancellation during serialization takes
effect before this replacement; the UI remains responsive while writing
finishes. Failed or cancelled saves leave an existing destination unchanged.

## Text layer

Pages render at 300 DPI, reduced to fit Windows OCR's maximum image size.
Windows word bounding boxes are mapped from bitmap pixels through view zoom
and rotation, page rotation, crop origin and PDF UserUnit into PDF coordinates.
Each page receives a separate Form XObject with PDF text rendering mode 3
(invisible). MuPDF embeds its Helvetica substitute and necessary Unicode
fallback fonts as CID fonts with ToUnicode maps. Existing page graphics state
and resources are isolated from the new layer.

Recognition may contain mistakes. Existing text is retained, so OCR on an
already searchable page can produce duplicate search or copy results. Use
selected pages for image-only scans. Word boxes preserve position, not exact
original typography, table structure or handwriting. OCR uses one language
per job. Scanner acquisition and deskew are not included.

The worker reads the saved document. Save annotations and form edits before
OCR. Current page and viewing rotation are captured when opening the dialog.
Windows OCR and at least one installed OCR language pack are required for
recognition. The portable ng app does not expose this command.

## Validation

`bun cmd/run-unit-tests.ts -dbg --no-build` includes deterministic
save/reopen/search tests for all combinations of right-angle page and view
rotation, a nonzero CropBox, UserUnit scaling, Unicode text, unselected-page
preservation and identical rendered pixels. These tests need no OCR pack.
A separate Windows OCR smoke test starts from a generated image-only PDF and
asserts that its saved OCR output is searchable after reopen; it explicitly
skips when Windows OCR or language packs are unavailable.

`bun tests/ad-hoc-ocr.ts --no-build` checks the native dialog, current/all-page
recognition, clipboard, PDF saving/reopened search and close-during-work.
It requires an installed OCR language pack and an interactive Windows desktop.
Device Guard may prevent running locally built executables; run these checks
on a Windows CI runner in that environment.
