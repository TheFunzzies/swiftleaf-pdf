# SwiftLeaf PDF

A fast, lightweight PDF reader and editor for Windows.

[![Download](https://img.shields.io/github/v/release/TheFunzzies/swiftleaf-pdf?label=Download&style=for-the-badge)](https://github.com/TheFunzzies/swiftleaf-pdf/releases/latest)

**[Download the latest release](https://github.com/TheFunzzies/swiftleaf-pdf/releases/latest)**

| File                   | Use                                                    |
| ---------------------- | ------------------------------------------------------ |
| `Swiftleaf.exe`        | portable: download and run, nothing to install         |
| `Swiftleaf-install.exe`| installer: adds Start menu entry and file associations |

See the [changelog](CHANGELOG.md) for versioned changes and known limitations.

## Features

- **Ribbon UI** in Foxit's order: File, Home, Comment, Edit, Organize, View,
  Fill & Sign, Protect and Help, with labeled buttons, a quick access toolbar
  (Open, Save, Print, Undo, Redo) in the title bar and an icon rail on the left
  for bookmarks, pages, comments, search and favorites.
- **Home**: Hand, Select and Snapshot tools (Snapshot copies a dragged area as
  an image), zoom, fit, rotate, typewriter, highlight, Fill & Sign.
- **Comment**: highlight, underline, squiggly, strikeout, sticky notes, text
  boxes, lines, rectangles, ovals, polygons, freehand ink, stamps, file
  attachments, a comments list, undo / redo.
- **Edit**: change the existing text of a page (Edit Text: click a paragraph,
  type, Enter; the text re-flows to the paragraph's width), add text and images,
  links, compress.
- **Organize**: works on the open PDF like Foxit / Acrobat: drag thumbnails to
  reorder pages, rotate, insert blank pages or another PDF's pages, move,
  delete, split, with Undo; plus extract, merge and export pages as images.
- **Fill & Sign**: fill AcroForms; create a signature by drawing it, typing your
  name in a handwriting font or uploading a photo (the paper turns
  transparent), then place it on the page; sign with a digital ID.
- **Protect**: password protect / unprotect, redact, flatten.
- **Updates**: Help > Update (and a daily check) downloads the newest release
  from GitHub, verifies its SHA-256 and installs it.
- **Fast**: single ~13 MB executable, opens large documents instantly, also reads
  EPUB, MOBI, CBZ/CBR, XPS, DjVu, CHM and images.

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
| ------------------- | ------------------------------------------------------- |
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
[PDFCraft](https://github.com/PDFCraftTool/pdfcraft) and its editing features
by [Okular](https://invent.kde.org/graphics/okular).

GPLv3 (see `COPYING`), with parts under BSD (`COPYING.BSD`) and MuPDF under
AGPLv3. Icons are [Tabler Icons](https://tabler.io/icons) (MIT) and
[Lucide](https://lucide.dev) (ISC), the set PDFCraft uses. See `THIRD_PARTY.md`.

The tools workspace layout is inspired by [PDFCraft](https://github.com/PDFCraftTool/pdfcraft). SwiftLeaf uses Lucide icons (ISC license); no PDFCraft application code is included.
