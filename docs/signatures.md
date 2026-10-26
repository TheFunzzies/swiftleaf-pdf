# PDF signatures

Windows signing uses SHA-256 and detached CMS. A configured TSA is mandatory:
SwiftLeaf requests an RFC 3161 token over the signature value, verifies its
signature and imprint, and embeds it as the CMS signature-time-stamp unsigned
attribute (1.2.840.113549.1.9.16.2.14). Retrieval/verification/encoding failure
aborts saving. The placeholder reserves 64 KiB extra; larger tokens fail.
TSA retrieval can use the network and waits at most 15 seconds.

## Validation

Digest failures mean altered signed bytes. Later revisions are checked against
the preceding revision's DocMDP and FieldMDP restrictions. Permitted form
filling is reported separately from an altered signed revision. Changes that
MuPDF cannot validate, including some annotation/structure updates, remain
unverified; they do not receive a valid-document verdict. Unsaved changes are
also unverified. A certification signature does not mark other approval
signatures as certifications.

New signatures include an authenticated CMS signing time. That time is a signer
claim. A verified RFC 3161 imprint, signature, timestamp EKU and Windows TSA
chain establish an independent time, subject to the reported revocation result.
Certificate validity is evaluated at that time, or at the explicitly labelled
CMS claimed time. Current expiry alone does not invalidate a historical
signature. Without either time, historical validity is unknown; PDF `/M` is
display metadata only.

Chain/revocation checks use cached Windows evidence and embedded CMS
certificates. Missing/offline revocation evidence is unknown, never a pass.
A verified token from an untrusted TSA is identified as such. Unsupported
timestamp types are unverified. DSS/VRI evidence, historical OCSP/CRL replay,
and archival LTV renewal are not implemented; this is not an LTV verdict.

## Checks

Run `python tests/signatures-rfc3161.py --exe out/dbg64/SumatraPDF-static.exe`
with OpenSSL on PATH. This starts a temporary local TSA, checks the embedded
token with OpenSSL, and rejects a mismatched signature imprint. Its self-signed
TSA proves interoperability, not public trust.

Focused native tests also cover permitted form filling, unverified page-box
changes, altered signed bytes, corrupted timestamps, and a certificate expired
today evaluated at in-period and out-of-period historical CMS signing times.

Debug signature tests reject an unreachable TSA. Set SWIFTLEAF_TEST_TSA_URL
to exercise a local RFC 3161 responder too. This writes out/signature-test.der.
Verify independently with OpenSSL:

```sh
printf test > content.bin
openssl cms -verify -inform DER -in out/signature-test.der -content content.bin -noverify -binary -out verified.bin
openssl cms -cmsout -print -inform DER -in out/signature-test.der
```

Extract the unsigned attribute token with an ASN.1 tool, then use
`openssl ts -verify -token_in -in token.der -data signature-value.bin
-CAfile tsa-root.pem -untrusted tsa-chain.pem`. The data must be the CMS
signature OCTET STRING value, not the PDF or complete CMS envelope.
