# Review and Measurement Tools

SwiftLeaf PDF provides review, annotation, and measurement tools with parity to PDFCraft and Adobe Acrobat.

## Comments Import & Export

### Formats
- **XFDF (XML Forms Data Format)**: Standard ISO 19444-1 XML interchange format for annotations and comments across PDF editors.
- **FDF (Forms Data Format)**: Adobe PDF syntax format for comments, form fields, and annotation data.
- **Comment Summary**: Export comprehensive summaries of all comments grouped by page to:
  - **PDF (`.pdf`)**: A cleanly formatted report document.
  - **Text (`.txt`)**: Hierarchical text outline with dates, authors, and status.
  - **CSV (`.csv`)**: Structured table with columns `Page,Type,Author,Date,Status,StateModel,InReplyTo,Content`.
- **Flatten Comments**: Bakes annotations and comments directly into the page content streams (`pdf_bake_document`), permanently applying visual appearances and removing interactive annotation elements.

## Comment Status & Review Workflow

Annotations support standard review states persisted in PDF dictionaries according to ISO 32000-1:
- **Review States**: `/StateModel (Review)` with states:
  - `Accepted`
  - `Rejected`
  - `Completed`
  - `Cancelled`
- **Marked States**: `/StateModel (Marked)` with states:
  - `Marked`
  - `Unmarked`
- **Threaded Replies**: Create child comments referencing the parent annotation via `/IRT` (In Reply To) and `/RT /R` (Review reply).
- **UI Access**: Status and reply controls are available in the floating annotation edit toolbar chip list, the comments filter sidebar, and the canvas right-click context menu.

## Measurement Tools

- **Page Scale Calibration**:
  - Calibrate measurement ratio per page or document (`1 in = 10 ft`, `1 mm = 1 m`, custom ratio, points per unit).
  - Set via `CmdMeasureSetScale`.
- **Distance**:
  - Measures Euclidean distance between two endpoints.
  - Shows live distance label during cursor movement.
  - Saved as `/Subtype /Line` with `/IT /LineDimension` and `/Measure` dictionary.
- **Perimeter**:
  - Measures cumulative polyline length across multiple vertices.
  - Shows live perimeter label during placement.
  - Saved as `/Subtype /PolyLine` with `/IT /PolyLineDimension` and `/Measure` dictionary.
- **Area**:
  - Measures enclosed polygon area using the shoelace algorithm.
  - Shows live area label during placement.
  - Saved as `/Subtype /Polygon` with `/IT /PolygonDimension` and `/Measure` dictionary.
- **Snapping**:
  - Automatic snapping to line endpoints, polygon/polyline vertices, and rectangle corners within an 8 pt radius.
- **CSV Export**:
  - Export all measurement annotations to CSV (`Page,Type,Measurement,Unit,Scale,Author,Date,Label`).

## Annotation Tools

The full annotation toolbar and Edit tab include:
- **Callout**: Free text annotation with leader line (`/IT /FreeTextCallout`).
- **Arrow**: Line annotation with directional arrowhead (`/LE [ /None /OpenArrow ]` or `/ClosedArrow`).
- **Text Box**: Free text annotation with border (`/Subtype /FreeText`).
- **Replace Text**: Text replacement on selection with strikeout and caret insertion.
- **Polygon & Polyline**: Multi-vertex vector paths and shapes.
- **Squiggly & Caret**: Proofreading and markup annotations.
