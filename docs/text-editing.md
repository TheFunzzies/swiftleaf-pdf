# Editing existing text

Choose **Edit Text**, then click a paragraph or use an existing text selection.
Enter commits; Shift+Enter inserts a paragraph break; Escape cancels. A failed
commit keeps the draft open for correction or copying. Save and Save As use the
atomic sibling-file replacement described in [recovery.md](recovery.md).

## Unicode and fonts

Existing-text replacement accepts Latin (including precomposed accents), Greek,
Cyrillic, Chinese, Japanese and Korean text. Each character must have a glyph in
an embeddable font. Supported supplementary characters use UTF-16 surrogate pairs
in the PDF's ToUnicode map. The engine checks the entire draft before removing
source text; it never substitutes a question mark for an unavailable glyph.

The original font is reused when it can be embedded and contains the character.
For missing characters the engine tries the original family through the Windows
font loader, then a matching Base-14 substitute and MuPDF's Unicode fallback
fonts. Subset prefixes are removed for installed-font lookup. Font substitution
can change glyph shapes and widths. Fonts are embedded as CID Type0 fonts with
explicit Encoding and ToUnicode maps, including distinct mappings for characters
that share a glyph. Font subsetting runs in a temporary PDF containing only the
replacement; the result is imported as an isolated Form XObject. MuPDF can leave
a font whole if its subsetter cannot handle that font program.

The editor preserves font size, fill color and the first baseline. Paragraph
replacement wraps within the original width, preferring spaces; CJK and long
words can wrap between characters. Additional lines can extend below the old
block and overlap following content. Replacement glyphs extending beyond the
page are rejected before source removal. Inspect the result and undo if needed.

## Deliberate limits in W2

This is a partial implementation, not Acrobat editing parity:

- Mixed fonts, sizes or colors within a selected block are rejected. The editor
  does not yet preserve multiple styled runs or offer rich-text controls.
- Rotated, vertical, transparent, stroked, clipped and invisible source text is
  unsupported. Blocks with character positioning differing from the source font's
  advances by more than 0.25 point are rejected. This includes many kerned,
  tracked, horizontally scaled or manually positioned blocks. Supported edits use
  the chosen font's advances.
- RTL, joining scripts, combining marks, variation selectors and multi-character
  emoji sequences are rejected. HarfBuzz shaping and bidi editing are not wired
  into this path. Use precomposed Latin characters where available.
- Blocks overlapping annotations are rejected to prevent redaction from removing
  those annotations. Text removal still uses geometric text-only redaction;
  original content-stream operators are not edited in place. Images and vector
  artwork are excluded from that redaction.
- Existing-image/vector selection, replacement, move, resize, crop, rotation,
  flip, multi-selection, align and distribute are not implemented by this change.
- A font manager and document-wide find/replace are not implemented.
- Add Text, Add Image, watermarks, headers/footers and Bates numbering retain
  their existing paths; this change does not extend their Unicode behavior.

## Verification

`TextEditUnicodeTest` in `src/tests/Sumatra_ut.cpp` replaces source text, saves,
reopens and checks exact extracted text for Latin, Cyrillic, Greek and CJK. It
also checks embedded subset Type0 fonts, ToUnicode streams, undo/redo and CJK
reflow, and confirms unsupported text and off-page edits leave the source
unchanged. `tests/edit-safety.cpp` checks strict UTF-8
validation and unsupported shaping/control characters before document access.

Run the Debug build and `bun cmd/run-unit-tests.ts -dbg --no-build`. On a host
where Device Guard blocks execution, dispatch `.github/workflows/build.yml` for
the branch; its unit and existing Add Text/Add Image/edit-toolbar smoke suites
exercise the built application. No existing-image replacement test is claimed.
