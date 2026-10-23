# Page marks & stamps

SwiftLeaf provides three tools to apply text overlays, watermarks, and numbering across PDF pages:

- **Add watermark**: Place custom text (e.g., `CONFIDENTIAL`, `DRAFT`) with configurable opacity, rotation angle (-180° to 180°), position (center, corners, edges), font size, and color.
- **Add header/footer**: Add text to top and bottom margins across left, center, and right alignments. Supports dynamic tokens:
  - `{page}`: Current page number
  - `{pages}`: Total page count
  - `{date}`: Current date (`YYYY-MM-DD`)
  - `{filename}`: Source document filename
- **Bates numbering**: Standard legal numbering overlay with custom prefix, starting number, digit count (zero-padded), position, font size, and color.

## Accessing the tools

The tools can be launched from:
- The top ribbon under the **Organize** tab in the **Page marks** group.
- The left **Tools panel** under Organize tools.
- Main menu under **Tools** -> **Watermark & stamps**.
- Command palette:
  - `CmdAddWatermark`
  - `CmdAddHeaderFooter`
  - `CmdBatesNumbering`

## Page ranges

Each tool accepts an optional page range specification (e.g. `all`, `1`, `1-5`, `1,3,5-8`). If left blank or set to `all`, the mark is applied to all pages in the document.

## Implementation details

Marks are drawn using PDF content stream overlays, wrapping the existing page contents in a save/restore graphic state bracket (`q` / `Q`) and appending a dedicated content stream with standard Type 1 font (Helvetica) and optional extended graphics states (`/ExtGState` dictionary for transparency). The resulting text is indexable and extractable.
