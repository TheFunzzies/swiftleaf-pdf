# SignPath Foundation application (copy/paste for https://signpath.org/apply)

- **Project name:** Swiftleaf PDF
- **Repository:** https://github.com/TheFunzzies/swiftleaf-pdf
- **Homepage / download page:** https://github.com/TheFunzzies/swiftleaf-pdf (README describes functionality; latest release has Swiftleaf.exe and Swiftleaf-install.exe)
- **License:** GPL-3.0 (see COPYING), with bundled third-party components under their own OSI licenses (THIRD_PARTY.md). Upstream project: SumatraPDF; PDF engine MuPDF (AGPL).
- **Code signing policy page:** https://github.com/TheFunzzies/swiftleaf-pdf/blob/main/docs/code-signing-policy.md
- **Build:** GitHub Actions, `.github/workflows/build.yml`, windows-2022; release job builds from tag `v*`; checksums in SHA256SUMS.txt.
- **Description:** Free, lightweight Windows PDF reader and editor: annotation, forms, OCR to searchable PDF, merge/split, redaction, sanitize, page tools. Single 12 MB portable exe.
- **Why signing:** unsigned downloads trigger SmartScreen warnings; the in-app updater verifies installer signatures.
- **Contact / team:** (owner fills in name, email, GitHub username; MFA enabled)

Before applying: ensure repo has a release (v0.5.0 exists), MFA is on for GitHub,
and the code-signing policy page above is merged to `main`.
Note: SignPath asks for a project 'already released' and 'actively maintained' with
some reputation; a young fork may be asked to wait or decline.
