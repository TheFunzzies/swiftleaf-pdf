# Scanning, Camera Import and Signatures

## 1. Scanner Acquisition (Scan to PDF / `CmdScanAcquire`)

SwiftLeaf communicates with WIA 2.0 flatbed and feeder scanners. Missing scanners,
missing paper, jams and driver errors fail visibly with the HRESULT. Empty or
invalid transfers never produce a substitute blank page. Real blank paper is
preserved, including blank duplex backs.

WIA 2.0 selects the source by item category. Feeder mode writes `FRONT_ONLY` or
`DUPLEX`, without the obsolete WIA 1.0 `FEEDER` flag that strict drivers reject.
See [document handling modes](https://learn.microsoft.com/en-us/windows-hardware/drivers/image/wia-ips-document-handling-select).

Acquisition runs on a worker. Cancel requests cancellation through the WIA
callback; response time depends on the driver. Closing while scanning requests
cancellation and keeps the dialog alive until the worker stops. A PDF is published
only when all pages transfer and write successfully; partial or failed scans leave
any destination file untouched.

WIA property failures report the property and status rather than continuing with
unverified settings. Only flatbed and feeder sources are offered. Scanner settings
persist in `AppSettings.Scanner`.

## 2. Photo / Camera Import (`CmdImportCameraImage`)

Perspective correction is optional and off by default. Enter four pixel coordinates
as `x,y`, clockwise: top left, top right, bottom right, bottom left. Coordinates use
the decoded image, starting at `(0,0)`; the defaults cover the full image. The
quadrilateral must be convex and stay inside the image. Invalid corners fail
without silently falling back to a different crop.

Correction uses a projective homography with bilinear pixel sampling. It does not
detect document edges automatically. There is no interactive corner preview yet;
use an image viewer to determine coordinates. Grayscale, linear contrast and global
Otsu threshold are available; this is not adaptive thresholding. Optional deskew is
off by default because rotation can clip edge content. Output is published atomically.

## OCR coordinates

OCR preprocessing records the actual raster transform. Its inverse maps all four
word corners back to the original raster. PDF insertion retains the transformed
word axes, then undoes view rotation, zoom, crop origin and PDF UserUnit. Both
interactive and batch OCR use this path. Batch rendering, recognition or insertion
failure aborts output instead of reporting a partially searchable file as success.

Regression tests save/reopen searchable PDFs with crop offsets, UserUnit, all page
and view quarter-turns and positive/negative deskew, verifying selection and redaction match.
Pixel tests cover identity camera corners, a trapezoid and crossed corners.

Run `bun test tests/scan-geometry.test.ts` for source guards and
`bun tests/ad-hoc-scan-geometry.ts --no-build` after a debug build for native tests.

## 3. Digital Signatures & Certificates (Certificates / Signatures)

### Digital Signature Panel (`CmdSignaturePanel`)
Lists all PKCS#7 / CMS signatures embedded in the document with real-time verification and inspection:
- **Integrity Status**:
  - `Valid`: Cryptographically sound; document unaltered since signed.
  - `ModifiedAfterSigning`: Cryptographic digest mismatch or unauthorized incremental changes detected.
  - `UnknownSigner`: Untrusted root or self-signed certificate.
  - `Expired`: Signing certificate expired before signature verification.
  - `NotSigned`: Empty signature field.
- **Certificate Properties**: Subject, Issuer, Validity Dates, SHA-1/SHA-256 Fingerprint, Key Usage.
- **Document Restrictions**: DocMDP / certification status (no changes allowed, forms only, forms & annotations).
- **Clear Signature**: Clears signature contents and AP dictionary, resetting the form field to unsigned state.

### Document Certification (`CmdCertifyDocument` / DocMDP)
Allows document owners to certify the PDF with DocMDP permissions (`TransformMethod /DocMDP`):
- **Permission 1 (P=1)**: No changes permitted (strict lock down).
- **Permission 2 (P=2)**: Form fill-in and subsequent digital signatures permitted.
- **Permission 3 (P=3)**: Annotations, form fill-in, and digital signatures permitted.

### RFC 3161 Timestamp Authority (TSA)
- Configurable via `TsaUrl` in Settings.
- When configured, queries the RFC 3161 timestamp authority server via `CryptRetrieveTimeStamp` (CryptoAPI) and embeds the authenticated timestamp token in the signature `signer_info` unauthenticated attributes.

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
