# Changelog

## 0.3.2 — Unreleased

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
