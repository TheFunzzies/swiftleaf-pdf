# Batch processing and PDF comparison

The batch runner applies an ordered JSON action list to each PDF, including
PDFs discovered under input folders. It writes outputs to the requested folder
or next to each source with the configured suffix. A failed file is reported
without stopping the remaining files. See [the CLI reference](cli.md) for the
action schema and command examples.

Optimizer presets are `web`, `print`, `archive`, and `min-size`; `compress` is
an alias for the optimizer. Batch results include the source and output byte
counts for each optimized file.

PDF comparison produces an HTML report with page navigation, a word-level text
diff, side-by-side page images, and a rendered-pixel change ratio for each
shared page. Pages that exist in only one input are reflected in the report
summary. The report is written as HTML; the CLI returns its machine-readable
summary as JSON.

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
