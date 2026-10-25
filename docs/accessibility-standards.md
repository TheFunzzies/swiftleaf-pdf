# Accessibility, Standards & Preflight

SwiftLeaf PDF provides comprehensive accessibility inspection and repair, PDF/A archiving conversion, and print production preflight analysis designed for parity with Adobe Acrobat Pro's *Accessibility*, *Standards*, and *Print Production/Preflight* toolsets.

---

## 1. Accessibility Checker

The Accessibility Checker inspects PDF documents against standard universal accessibility requirements (PDF/UA, WCAG 2.1, Section 508):

- **Tagged PDF**: Verifies the presence of a marked structure tree (`/MarkInfo << /Marked true >>`).
- **Document Language**: Checks that a primary document natural language is declared (`/Root << /Lang (...) >>`), enabling screen readers to load correct pronunciation engines.
- **Title & DisplayDocTitle**: Checks that the document has a meaningful title in metadata and that `/ViewerPreferences << /DisplayDocTitle true >>` is configured so viewer titlebars show the document title instead of the filename.
- **Figure Alternate Text**: Inspects all figure structure elements and images on pages to ensure every non-decorative visual item has alternate text (`/Alt`).
- **Reading Order & Structure Tree**: Confirms the presence of `/StructTreeRoot` with valid child element nodes (`/K`).
- **Table Headers**: Checks that table structures (`/Table`) include header cells (`/TH`) rather than only data cells (`/TD`).
- **Form Field Tooltips**: Checks that interactive form fields (`/Widget`) define accessible descriptions/tooltips (`/TU`).
- **Scanned / Image-Only Pages**: Detects scanned pages containing full-page raster images without underlying OCR text streams.

### Interactive Results Panel
The Accessibility Checker panel reports each rule with:
- **Status badge**: `[PASS]`, `[WARN]`, or `[FAIL]`.
- **Category & Description**: Clear explanation of each check and identified issue.
- **Page Number**: Associated page for page-specific issues.
- **Go to Page**: Quickly navigates the document viewer directly to the flagged page.
- **Fix Issues...**: Directly launches the Accessibility Fixes dialog to resolve identified problems.

---

## 2. Accessibility Fixes & Structure Auto-Tagging

The Accessibility Fixes tool repairs common accessibility deficiencies and writes standards-compliant structure tags:

- **Document Language**: Sets or updates the document natural language (e.g., `en-US`, `de-DE`, `fr-FR`).
- **Document Title**: Sets the document title and configures `/ViewerPreferences << /DisplayDocTitle true >>`.
- **Figure Alt Text**: Lists images and figures across the document; allows specifying alternate text descriptions or marking images as decorative (`/Artifact`) so screen readers ignore them.
- **Basic Structure Auto-Tagging**: Scans text lines and font metrics across all pages and constructs a clean logical structure tree:
  - **Headings (`/H1`, `/H2`)**: Inferred from font size relative to median body text.
  - **Paragraphs (`/P`)**: Inferred from contiguous body text runs.
  - **Lists (`/L`, `/LI`)**: Inferred from bullet and numeric line prefixes.
  - **Figures (`/Figure`)**: Associated with page image resources.
  - Generates `/StructTreeRoot` and `/MarkInfo << /Marked true >>`.

---

## 3. PDF/A Standards Conversion & Validation

### Best-Effort PDF/A Conversion
Enables archiving PDF documents under ISO 19005 standards:
- **PDF/A-1b** (ISO 19005-1): Baseline conformance based on PDF 1.4. Flattens transparency groups, blend modes, and soft masks (`/SMask`).
- **PDF/A-2b** (ISO 19005-2): Based on PDF 1.7. Preserves native PDF transparency.

#### Operations Performed:
1. **Forbidden Features Stripped**:
   - Removes document and annotation JavaScript actions (`/JavaScript`, `/JS`).
   - Strips external launch actions, URI actions referencing external executables, and sound/movie actions.
   - Disables encryption dictionaries (`/Encrypt` is forbidden in PDF/A).
2. **Standard XMP Metadata**:
   - Injects standard XMP metadata packet with `pdfaid:part` (1 or 2) and `pdfaid:conformance` (`B`).
3. **Color OutputIntent**:
   - Embeds standard `sRGB IEC61966-2.1` ICC profile with `/OutputIntents` dictionary (`/GTS_PDFA1`).

> [!NOTE]
> SwiftLeaf's PDF/A converter and validator are best-effort implementations and are not certified by veraPDF or official ISO testing bodies.

### PDF/A Validation
Inspects documents for compliance with basic PDF/A rules, reporting violations including missing structure or metadata, un-embedded fonts, prohibited encryption, embedded scripts, and unflattened transparency (for PDF/A-1b).

---

## 4. Preflight & Color Conversion

### Preflight Inspector
Analyzes print and production attributes across the entire document:
- **PDF Version**: Major and minor version numbers.
- **Encryption**: Method and key length.
- **Transparency**: Identifies page transparency groups, blend modes, and alpha masks.
- **JavaScript**: Detects scripts in catalog names and annotations.
- **Color Spaces**: Catalogs DeviceRGB, DeviceCMYK, DeviceGray, Separation, and ICCBased spaces.
- **Font Embedding**: Lists every font, subtype (TrueType, Type1, CIDType2, Type0), and whether glyph programs are fully embedded or subsetted.
- **Images & Effective DPI**: Reports page location, pixel dimensions, computed horizontal and vertical DPI, color space, and bit depth.
- **Page Boxes**: MediaBox, CropBox, BleedBox, TrimBox, and ArtBox per page.

### Report Export
- **Text format (`.txt`)**: Human-readable summary suitable for print shop handoff.
- **CSV format (`.csv`)**: Tabular export for automated production pipelines.

### Color Conversion
- **Convert to Grayscale**: Converts all page elements, raster images, and vector paths to 1-component grayscale (`DeviceGray`) for black-and-white printing.
- **Convert to RGB**: Standardizes color definitions to 3-component RGB (`DeviceRGB`).

---

## 5. UI Commands & Accessibility

| Tool | Command | Ribbon Location | All Tools |
| :--- | :--- | :--- | :--- |
| Accessibility Checker | `CmdAccessibilityChecker` | Protect > Standards | Yes |
| Accessibility Fixes | `CmdAccessibilityFixes` | Protect > Standards | Yes |
| Convert to PDF/A | `CmdPdfAConvert` | Organize > Convert / Protect | Yes |
| Validate PDF/A | `CmdPdfAValidate` | Protect > Standards | Yes |
| Preflight | `CmdPreflight` | Organize > Standards | Yes |
| Convert Colors | `CmdPdfConvertColors` | Organize > Convert | Yes |
