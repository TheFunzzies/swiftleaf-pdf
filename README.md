# SwiftLeaf PDF

A fast, lightweight PDF reader and editor for Windows.

[![Download](https://img.shields.io/github/v/release/TheFunzzies/swiftleaf-pdf?label=Download&style=for-the-badge)](https://github.com/TheFunzzies/swiftleaf-pdf/releases/latest)

**[Download the latest release](https://github.com/TheFunzzies/swiftleaf-pdf/releases/latest)**

| File                   | Use                                                    |
| ---------------------- | ------------------------------------------------------ |
| `Swiftleaf.exe`        | portable: download and run, nothing to install         |
| `Swiftleaf-install.exe`| installer: adds Start menu entry and file associations |

See the [changelog](CHANGELOG.md) for versioned changes and known limitations, and read our [comparison with PDFCraft](docs/comparison-pdfcraft.md).

## Features

- **Five workflow modes**: organized into five focused task modes:
  - **All tools**: complete tools workspace palette for quick access to all document capabilities.
  - **Read**: clean reading interface with zoom, fit, rotation, search, navigation, bookmarks, and dual/continuous page views.
  - **Edit**: modify existing page text with automatic line wrapping, add text boxes, images, links, highlight, annotations, shapes, freehand ink, and comments.
  - **Convert**: extract text, render pages to images, export, and run searchable OCR.
  - **E-Sign**: fill interactive AcroForms, create and place signatures (drawn, typed, or image upload with transparent background), and certify with digital signatures.
- **Searchable OCR**: ad-hoc and document OCR powered by native Windows OCR engines, generating selectable, searchable text layers directly in your PDF.
- **Atomic saves & recovery**: journaled atomic file writes protect against corruption during power loss or crashes, with automatic session and unsaved edit recovery.
- **Sanitize & redaction**: permanently redact sensitive text and areas with black-out or custom colors, and sanitize PDFs by stripping hidden metadata, annotations, scripts, and embedded attachments.
- **Page marks & stamps**: apply custom stamps, Bates numbering, headers, footers, and page numbers across page ranges.
- **Lossless merge & page tools**: reorder pages via thumbnail drag-and-drop, insert blank pages or pages from other PDFs without re-compressing streams, extract, rotate, and lossless merge.
- **Fast & lightweight**: single ~13 MB portable executable, instant startup, low memory footprint, and broad support for EPUB, MOBI, CBZ/CBR, XPS, DjVu, CHM, and images.
- **Updates**: in-app updater (and optional check) downloads releases from GitHub, verifies publisher signature and SHA-256 checksums, and updates safely.

## Building

Requirements: Windows 10/11, Visual Studio 2022 (or Build Tools) with the
"Desktop development with C++" workload, and [Bun](https://bun.sh) for the
code generators and helper scripts.

```powershell
powershell -File tools/swiftleaf/build.ps1            # Release x64 -> out/rel64/Swiftleaf.exe
powershell -File tools/swiftleaf/build.ps1 -Config Debug
```

Or open `vs2022/SumatraPDF.sln` in Visual Studio. After adding or removing
source files, regenerate the projects with `bin/premake5.exe vs2022`.
Every tagged release is built by CI and published on the Releases page.

## Project layout

| Path                | What                                                    |
| ------------------- | ------------------------------------------------------- |\
| `src/`              | the application                                         |
| `ext/`              | vendored libraries (MuPDF, FreeType, HarfBuzz, ...)     |
| `vs2022/`, `premake5*.lua`, `bin/` | build system and build tools            |
| `cmd/`, `tests/`    | build scripts and test suites                           |
| `tools/swiftleaf/`  | build, screenshot and sample-document scripts           |
| `docs/`             | user docs and `development.md`; old notes in `archive/` |

## Credits and license

SwiftLeaf is a fork of [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf)
by Krzysztof Kowalczyk and contributors (see `AUTHORS`; the upstream readme is
kept in `docs/upstream-readme.md`). Its interface is inspired by
[PDFCraft](https://github.com/PDFCraftTool/pdfcraft) (see [SwiftLeaf vs PDFCraft](docs/comparison-pdfcraft.md)) and its editing features
by [Okular](https://invent.kde.org/graphics/okular).

GPLv3 (see `COPYING`), with parts under BSD (`COPYING.BSD`) and MuPDF under
AGPLv3. Icons are [Tabler Icons](https://tabler.io/icons) (MIT) and
[Lucide](https://lucide.dev) (ISC), the set PDFCraft uses. See `THIRD_PARTY.md`.

The tools workspace layout is inspired by [PDFCraft](https://github.com/PDFCraftTool/pdfcraft). SwiftLeaf uses Lucide icons (ISC license); no PDFCraft application code is included.
