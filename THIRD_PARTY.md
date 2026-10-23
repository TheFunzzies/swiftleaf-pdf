# Third-party assets

Vendored libraries are listed in `ext/versions.txt`. Assets copied into `src/`:

## Lucide icons

`src/LucideIcons.cpp` holds SVG path data from [Lucide](https://lucide.dev)
(`lucide-icons/lucide`), the icon set [PDFCraft](https://github.com/PDFCraftTool/pdfcraft)
uses. Only the stroke width was changed.

```
ISC License

Copyright (c) 2026 Lucide Icons and Contributors

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

The `x`, `search`, `info` and `type` icons derive from
[Feather](https://github.com/feathericons/feather) (MIT, Copyright (c)
2013-present Cole Bemis).

## PDFCraft design tokens

Colors, radii and spacing of the overhauled chrome follow PDFCraft's
`src/app/globals.css` (primary `hsl(226 71% 55%)`, border `hsl(214 32% 91%)`,
radii 6 / 12 / 16 px). PDFCraft is AGPLv3; only these values were taken, no
code or files. Its UI font is Inter with a Segoe UI fallback; SwiftLeaf uses
Segoe UI, the system font.
