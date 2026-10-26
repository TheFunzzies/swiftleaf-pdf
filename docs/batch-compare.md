# Batch processing and PDF comparison

The batch runner applies an ordered JSON action list to each PDF, including
PDFs discovered under input folders. It writes outputs to the requested folder
or next to each source with the configured suffix. A failed file is reported
without stopping the remaining files. See [the CLI reference](cli.md) for the
action schema and command examples.

Optimizer presets are `web`, `print`, `archive`, and `min-size`; `compress` is
an alias for the optimizer. Batch results include the source and output byte
counts for each optimized file, plus imageBytes, fontBytes, objectBytes and
keptOriginal when the optimizer runs.

- **web** — 225/150 dpi, JPEG 75, for screen viewing
- **print** — 300 dpi, JPEG 85, for printing
- **archive** — lossless (no image recoding); structural cleanup only. Not PDF/A.
- **min-size** — 144/96 dpi, JPEG 50, smallest possible

Output files are kept only if smaller than the source; otherwise the original
is preserved.

PDF comparison produces an HTML report with page navigation, a word-level text
diff, side-by-side page images, and a rendered-pixel change ratio for each
shared page. Pages that exist in only one input are reflected in the report
summary. The report is written as HTML; the CLI returns its machine-readable
summary as JSON.
