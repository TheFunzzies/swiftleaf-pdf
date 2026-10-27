# Batch processing and PDF comparison

Batch plans are validated before creating outputs. Empty/unknown actions,
wrong JSON types, duplicate/unknown keys, invalid optimizer presets and page
ranges, ignored parameters, and exports followed by PDF actions are rejected.
Encryption must also be last. At most 32 actions and 10,000 inputs are accepted.
Suffixes cannot contain path separators or Windows filename metacharacters.

Outputs must be distinct from every input and from each other. Existing files,
including same-basename collisions from recursive folders, stop the whole plan
before execution. Rename refuses a destination created after validation. Only
private temporary intermediates are deleted. Earlier completed outputs remain
when a later file fails or the user cancels; JSON reports each attempted file.
The Action Wizard shows an ordered, typed input/output preview before starting.

Optimizer presets: `web`, `print`, `archive`, `min-size`; `compress` is an alias.
Exports must be last. SVG export is one `.zip` containing SVG pages. Loose PNG
batch exports are rejected until they have a multi-file publication transaction.
Cancellation stops between actions and before publishing an output; the current
OCR/optimizer action may need to finish. Closing the dialog requests cancellation
without freeing worker-owned data.

Compare writes HTML or PDF reports, one section at a time. Bounded word LCS
keeps separate insertions/deletions around unchanged words. Word edit totals
count inserted plus deleted words, so replacing one word counts as two edits.
Exact text-and-render fingerprints align unchanged pages across insertions and
moves. Remaining pages pair in order within unmatched gaps. Duplicate pages
match in occurrence order. Edited moved pages and OCR normalization are not
fuzzy-matched. A shifted page is labelled �Moved or shifted�; this does not
claim to distinguish a move from displacement by an insertion.

Inserted/deleted pages include text and thumbnails. Paired pages include a
thumbnail pixel-change ratio. Thumbnail size is capped at 1,024 pixels per side;
this is not a full-resolution visual comparison or localized-region detector.
Extraction, incomplete rendering, output errors, cancellation, PDF story
width overflow and pagination limits fail the operation. No partial report
replaces the destination, and neither input can be used as the report path.

Compare runs on a worker with progress and cancellation between page operations
and PDF report pages. Limits: 4,096 pages per input, 1 MiB extracted text per
page, 4,194,304 LCS cells per pair, 1,000 PDF report pages per section, and a
64 MiB MuPDF cache. Exceeding a limit fails visibly. Report memory is per-section;
source PDF structures and decoded resources still depend on the document.

Debug regression tests cover separated edits, alignment, limits, output guards,
HTML/PDF reports and cancellation. Run the debug executable with `-unit-tests
-for-ai -for-testing`; set `SWIFTLEAF_COMPARE_TESTS=1` for the focused compare,
batch and replacement suite. See [CLI reference](cli.md) for action examples.
