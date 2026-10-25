# Batch processing and PDF comparison

The batch runner applies an ordered JSON action list to each PDF, including
PDFs discovered under input folders. It writes outputs to the requested folder
or next to each source with the configured suffix. A failed file is reported
without stopping the remaining files. See [the CLI reference](cli.md) for the
action schema and command examples.

Optimizer presets are `web`, `print`, `archive`, and `min-size`; `compress` is
an alias for the optimizer. Presets use the existing PDF compressor and expose
before/after byte counts through the optimizer API.

PDF comparison produces an HTML report with page navigation, extracted text,
word counts, and a rendered-pixel change ratio for each shared page. Pages that
exist in only one input are reflected in the report summary. The report is
written as HTML; the CLI returns its machine-readable summary as JSON.
