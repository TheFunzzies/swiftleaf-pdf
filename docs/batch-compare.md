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
shared page. Pages that exist in only one input are reflected in the report
summary. The report is written as HTML; the CLI returns its machine-readable
summary as JSON.
