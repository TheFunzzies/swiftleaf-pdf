# Redaction, compression and release reports

Search and Redact previews numbered matches with 1-based pages and page-space
boxes. Search is case-sensitive. Regex accepts patterns such as `ID-[0-9]+`
or `[^ @]+@[^ @]+`; check the results before applying. Repeated identifiers
are separate matches. Enter match numbers/ranges to select a subset; blank
selects all. The optional reason is burned into each selected area as an
overlay; currently printable ASCII only. Long overlays shrink to fit.

Apply uses the document snapshot captured by Preview. Editing the query or
mode requires another preview. Document edits made after Preview require
clicking Preview again. Image-only scans need OCR first. Search includes
invisible text and disabled layers; fields are flattened before searching.
Touching images and graphics may be removed in full.

Applied redactions save a separate, nonincremental, garbage-collected PDF.
They remove tags, ActualText/Alt dictionaries, metadata, attachments, scripts,
forms, comments and thumbnails because those may retain sensitive copies.
This loses interactive/accessibility data. Review the saved file. Regression
tests reopen it and inspect extracted text and decoded objects, including
selected/unselected identifiers, alternate text, metadata and attachments.
This is a regression fixture, not a claim of forensic coverage of every PDF.

Compression presets:

- Archive and Print use identical lossless cleanup, without downsampling.
  Archive does **not** convert to or validate PDF/A. Use the separate standards
  tools and an independent validator for compliance.
- Web reduces photos above 225 dpi to 150 dpi, JPEG quality 75.
- Minimum Size reduces photos above 144 dpi to 96 dpi, JPEG quality 50.

Lossy presets can lose detail. No preset guarantees a particular size. If the
candidate grows, the destination receives the original bytes. Source overwrite
is rejected. The report shows input stored image and embedded-font stream
bytes; all remaining bytes are other objects/structure. These are storage
contributions, not decoded memory sizes or estimates of possible savings.
Malformed/encrypted inputs can make the breakdown unavailable.

`pwsh -File tools/release-gate.ps1` compares portable/installer sizes against
`tools/release-baseline.json`. The baseline records v0.5.0 published asset
sizes, not a target budget. Only growth strictly above 10% fails. Missing
inputs and performance failures are advisory and explicitly unavailable.
The build workflow uploads the report even on failure.

The script launches a fresh hidden process with `-for-testing -bench` per
corpus PDF and samples working set/private bytes. Load + first render and
P95 sequential page load/render are engine proxies, not measured cold-cache
UI readiness/navigation. OS cache is not flushed. Default fixtures are smoke
samples; use `-Corpus` with representative text, image-heavy and huge PDFs
on a fixed machine for release comparisons. Optional Office/OCR resources
are excluded and must be recorded separately. Baseline updates need an
explicitly reviewed release measurement; CI never rewrites the baseline.
