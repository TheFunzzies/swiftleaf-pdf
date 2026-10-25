# Batch actions and PDF comparison

Batch definitions are JSON objects with an ordered `actions` array. Each action
reads the output of the previous action. `suffix` names the final output and
`outputFolder` selects its directory. If no folder is set, outputs sit beside
the source file.

```json
{
  "suffix": "_ready",
  "outputFolder": "C:\\PDF\\processed",
  "actions": [
    { "type": "sanitize" },
    { "type": "watermark", "text": "REVIEWED", "pages": "all" },
    { "type": "optimize", "text": "web" }
  ]
}
```

Supported action types are `watermark`, `header-footer`, `bates`, `crop`, `rotate`,
`flatten-layers`, `properties`, `redact-matches`, `sanitize`, `encrypt`,
`optimize`, and `export-text`. Watermark, header/footer, Bates, crop and rotate accept
`pages`; watermark/header/footer/Bates use `text`; crop uses a point margin in
`value`; property updates use `title`, `author`, `subject` and `keywords`;
redaction uses `text`; encryption uses `text` as the password. Optimizer
presets are `web`, `print`, `archive` and `min-size`. Each optimized result
includes input and output byte counts in the JSON result.

`SaveBatchPreset` and `LoadBatchPreset` store named JSON definitions atomically
under the app data directory's `BatchActions` folder. The batch engine returns
one JSON result per input, including output path or failed action. Folder inputs
are expanded recursively by the CLI.

Compare reports contain a page summary, a per-page low-resolution pixel
heatmap, and word-level insertions/deletions. The report is linked to each page
from its summary list. It is HTML and can be opened in a browser.

The current entry point is the CLI. The interactive Action Wizard, live
progress/cancel UI, side-by-side/overlay viewer, image export action and PDF
report export are not wired yet.
