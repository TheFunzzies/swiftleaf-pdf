# Changelog

## Unreleased

- PDF compression honesty: distinct Archive preset from ISO PDF/A, separate Print and Archive backends, detailed image/font/object byte contributions in audit and dialog, honest preset descriptions, and keep-original guarantee when output would grow.
- Release size and footprint gate: automated CI check against recorded baselines with warning on growth and strict >10% growth prevention gate.
- Accessibility: warn about unverified reading order and figure links; preserve existing tags and mark generated roles as drafts.
- PDF/A: reject invalid ICC profiles, preserve transparency, and report unverified conformance.
- PDF compare: report load, render, and text extraction failures honestly without false 0% diffs, include extra-page details, and ensure atomic report writes.
- PDF compare: report separate word edits with a bounded diff and coarse fallback, and align inserted, removed or moved pages by text similarity instead of page index.
- Tools search: enable Tab navigation and prevent minimum-width clipping on resize.
- Recommended section at top of the All tools panel for quick access to primary workflows.
- Grouped All tools into collapsible categories with search by name/description, persisted favorites, and tooltips on every tool row.
- Command palette lists all commands with description and ribbon tab destination.
- Small-window layout: at <=800px width the ribbon collapses with an overflow chevron menu.
- Start page layout: prevent text clipping on narrow windows and 800x600/1024x768 virtual desktops.
- First-run dismissible Quick tour card on the start page, persisted in settings (QuickTourDismissed).

### Added

- Batch plans are validated before the first write: empty or unknown actions,
  invalid optimizer presets, exports that are not last, duplicate outputs and
  source overwrites are rejected. The Action Wizard previews the actions and
  each input's final output before running, and intermediate cleanup never
  deletes an input file. See `docs/batch-compare.md`.

- Edit Images and Objects replaces page images and scales, rotates or flips
  named images and grouped objects in a separate copy. Shared resources on
  other pages remain unchanged; failed operations preserve existing output.

- Font Manager lists document fonts and changes the installed font family on
  selected text lines. Find and Replace Text replaces literal matches across
  pages in an atomic copy, including unsaved edits. Saved Unicode CID subsets
  can be edited again. See `docs/text-editing.md` for supported styles and limits.

- Batch processing and PDF comparison with ordered JSON actions, OCR,
  optimizer presets, sanitization, redaction, metadata and text exports;
  recursive PDF inputs; JSON CLI results; and HTML comparison reports. See
  `docs/batch-compare.md` and `docs/cli.md`.

- All tools categories with name and description search, persisted favorites, and short tooltips.
- Existing-text editing: Unicode Latin, Cyrillic, Greek and CJK with embedded,
  subset CID fonts, explicit ToUnicode maps, installed-family fallback and
  character-aware wrapping. Unsupported glyphs and block styles are rejected
  before source removal; failed edits keep the draft. See `docs/text-editing.md`
  for remaining shaping, style and object-editing limits.
- Existing-text editing keeps CJK IME candidate keys inside composition and
  refuses edits overlapping links, annotations or form fields. Added editor
  save/reopen and failed-draft smoke coverage.

- Accessibility & Standards Tools (TASK W2-STANDARDS):
  - Accessibility checker: comprehensive inspection for tagged structure (`/MarkInfo /Marked`), document language (`/Lang`), title and window title display (`/ViewerPreferences /DisplayDocTitle`), figure alternate text (`/Alt`), reading order structure tree (`/StructTreeRoot`), table headers (`/TH`), form field tooltips (`/TU`), and scanned image-only pages, with interactive dialog reporting pass/warn/fail status and direct "Go to Page" navigation.
  - Accessibility fixes & basic structure auto-tagging: configure language and title, add/edit figure alternate text or mark as decorative (`/Artifact`), and auto-tag headings (`/H1`, `/H2`), paragraphs (`/P`), lists (`/L`), and figures from layout and font size metrics.
  - PDF/A conversion & validation: best-effort conversion to PDF/A-1b (flattens transparency) and PDF/A-2b (preserves transparency) with XMP `pdfaid` packet, OutputIntent with sRGB ICC profile, stripping forbidden features (JS, encryption, external references); built-in validator with honest best-effort disclaimer.
  - Preflight & color conversion: document preflight reporting fonts (embedded/subset), color spaces, image dimensions & effective DPI, page boxes (Media, Crop, Bleed, Trim, Art), transparency, JS, encryption, and PDF version; text and CSV report export; convert document colors to Grayscale or RGB.
  - Registered in Ribbon (Protect, Organize, Convert), All tools panel, and Command palette.
  - Documentation in `docs/accessibility-standards.md` and roundtrip unit tests in `src/tests/Sumatra_ut.cpp`.

- Review & Measurement tools (TASK W-REVIEW):
  - Comments import and export in XFDF (ISO 19444-1) and FDF formats.
  - Comment summary generation to PDF, text outline, and CSV tabular formats.
  - Flatten comments baking annotations directly into page content streams.
  - Comment review status workflow: Accepted, Rejected, Completed, Cancelled, Marked persisted in /State and /StateModel dictionaries per PDF specification.
  - Threaded comment replies referencing parent annotations via /IRT.
  - Measurement tools: page scale calibration, distance, perimeter, and area measurement with live on-screen labels.
  - Vertex and endpoint snapping to existing annotations and page content.
  - Measurement persistence in PDF /Measure dictionary and CSV export.
  - Callout, Arrow, Text Box, Replace Text, Polygon, Polyline, Squiggly, and Caret annotation tools registered in Ribbon, Edit tab, All tools, and context menus.
  - Documentation in docs/review-tools.md and roundtrip unit tests in src/tests/Sumatra_ut.cpp.

- Scan & OCR and Digital Signatures (TASK W2-SCAN-SIGN):
  - Scanner acquisition via Windows Image Acquisition (WIA COM) with device selection, resolution presets (150/300/600 DPI), color modes (RGB, Grayscale, B&W), and source selection (flatbed, feeder, duplex) to multi-page PDF with immediate OCR prompt.
  - Camera and mobile photo import with 4-corner perspective crop, grayscale/contrast/threshold image enhancements, and auto-deskewing.
  - OCR preprocessing pipeline: automated deskewing, despeckling, and scanner edge/border suppression; exposed multi-page batch OCR API and interactive dialog.
  - Digital signatures panel: real-time cryptographic validation (integrity ok, modified after signing, unknown signer, expired, revoked), signer metadata, and native Windows certificate viewer (CryptUIDlgViewContext).
  - Document certification (DocMDP) with customizable author permissions (P=1 strict lock, P=2 form fill-in and signing, P=3 annotations and forms).
  - RFC 3161 cryptographic timestamping via configurable TSA server URL setting (TsaUrl).
  - Clear signature functionality to reset signed form fields back to unsigned state.
  - Registered commands in Ribbon ('Convert' and 'E-Sign' tabs), All Tools panel, and Command Palette.
  - Unit tests in Sumatra_ut.cpp verifying signature creation, verification, DocMDP checks, tampering detection, and clearing.
  - Documentation in docs/scan-and-sign.md.

- Export PDF pages to DOCX, PPTX, XLSX, HTML, RTF, Markdown, spaced text, JSON and per-page SVG ZIP with page ranges and a limitations note.
- Create or combine PDFs from Office files, text, RTF, HTML, Markdown, images and clipboard images. Office uses Microsoft Office automation or LibreOffice when available.
- Advanced AcroForm tools: field detection with candidate preview, list boxes,
  reset/link/submit-action buttons, image and date fields, expanded field
  properties, tab-order editing, FDF/XFDF/CSV/JSON data exchange, Clear Form
  and flatten-to-copy. Submit actions are for other readers; calculations,
  validation ranges and input masks require JavaScript and are not authored.
  See `docs/form-authoring.md` for compatibility limits.
- Comprehensive page and document production tools (PageTools2):
  - Crop pages and edit page boxes (Media, Crop, Trim, Bleed, Art) with numeric dialog and automatic white margin removal.
  - Multi-page imposition: N-up / grid (2, 4, 6, 8, 9, 16 pages per sheet), saddle-stitch booklet creation, posterize tiling with overlap margins, horizontal/vertical page division, and combine pages into a continuous tall page.
  - Page background color (custom RGB) and image underlay with opacity support.
  - Page ordering and cleanup: duplicate pages, reverse page order, insert blank pages, automated blank page detection and removal, lossless skew detection and page deskewing.
  - Document properties editor: view and edit Title, Author, Subject, Keywords, and XMP metadata packets.
  - Embedded attachments manager: inspect, embed, extract, and remove document file attachments.
  - Bookmark / outline editor: create, edit, reorder outlines, and auto-generate bookmarks from document headings.
  - Custom page numbering and page labels: configure decimal, Roman, and alphabetic ranges with custom prefixes and starting numbers.
  - PDF optimization and repair: Linearize for fast web view, repair damaged cross-reference tables, rasterize pages at custom DPI, convert to grayscale, invert colors losslessly, remove annotations, and remove links.
  - Document layers (OCG) panel: list layers, toggle visibility, and flatten layers permanently into page content.
  - Registered all tools under Ribbon tabs (Organize, Edit, Protect), All tools panel, and Command palette.
  - Unit tests in `Sumatra_ut.cpp` verifying page transformations, text extraction, properties, attachments, bookmarks, labels, and recovery.
  - Documentation in `docs/page-tools.md`.

## 0.5.0 - 2026-10-24

### Added

- Sanitize Document with selectable metadata, hidden text/layers, JavaScript,
  attachments, comments, form data and thumbnail removal. Search & Redact removes
  all literal or regex matches from saved page content. Both save atomic copies
  and are available in Protect, All tools and Tools. See `docs/document-privacy.md`.

- Searchable OCR PDF copies with aligned invisible Unicode text, installed
  Windows OCR language selection, and current/all/selected page ranges.
  Recognition and saving run in the background with progress and cancellation.
  Saved copies preserve the original page artwork and unselected pages.

- Unsaved PDF edits get recovery copies about once a minute. After a crash,
  Restore saves a separate PDF; Discard removes the recovery copy.

- Three new page stamping tools in All tools, Organize tab, and Tools menu:
  - **Add watermark**: Text watermark with customizable opacity, rotation angle, font size, position/centered, color, and page range.
  - **Add header/footer**: Header and footer text across left, center, right alignments with dynamic tokens {page}, {pages}, {date}, {filename} and page range.
  - **Bates numbering**: Standard legal Bates numbering with customizable prefix, starting number, digit count, position, and page range.
- Page stamps unit tests asserting text extraction on the right pages.
- Documentation for page marks and stamps in docs/page-stamps.md.

### Fixed

- Merge and Insert File preserve imported forms with unique names, comments,
  remapped internal links, bookmarks and attachments. Element-specific fallbacks
  are reported instead of flattening every imported page.

- PDF saves, page replacement, conversion, merge, extraction, compression and
  encryption stage a complete file beside the destination, flush it, then
  replace the destination atomically. Failed writes leave the original intact.
- Recovery and tool snapshots preserve pending edits for subsequent saves.

### Limitations

- Page organizing keeps its separate Undo Page Change history; it is not
  interleaved with annotation/form undo and has no redo. See `docs/recovery.md`.

### Security

- Releases publish SHA-256 checksums; the updater refuses downloads whose
  checksum is missing or does not match. Authenticode signing is available
  through repository secrets.

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
