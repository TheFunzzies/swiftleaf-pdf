# Changelog

## [Unreleased]

### Added

- Three new page stamping tools in All tools, Organize tab, and Tools menu:
  - **Add watermark**: Text watermark with customizable opacity, rotation angle, font size, position/centered, color, and page range.
  - **Add header/footer**: Header and footer text across left, center, right alignments with dynamic tokens {page}, {pages}, {date}, {filename} and page range.
  - **Bates numbering**: Standard legal Bates numbering with customizable prefix, starting number, digit count, position, and page range.
- Page stamps unit tests asserting text extraction on the right pages.
- Documentation for page marks and stamps in docs/page-stamps.md.

## 0.4.1 - 2026-10-23

### Changed

- The top ribbon tabs now drive the left panel: All tools, Read, Edit, Convert
  and E-Sign each show their own tools there, and the second tool strip is
  gone, leaving one slim top bar. The left panel scrolls with the mouse wheel.
- Both side panels can be resized by dragging and remember their width. On
  narrow windows the tools panel shrinks to an icon rail and the comments panel
  hides, so the document keeps its space.
- Smooth panel, tab and hover animations that turn off when Windows
  "reduce motion" is enabled.

### Fixed

- Accessibility: keyboard navigation through the whole top bar, screen reader
  names for icon-only buttons, checked state for toggles, high-contrast focus
  outlines, and focus kept when switching tabs.
- Robustness: saved panel widths are range-checked, and several null-pointer
  and animation-list edge cases found in review were fixed.

## 0.4.0 - 2026-10-23

### Added

- PDFCraft-style interface: an "All tools" sidebar, a mode tab bar with a
  command search box, a comments panel grouped by page with replies, an icon
  rail, a floating annotation tool strip and a form-fields banner.
- Scan & OCR using the Windows OCR engine (current or all pages, copy or save as
  text). The result is not written into the PDF as a text layer.
- Form authoring: text, checkbox, radio, dropdown and signature fields with a
  properties dialog, saved as real AcroForm fields.

### Changed

- The product is now consistently named SwiftLeaf; upstream credits remain in
  the README and AUTHORS. Settings and registry names are unchanged so existing
  installs keep working (see `docs/rename-notes.md`).
- Repository reorganized: stale branches removed, old notes moved to
  `docs/archive/`, README links the latest release.

### Security

- Extracted attachments are opened more safely: unsafe extensions are neutralized
  and embedded HTML attachments are not run.
- Fixed a clipboard read race and added keyboard access and contrast fixes to
  the new banner and panels.

### Limitations

- OCR needs an installed Windows OCR language; form authoring cannot change a
  radio group's option count after creation. The binaries are unsigned.

## 0.3.4 - 2026-10-20

### Security

- The updater stages downloads in a protected per-user folder, keeps the file
  locked against changes while its SHA-256 is checked, and rechecks it before
  launch. The installer rechecks the hash and keeps the file locked until
  installation or portable copy finishes. Altered or unsigned legacy payloads
  are refused.

### Validation and limitations

- CI builds Release and Debug, runs the unit suite and 25 smoke tests. Six
  smoke tests are tracked known failures carried from 0.3.3.
- Release binaries are unsigned. The expected hash comes from the same GitHub
  release as the payload: it detects corruption or local tampering, not a
  compromised release. There is no automatic rollback.

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
