# Code signing policy

Free code signing for Swiftleaf releases is provided by
[SignPath.io](https://signpath.io), with a certificate from the
[SignPath Foundation](https://signpath.org). (Applies once the application is
accepted; until then releases are unsigned and verified by the published
`SHA256SUMS.txt`.)

## What is signed

Only binaries built by this project's GitHub Actions workflow
(`.github/workflows/build.yml`) from the source in
<https://github.com/TheFunzzies/swiftleaf-pdf>: `Swiftleaf.exe` and
`Swiftleaf-install.exe`. Third-party libraries are included only as source and
compiled into those binaries; no pre-signed third-party binaries are repackaged.

## Roles

- **Author / committer / reviewer / approver:** TheFunzzies (repository owner).
  Contributors submit pull requests; only the owner merges to `main` and pushes
  release tags. Signing requests are approved manually by the owner.
- All team members use multi-factor authentication on GitHub and SignPath.

## Privacy

Swiftleaf does not collect or transmit personal data. The only network
request it makes on its own is the optional update check, which downloads
`update-check.txt` from this project's GitHub releases. No document contents,
file names or identifiers are sent. See `docs/rename-notes.md` for the few
legacy upstream endpoints that remain and how they are handled.
