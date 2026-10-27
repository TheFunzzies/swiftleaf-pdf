# Batch processing and PDF comparison

The batch runner applies an ordered JSON action list to each PDF, including
PDFs discovered under input folders. It writes outputs to the requested folder
or next to each source with the configured suffix. A failed file is reported
without stopping the remaining files. See [the CLI reference](cli.md) for the
action schema and command examples.

Optimizer presets are `web`, `print`, `archive`, and `min-size`; `compress` is
an alias for the optimizer. Batch results include the source and output byte
counts for each optimized file.

Plans are validated before the first write. Empty or unknown actions, optimizer
presets outside the list above, and export actions that are not last are
rejected. Planned intermediates and final outputs must be unique across
recursive inputs and must not overwrite an input file; a rejected plan writes
nothing. The Action Wizard shows a text preview of the actions and each input's
final output before starting. Intermediate results are removed without ever
deleting an input file.

PDF comparison produces an HTML report with page navigation, a word-level text
diff, side-by-side page images, and a rendered-pixel change ratio for each
shared page. Pages are aligned by text similarity, so an inserted, removed or
moved page is reported once and later pages still compare against their own
partner instead of shifting by index. The word diff reports every separated
edit instead of swallowing the unchanged text between them; it is bounded, and
a changed span too large for the exact LCS table is reported as one coarse edit.
Pages that exist in only one input are fully detailed with text diffs, previews,
and links in the report summary. If text extraction, page rendering, document
loading, or report generation fails, errors are reported visibly instead of
appearing as empty text or false 0% pixel changes. Reports are written
atomically, and truncated PDF stories fail without leaving corrupt output. The
report is written as HTML (or PDF); the CLI returns its machine-readable summary
as JSON, including section, edit-run, moved/added/removed page and coarse-diff
page counts.

Compare runs in a worker with page progress and Cancel. Closing the dialog
requests cancellation without waiting for the worker. Cancellation is checked
between pages and PDF report layout steps; a running MuPDF call must finish.
HTML is streamed page by page to a temporary file. PDF reports lay out one
section at a time. Only a completed report replaces the destination.
Extraction, rendering, layout and write failures return failure, without a
misleading zero-change result.

Compare uses a 32 MiB MuPDF cache and previews at most 1,024 pixels per side.
Page text over 16 MiB is rejected. Scratch text/images are released per page;
these limits bound report overhead, not all memory used to parse a PDF or
decode a single source image. Pixel ratios use the capped preview resolution.

### Compression presets and honesty

Optimizer presets are `web`, `print`, `archive`, and `min-size`; `compress` is
an alias for the optimizer:

- `archive`: Lossless stream deflation, object stream compacting, and structure preservation (`--structure=keep`) for long-term storage. Note: does not convert to ISO PDF/A compliance.
- `print`: High-resolution image preservation (subsamples above 450 DPI down to 300 DPI, JPEG 85) and stream compression for physical printing.
- `web`: Balanced screen compression (subsamples photos above 225 DPI to 150 DPI, JPEG 75).
- `min-size`: Aggressive compression (subsamples photos above 144 DPI to 96 DPI, JPEG 50).

Optimization audits report `beforeBytes`, `afterBytes`, `imageBytes`, `fontBytes`, and `objectBytes`. If optimization would cause the output file size to grow compared to the original, SwiftLeaf keeps the original document rather than producing a larger file.
