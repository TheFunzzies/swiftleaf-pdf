# Scan & OCR

The Windows app's All tools sidebar, Edit ribbon and command palette expose
`CmdScanOcr` (Scan & OCR). Open a document, choose Current page or All pages,
then review the recognized text. Copy text copies all results; the result box
also supports selecting and copying part of the text. Save as .txt writes UTF-8.

OCR uses Windows.Media.Ocr and locally installed Windows OCR language packs.
It prefers the user's profile languages, falling back to the first available
OCR language. No downloads or third-party OCR libraries are bundled. A missing
language pack produces installation guidance in the result dialog.

Rendering and recognition run on a worker thread. Progress reports the page
being processed. Cancel keeps completed pages; closing the dialog cancels the
job. Tabs and document windows can be closed while OCR runs. Pages render at
300 DPI, reduced when necessary to fit Windows OCR's maximum image dimension.

Limitations:

- Results are plain text, without layout or formatting. Recognition can make
  mistakes; review the output.
- The PDF is unchanged. OCR does not embed an invisible text layer or make the
  original PDF searchable. Search the exported text in a text editor.
- The worker reads the saved document. Save pending annotations and form edits
  before OCR. The current page and viewing rotation are captured when opening
  the dialog.
- Recognition uses one installed language per job. There is no language picker,
  scanner acquisition, handwriting mode or table reconstruction.
- Windows OCR must be available. The portable ng app does not expose this command.

Validation: `bun cmd/run-unit-tests.ts -dbg --no-build` includes bitmap-size
checks and a real Windows OCR smoke test on generated text. The recognition
smoke explicitly skips if Windows OCR or its language packs are unavailable.
`bun tests/ad-hoc-ocr.ts --no-build` checks the native dialog with rendered PDF
pages; it requires an installed OCR language pack.
