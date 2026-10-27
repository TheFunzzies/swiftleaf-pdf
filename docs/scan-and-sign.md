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
  - `Valid`: Signed bytes verify; later revisions satisfy signature locks and DocMDP permissions.
  - `ModifiedAfterSigning`: Cryptographic digest mismatch or unauthorized incremental changes detected.
  - `UnknownSigner`: Untrusted chain, unavailable verification, or unknown revocation status.
  - `Expired`: Certificate was outside its validity period at the evaluated signing time.
  - `Revoked`: Revocation check via CryptoAPI / CRL / OCSP marked the certificate as revoked.
- **Signing time**: A bound RFC 3161 token must verify and chain to a trusted TSA at its generation time before it proves signing time. Otherwise `/M` is a signer claim; absent a date, verification uses current time. Present but untrusted/invalid timestamps are labeled explicitly. Cached chain/revocation data cannot establish historical revocation proof; missing data is reported as unknown.
- **Change validation**: Signed-byte digest failures and forbidden incremental changes are separate from permitted form filling, later signatures and annotations. The panel uses MuPDF revision/field-lock validation; this is not an independent full ISO conformance check. Only the signature declaring DocMDP is labeled certified.
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
- Hashes the CMS signature value with SHA-256, requests and verifies the RFC 3161 response, and embeds its token in the CMS unsigned attribute `1.2.840.113549.1.9.16.2.14`. The signed PDF digest is unchanged.
- Reserves 64 KiB beyond the base CMS size. TSA retrieval/verification, encoding or reservation overflow fails the save visibly; no silent untimestamped fallback. Atomic save preserves existing output.
- Focused local interoperability test: `bun tests/ad-hoc-signing.ts` (OpenSSL required). It runs a loopback TSA, verifies CMS/token output with OpenSSL and checks DocMDP changes and failed TSA saves.

### Signing Providers

- **Windows Certificate Store**: Signs using user/machine certificates via CryptoAPI / CNG (`MY` Personal store).
- **PKCS#12 / PFX Files**: Supports software certificate files (`.pfx` / `.p12`) protected with a password.

### Certificate-based Encryption

- _Note:_ Certificate-based encryption (public-key envelope / CMS recipient encryption) was evaluated and skipped due to scope/binary size constraints per project architecture guidance.

---

## 4. UI Integration

- **Ribbon**:
  - **Convert Tab**: _Scan to PDF_ (`CmdScanAcquire`), _Import Photo/Camera_ (`CmdImportCameraImage`), _Batch OCR_ (`CmdBatchOcr`).
  - **E-Sign Tab**: _Digital Signatures Panel_ (`CmdSignaturePanel`), _Certify Document_ (`CmdCertifyDocument`), _Clear Signature_ (`CmdClearSignature`), _Sign Document_ (`CmdSignDocument`).
- **All Tools Panel**: Contextual panel entries for all scanning and signature tools.
- **Command Palette**: Searchable by command name or keyword.
