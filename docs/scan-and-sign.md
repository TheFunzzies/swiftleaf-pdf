# Scan & OCR and Digital Signatures (Parity with Adobe Acrobat)

SwiftLeaf PDF provides comprehensive scanner document acquisition, camera image capture, OCR preprocessing, and cryptographic digital signature workflows with parity to Adobe Acrobat's **Scan & OCR** and **Certificates / Signatures** toolsets.

---

## 1. Scanner Acquisition & Camera Import (WIA & Image Prep)

### Scanner Acquisition (WIA COM)
- **Device Enumeration & Selection**: Connects via Windows Image Acquisition (`CLSID_WiaDevMgr`). If multiple scanners are connected, users can choose their target hardware or default scanner.
- **Graceful Degradation**: If no WIA scanner or scanning driver is detected, SwiftLeaf presents a clear diagnostic message offering the user to import a camera/photo image instead (`CmdImportCameraImage`).
- **Scan Settings**:
  - **Resolution**: Selectable DPI presets (150 DPI for drafts/emails, 300 DPI for standard OCR, 600 DPI for archival high-resolution).
  - **Color Mode**: Full Color (24-bit RGB), Grayscale (8-bit), and Black & White thresholding (1-bit line art).
  - **Input Source**: Flatbed platen, Automatic Document Feeder (ADF), or Duplex (both sides).
  - **Multi-page Acquisition**: Scans successive pages until the feeder is empty or user completes scanning, assembling pages into a single PDF document.
  - **Integrated OCR**: Prompts to run optical character recognition (`src/Ocr.cpp`) upon acquisition to produce searchable, selectable PDF output immediately.

### Camera & Mobile Photo Image Import
- **Perspective Correction**: Automatic 4-corner document boundary detection and quad perspective transformation matrix mapping skewed or angled photos into an upright, rectangular page.
- **Image Enhancement**:
  - Original Color: Preserves captured colors.
  - Grayscale: Removes color noise and chromatic aberration.
  - Enhanced Contrast: Linear stretching and histogram normalization to maximize text contrast.
  - Black & White Threshold: Otsu-inspired dynamic binarization yielding clean document backgrounds.
- **Auto-Deskew**: Hough / projection-based skew detection and rotation alignment (±15° range) to straighten text lines.

---

## 2. OCR Preprocessing & Batch OCR

### Image Preparation Pipeline (`OcrPrep`)
Before invoking the Windows OCR engine (`Windows.Media.Ocr`), SwiftLeaf can run preprocessing on input page rasterizations:
1. **Deskew**: Detects dominant text line orientation angles and applies bilinear rotation correction.
2. **Despeckle**: Median filtering to eliminate scanner dust, salt-and-pepper noise, and dark scanning artifacts.
3. **Border Removal**: Identifies dark scanner edges and shadow borders around document margins and suppresses them to white.

### Batch OCR API
- `BatchOcrPdf(const char* inputPdfPath, const char* outputPdfPath, const char* langTag, bool preprocess)`: Processes an entire multi-page PDF in batch, rendering each page at 300 DPI, running preprocessing, recognizing text, and inserting invisible searchable text layers into the output PDF.
- `ShowBatchOcrDialog(MainWindow* win)`: User-facing interactive dialog to run batch OCR across documents with progress updates.

---

## 3. Digital Signatures & Certificates (Certificates / Signatures)

### Digital Signature Panel (`CmdSignaturePanel`)
Lists all PKCS#7 / CMS signatures embedded in the document with real-time verification and inspection:
- **Integrity Status**:
  - `Valid`: Signed bytes verify; later revisions pass MuPDF signature-permission checks. Read the time and revocation qualifications in the status description.
  - `ModifiedAfterSigning`: Cryptographic digest mismatch or unauthorized incremental changes detected.
  - `UnknownSigner`: Untrusted root or self-signed certificate.
  - `Expired`: Certificate was outside its validity period at the evaluated time (trusted TSA time, authenticated signer-claimed time, or current-time fallback).
  - `Revoked`: Revocation check via CryptoAPI / CRL / OCSP marked the certificate as revoked.
- **Signer Details**: Common Name (CN), Issuer CN, signing timestamp (or RFC 3161 TSA timestamp), Reason, Location, Field name, and page number.
- **Certificate Viewer**: Integrates directly with Windows `CryptUIDlgViewContext` (`cryptui.dll`) allowing users to inspect the full X.509 certificate hierarchy, thumbprint, key usage, and validity parameters.
- **Clear Signature**: Clears signature contents and AP dictionary, resetting the form field to unsigned state.

### Document Certification (`CmdCertifyDocument` / DocMDP)
Allows document owners to certify the PDF with DocMDP permissions (`TransformMethod /DocMDP`):
- **Permission 1 (P=1)**: No changes permitted (strict lock down).
- **Permission 2 (P=2)**: Form fill-in and subsequent digital signatures permitted.
- **Permission 3 (P=3)**: Annotations, form fill-in, and digital signatures permitted.

### RFC 3161 Timestamp Authority (TSA)
- Configurable via `TsaUrl` in Settings.
- Requests SHA-256 over the CMS signature value, verifies the response signature, and embeds its token as the RFC 3161 unsigned attribute (`1.2.840.113549.1.9.16.2.14`). Reserves 32 KiB for the token; oversized responses fail saving.
- A requested TSA failure aborts saving; it never silently creates an untimestamped signed output.
- Validation checks the token signature and message imprint, then the TSA chain, timestamping EKU, time validity and revocation. Merely finding timestamp metadata does not establish trusted time.
- Without a trusted TSA, authenticated CMS signingTime remains a signer claim. A PDF `/M` date is display metadata, not evidence of signing time.
- Signer chains are evaluated at the selected time. Revocation unknown/offline is reported explicitly. Online checks do not establish historical revocation validity; DSS/VRI/archived OCSP/CRL LTV validation is not implemented.
- Incremental revisions are checked against DocMDP/FieldMDP permissions, separately from the signed-byte digest. An approval signature does not certify all later content. Permission validation uses MuPDF's conservative object comparison; this is not a claim of full Acrobat/LTV parity.

Run `bun cmd/build.ts -dbg`, then `powershell -File tests/signature-crypto.ps1` for generated CMS signatures, TSA failure/success, OpenSSL verification when installed, and DocMDP revision tests. The TSA success test uses a public endpoint by default; `-Tsa` selects another endpoint.

### Signing Providers
- **Windows Certificate Store**: Signs using user/machine certificates via CryptoAPI / CNG (`MY` Personal store).
- **PKCS#12 / PFX Files**: Supports software certificate files (`.pfx` / `.p12`) protected with a password.

### Certificate-based Encryption
- *Note:* Certificate-based encryption (public-key envelope / CMS recipient encryption) was evaluated and skipped due to scope/binary size constraints per project architecture guidance.

---

## 4. UI Integration

- **Ribbon**:
  - **Convert Tab**: *Scan to PDF* (`CmdScanAcquire`), *Import Photo/Camera* (`CmdImportCameraImage`), *Batch OCR* (`CmdBatchOcr`).
  - **E-Sign Tab**: *Digital Signatures Panel* (`CmdSignaturePanel`), *Certify Document* (`CmdCertifyDocument`), *Clear Signature* (`CmdClearSignature`), *Sign Document* (`CmdSignDocument`).
- **All Tools Panel**: Contextual panel entries for all scanning and signature tools.
- **Command Palette**: Searchable by command name or keyword.
