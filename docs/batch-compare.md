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
