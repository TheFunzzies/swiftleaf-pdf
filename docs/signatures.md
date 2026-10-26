# PDF signatures

Windows signing uses SHA-256 and detached CMS. A configured TSA is mandatory:
SwiftLeaf requests an RFC 3161 token over the signature value, verifies its
signature and imprint, and embeds it as the CMS signature-time-stamp unsigned
attribute (1.2.840.113549.1.9.16.2.14). Retrieval/verification/encoding failure
aborts saving. The placeholder reserves 64 KiB extra; larger tokens fail.
TSA retrieval can use the network and waits at most 15 seconds.

## Checks

Run `python tests/signatures-rfc3161.py --exe out/dbg64/SumatraPDF-static.exe`
with OpenSSL on PATH. This starts a temporary local TSA, checks the embedded
token with OpenSSL, and rejects a mismatched signature imprint. Its self-signed
TSA proves interoperability, not public trust.

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
