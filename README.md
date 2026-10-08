# Swiftleaf PDF

A fast, lightweight PDF reader and editor for Windows with a modern ribbon interface.

Swiftleaf is a fork of [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf)
(small, instant-start, MuPDF-based) with a Foxit / Acrobat style ribbon UI and
editing features inspired by [Okular](https://invent.kde.org/graphics/okular)
(annotation review tools, forms, signatures, page organization).

## Features

- **Ribbon UI**: Home, Comment, Edit, Organize, Forms & Sign and View pages,
  large labeled buttons, page navigation always in reach.
- **Comment**: highlight, underline, squiggly, strikeout, sticky notes, text
  boxes, lines, rectangles, ovals, polygons, freehand ink, stamps, file
  attachments, a comments list, undo / redo.
- **Edit**: change the existing text of a page (Edit Text: click a line, type,
  Enter), add text and images, links, redaction, compress, password protect.
- **Organize**: works on the open PDF like Foxit / Acrobat: rotate, insert blank
  pages or another PDF's pages, move, delete, split, with Undo; plus extract,
  merge and export pages as images.
- **Forms & Sign**: fill AcroForms, place signature images, sign with a digital ID.
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

To try a build without touching your settings:

```powershell
out/rel64/Swiftleaf.exe -for-testing docs/test/swiftleaf-sample.pdf
```

## Installing

The executable carries its own installer: run it and choose **Install**, or use
the installer published on the GitHub Releases page (built by CI).

## Project layout

| Path | What |
| --- | --- |
| `src/Ribbon.*` | Swiftleaf ribbon: controls, colors, icons, page contents |
| `src/Toolbar.cpp` | toolbar host; builds the ribbon and keeps button state current |
| `src/` | the application (SumatraPDF code base) |
| `ext/` | vendored libraries (MuPDF, FreeType, HarfBuzz, ...) |
| `tools/swiftleaf/` | build, screenshot and sample-document scripts |
| `docs/SumatraPDF-readme.md` | the upstream README |

## License

GPLv3 (see `COPYING`), with parts under BSD (`COPYING.BSD`) and MuPDF under
AGPLv3. Swiftleaf is based on SumatraPDF by Krzysztof Kowalczyk and contributors
(see `AUTHORS`). Icons are [Tabler Icons](https://tabler.io/icons) (MIT).
