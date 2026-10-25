# Page & Document Production Tools

SwiftLeaf provides a comprehensive suite of page organization, document production, and PDF utility tools built for high-throughput publishing, document preparation, and PDF/A compliance.

## Tools Overview

### 1. Crop Pages & Edit Page Boxes
- **Crop pages**: Specify margins (left, bottom, right, top in points) to crop pages.
- **Edit page boxes**: Set or adjust `MediaBox`, `CropBox`, `TrimBox`, `BleedBox`, or `ArtBox`.
- **Remove white margins**: Automatically computes content bounding boxes via Fitz display list analysis and crops excess white space.
- **Page ranges**: Apply to all pages, current page, or custom range (e.g., `1-5, 8`).

### 2. Multi-Page Layout & Imposition
- **N-Up / Grid**: Place 2, 4 (2x2), 6 (3x2), 8 (4x2), 9 (3x3), or 16 (4x4) pages onto each sheet with optional grid cell borders.
- **Booklet creation**: Imposes pages for standard 2-up saddle-stitch booklet printing (front/back sheet pairs).
- **Posterize**: Tiles large pages across an $N \times M$ grid of smaller sheets with configurable overlap margin for print assembly.
- **Divide pages**: Splits each page into horizontal or vertical halves (e.g. splitting two-page spreads into single pages).
- **Combine into long page**: Stacks all pages vertically into a single tall continuous page ideal for web and mobile scrolling.

### 3. Page Background Color & Image
- **Solid background**: Apply solid background color (e.g. eye-friendly soft cream `#FDF6E3` or dark theme) underneath existing page content.
- **Image background**: Underlay a watermark or background image across selected pages.
- **Opacity**: Configurable background opacity (0.0 to 1.0).

### 4. Page Ordering & Cleanup
- **Duplicate pages**: Duplicates selected pages in-place.
- **Reverse page order**: Reverses the sequence of document pages.
- **Insert blank pages**: Inserts blank pages before or after specified positions.
- **Remove blank pages**: Automatically detects blank pages (analyzing text content, display list bounds, and pixel luminance) and strips them.
- **Deskew pages**: Scans pages for rotational skew angles using Radon transform detection; straightens skewed pages losslessly.

### 5. Document Properties Editor
- View and edit standard PDF metadata in the `/Info` dictionary:
  - **Title**
  - **Author**
  - **Subject**
  - **Keywords**
- Synchronizes with standard XMP metadata packets (`/Root /Metadata`).

### 6. Embedded Attachments Manager
- Inspect embedded files in the PDF NameTree (`/Root /Names /EmbeddedFiles`).
- **Add attachment**: Embed files directly into the PDF.
- **Extract attachment**: Extract embedded files to disk.
- **Delete attachment**: Remove embedded files from the document.

### 7. Bookmarks & Outline Editor
- Manage document outline tree (`/Root /Outlines`).
- **Add / Edit**: Modify outline titles, hierarchical levels, and page destinations.
- **Auto-generate from headings**: Scans pages for large font size text and automatically constructs a clean Table of Contents tree.

### 8. Custom Page Numbering & Labels
- Set custom numbering ranges in `/Root /PageLabels`:
  - Styles: Decimal Arabic (`1, 2, 3`), Uppercase Roman (`I, II, III`), Lowercase Roman (`i, ii, iii`), Uppercase Alpha (`A, B, C`), Lowercase Alpha (`a, b, c`).
  - Prefix: Optional range prefix (e.g., `Section 1-`, `Appendix-`).
  - Starting number and page offset.

### 9. PDF Optimization & Conversion
- **Linearize (Fast Web View)**: Rewrites PDF with primary hint tables for instant page-at-a-time streaming over the web.
- **Repair PDF**: Reconstructs damaged cross-reference tables (xref) and unreadable object streams into a sanitized, valid PDF.
- **Rasterize document**: Converts vector pages into flattened high-resolution bitmap pages at configurable DPI (72–600 DPI).
- **Convert to grayscale**: Recolors all document page streams and images into single-channel DeviceGray colorspace.
- **Invert document colors**: Inverts page colors using Difference blend mode overlay while preserving text selection and vector quality.
- **Remove annotations**: Strips all non-link comments, stamps, and highlights.
- **Remove all links**: Removes all link annotations and clickable URI actions.

### 10. Document Layers (OCG)
- Manage Optional Content Groups (`/OCProperties`).
- Toggle layer visibility ON / OFF.
- **Flatten layers**: Merges active layers into permanent page content and removes layer metadata.

## Accessing Tools

All page production tools can be launched from:
- Top **Ribbon** under **Organize**, **Edit**, or **Protect** tabs.
- Left **Tools panel** under All tools.
- Command palette (`Ctrl + Shift + P` or `F1`).

## Atomic Saving & Crash Safety

All page and document tools write their output to a temporary staging file beside the target destination. The file is flushed and committed using atomic replacement (`MoveFileEx` on Windows). Original files are never modified until the new PDF is successfully serialized and verified.
