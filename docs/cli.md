# Command-line batch tools

## Batch actions

Run a saved JSON definition against PDF files or folders:

```powershell
Swiftleaf.exe -batch actions.json input.pdf other.pdf folder\
```

Directories are searched recursively for PDFs. The process writes one JSON
object to stdout and exits with code 0 only when every file succeeds. A batch
definition has an optional `suffix` (default `_batch`), optional `outputFolder`,
and an ordered `actions` array. Named definitions are stored under the app data
directory by the batch action store.

```json
{
  "suffix": "_ready",
  "outputFolder": "C:\\PDF\\output",
  "actions": [
    { "type": "ocr" },
    { "type": "watermark", "text": "CONFIDENTIAL", "pages": "all" },
    { "type": "export-text", "pages": "1-3" }
  ]
}
```

Supported action types are `ocr`, `compress` and `optimize` (set `text` to
`web`, `print`, `archive`, or `min-size`), `watermark`, `header-footer`,
`bates`, `crop`, `rotate`, `flatten-layers`, `sanitize`, `redact-matches`, `properties`,
`export-text`, `export-html`, `export-markdown`, and `export-json`.
Exports must be last. Multi-file PNG/SVG plans are rejected. Plans are strictly
validated and existing/colliding outputs are never overwritten.
Watermark, header/footer, Bates, crop and export actions accept a `pages` value.
The `text` property supplies watermark/Bates text or the search term for
`redact-matches`. Document properties use `title`, `author`, `subject`, and
`keywords`. Crop `value` is the margin in points; rotate `value` is degrees and
must be a multiple of 90.

Each result contains the input, success state, and output path or failed action.
Optimizer results also include `beforeBytes` and `afterBytes`.
The final output is written to a temporary sibling and renamed into place only
after the final action succeeds. Intermediate outputs from a successful chain
are removed.

## Compare PDFs

Generate an HTML comparison report:

```powershell
Swiftleaf.exe -compare old.pdf new.pdf -report compare.html
```

The process writes a JSON summary to stdout and returns nonzero if either PDF
cannot be opened or the report cannot be written. The report lists page counts,
page text, word-count differences, and the percentage of changed rendered
pixels for each page.
