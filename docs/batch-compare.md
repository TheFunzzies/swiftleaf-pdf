# Batch processing and PDF comparison

Batch validates the entire plan and all destination paths before creating outputs.
Empty/unknown actions, wrong JSON types, unknown/duplicate fields, invalid presets,
invalid ranges and exports followed by another action are rejected. Rotation is
90, 180 or 270 degrees; crop is 0 through 14,400 points. Suffixes contain 1–64
letters, digits, underscores or hyphens. Limits: 64 actions, 10,000 inputs, 1 MiB JSON.

Outputs must be distinct, absent and different from every input. Same-basename
inputs targeting one folder fail preflight. Publication never overwrites an existing
file, including one created after preflight. Every intermediate is an exclusively
created sibling; cleanup deletes only those owned files. Completed outputs remain
when a later action/file fails or the user cancels. The wizard previews the validated
PDF-to-PDF/export steps and every input/output path before execution.

Optimizer presets: `web`, `print`, `archive`, `min-size`; `compress` is an alias.
Text, HTML, Markdown and JSON exports must be last. Legacy multi-file PNG/SVG
batch exports are rejected until a manifest transaction can publish all artifacts
safely. Use the standalone export workflow instead. See [CLI reference](cli.md).

Compare produces HTML or PDF, with separate word edits, paired page numbers,
page previews and pixel-change percentages. Exact text/raster fingerprints align
moved pages; word sketches pair similar edited text pages. Unmatched pages include their
text and image as insertions/deletions (page 0 means absent). Repeated text is matched
in occurrence order. Edited scans without exact raster matches remain positional; this is not
OCR-aware or semantic alignment. Preview resolution is 72 dpi, reduced when needed
to at most 2048 pixels per side; small visual differences can still be missed.

Reports stream one page pair at a time to a unique sibling and publish atomically.
Load, extraction, render, write, story overflow and cancellation failures preserve
any existing report and return failure, never an invented 0% result. No partial
report is published. Report paths cannot alias either input.

Compare and Find and Replace run on workers. Dialogs show progress and support
Cancel; closing a dialog requests cancellation without freeing the running job.
Cancellation is cooperative between pages/blocks and during word scans, not an
interrupt of a decoder, renderer or filesystem call. Replacement retains the source
engine while taking a snapshot, includes unsaved edits, and saves a separate copy.

Compare limits: 10,000 pages per input, 2 MiB extracted text per page, 4,194,304 LCS
cells per page pair, 64 MiB MuPDF cache and 2048-pixel preview sides. Exceeding a
limit fails explicitly. Temporary text/images are released after each pair; the
report is never retained in full. These are working-set limits, not a hard process
memory cap: individual PDF objects/decoders can allocate more. Replacement limits:
10,000 pages, 4096 blocks and 2 MiB scanned text per page, 4096-byte find/replacement
strings, and 2 MiB replacement text per block. Unsupported text still stops the copy.

Focused regression: `bun tests/ad-hoc-compare-safety.ts --no-build` after a debug build.

Memory probe: `bun tests/ad-hoc-compare-memory.ts --no-build`. On 2026-10-26,
x64 Debug, synthetic 600x800 RGB noise scans compared with themselves:
16 pages produced 58.7 MiB HTML at 77.0 MiB peak working set (18.4 seconds);
80 pages produced 293.3 MiB HTML at 79.9 MiB peak (57.5 seconds).
The fixture reuses one image across pages. This demonstrates streamed-report
memory behavior, not a general PDF decoder memory ceiling or performance promise.
