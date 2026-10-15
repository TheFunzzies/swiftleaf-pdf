# Changelog

## 0.3.3 — 2026-10-15

### Improved

- Add Text starts an empty draft with transparent fill and no PDF border.
  Placeholder text is no longer written into new annotations; existing text
  is preserved. Empty or cancelled drafts leave no annotation or undo entry.
- Accepted text stays selected and can be moved, reopened with a double-click
  or Enter, and edited with contextual font, size, color, Move and Delete controls.
- The native text editor previews the PDF beneath it, including rules and images.
  Text sizing follows zoom and rotation; dark-page previews remain readable.

### Fixed

- Separate typing, selection and frame-drag transitions so moving text does not
  resize its box. Enter inserts a newline, Ctrl+Enter accepts, and Escape cancels.
- Preserve explicit annotation fill, opacity and border styling in the editor.
- Retain drafts when creation fails and prevent stale font resources after
  undoing creation and adding text again.

### Validation and limitations

- Focused native regression tests cover drafts, placement, editing, formatting,
  transparency, custom styling and undo. Saved PDFs were independently rendered
  and inspected; 29 editing-safety checks passed.
- Real IME composition, physical multi-monitor DPI transitions, high-contrast
  mode and screen-reader use remain unverified. Rotated editing stays upright;
  saved text follows the PDF page axes. Full Unicode replacement of existing PDF
  text remains unfinished. No performance improvement has been measured.

## 0.3.2 — 2026-10-15

### Improved

- Cleaner light and dark ribbon surfaces, clearer active tabs, larger labels,
  and more compact navigation-rail corners.
- Adaptive tab spacing, selective label compaction, ellipsis for long labels,
  and dropdown indicators that remain visible in compact mode.
- Page Undo becomes available immediately after a page change and disables
  itself after the final undo.

### Fixed

- Page changes now require a unique undo snapshot, avoiding collisions between
  documents with identical filenames and separate app instances.
- Undo snapshots preserve unsaved edits. Failed undo operations retain their
  history entry so restoration can be retried.
- Failed file replacement retains a snapshot of unsaved edits for recovery
  through **Undo Page Change**.
- Edit Text rejects unsupported characters before removing the original text,
  instead of silently substituting question marks. Rejected drafts remain
  available for correction or copying.
- Windows-1252 characters such as Œ, š, Ÿ and € use MuPDF's complete mapping.

### Validation and limitations

- Added 29 focused regression checks for undo, failure recovery, toolbar state,
  character validation and PDF string encoding.
- Native smoke tests verified byte-identical page undo, unsupported-text
  rejection, and an unsaved accented-text edit surviving rotate and undo with
  exact text extraction from the saved PDF.
- Full Unicode editing, crash recovery, unified undo history and ribbon
  overflow remain unfinished. Imported forms and annotations are still
  flattened. No performance improvement has been measured.

## 0.3.1

Earlier releases predate this changelog. See the
[GitHub release history](https://github.com/TheFunzzies/swiftleaf-pdf/releases).
