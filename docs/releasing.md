# Releasing SwiftLeaf

Push a `v*` tag to build and publish the Windows release. The workflow publishes
`Swiftleaf.exe`, `Swiftleaf-install.exe`, `update-check.txt`, and
`SHA256SUMS.txt`. The updater requires the installer SHA-256 value from
`update-check.txt` to match before it launches the installer.

## Optional Authenticode signing

The release workflow signs both executables when both signing secrets are set.
Without them, the workflow publishes unsigned executables and the SHA-256
checks remain enabled.

1. Obtain a valid Authenticode code-signing certificate as a PFX file.
2. Base64-encode the PFX without line breaks. In PowerShell:

   ```powershell
   [Convert]::ToBase64String([IO.File]::ReadAllBytes('signing.pfx'))
   ```

3. In the GitHub repository, add Actions secrets named
   `SIGN_CERT_PFX_BASE64` (the encoded PFX) and `SIGN_CERT_PASSWORD` (its
   password). Do not commit the certificate or paste either value into source
   control.

The workflow signs with SHA-256 and timestamps the signature. Protect and renew
the certificate according to its issuer's requirements.
