# Editing existing text and fonts

Choose **Edit Text**, then click a paragraph or use an existing text selection.
Enter commits; Shift+Enter inserts a paragraph break; Escape cancels. A failed
commit keeps the draft open for correction or copying. Save and Save As use the
atomic sibling-file replacement described in [recovery.md](recovery.md).
The **Edit** tab and **All tools** also provide **Find and Replace Text** and
**Font Manager** (`CmdPdfReplaceText` and `CmdPdfFontManager`).
While a CJK input method is composing a character, Enter and Escape belong to
the input method. Commit the completed draft with Enter after composition ends.

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
Saved CID subsets remain editable even when the font program has no Unicode
lookup table: spacing and glyph reuse use the PDF's decoded glyph IDs.

The editor preserves font size, fill color and the first baseline. Paragraph
replacement wraps within the original width, preferring spaces; CJK and long
words can wrap between characters. Additional lines can extend below the old
block and overlap following content. Replacement glyphs extending beyond the
page are rejected before source removal. Inspect the result and undo if needed.

## Font manager

The font manager lists fonts in page resources, nested Form XObjects and
annotation appearances, including embedded status, font type and encoding.
Fonts without the `embedded` label are not embedded. Inherited page resources
are included; unused resources can appear in the list.

Select text on one page, open **Font Manager**, choose an installed family and
click **Replace Selection Font**. This changes complete selected lines, keeping
their size, color and initial baseline. The selected family must supply every
glyph and allow embedding; an unavailable font or glyph rejects the operation.
No Unicode fallback is substituted for an explicit family choice. The engine
keeps the source's bold/italic request; the font picker's size is ignored.
Save commits the change to disk; ordinary Undo/Redo applies before saving too.

## Find and replace

**Find and Replace Text** replaces non-overlapping, case-sensitive literal
matches within paragraphs on every page. Soft line breaks are searched as spaces.
Matches cannot cross paragraph or page boundaries; regex, OCR and form-field
replacement are not provided. An empty replacement deletes matches.

Choose a separate output file and click **Replace All in Copy**. The copy includes
unsaved edits. All changes run on a private snapshot; a missing glyph, unsupported
matching block, overlapping block or save failure leaves the open document and
any existing output file intact. Vertical-only blocks currently stop the scan,
even when they contain no match. No-match searches write nothing. Successful
copies open for inspection. Large documents can pause the UI during this operation.

## Images and grouped objects

Choose **Edit Images and Objects** from **All tools**, or **Objects** in the
**Edit** tab. The list shows named images and grouped objects on the current
page. Enter a listed name without its parenthesized type. Optionally enter a
replacement image file, set width/height percentages and rotation, then choose
**Save Edited Copy**. Negative percentages flip an axis; rotation and scaling
use the object's center. Image replacement retains the original placement.

Every use of that named object on the current page changes. Other pages and the
open document remain unchanged, even when pages share resources. Unsaved edits
are included in the copy. Failure preserves an existing destination file.
Objects can grow outside the page and be clipped; inspect the saved copy.
The list includes unused resources. Inline images, stencil images, individual
paths and objects nested inside a group cannot be selected separately. Cropping,
drag handles, translation, multi-selection, alignment and distribution are not
provided. This dialog adds no animation and uses the existing Windows dialog UI.

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
- Blocks overlapping annotations, links or form fields are rejected to prevent
  redaction from removing interactive content. Text removal still uses geometric text-only redaction;
  original content-stream operators are not edited in place. Images and vector
  artwork are excluded from that redaction.
- Add Text, Add Image, watermarks, headers/footers and Bates numbering retain
  their existing paths; this change does not extend their Unicode behavior.

## Verification

`TextEditUnicodeTest` in `src/tests/Sumatra_ut.cpp` replaces source text, saves,
reopens and checks exact extracted text for Latin, Cyrillic, Greek and CJK. It
also checks embedded subset Type0 fonts, ToUnicode streams, undo/redo and CJK
reflow, and confirms unsupported text and off-page edits leave the source
unchanged. `tests/edit-safety.cpp` checks strict UTF-8
validation and unsupported shaping/control characters before document access.
`tests/existing-text-edit.ts` checks failed-draft retention, IME key handling and
saving/reopening CJK text through the editor. The unit suite also checks that
overlapping links reject edits without removing source text.
`TextReplaceToolsTest` covers repeated matches on multiple pages, Unicode output,
empty replacement, explicit font changes, unsaved edits in the snapshot and
failure preserving an existing output. The contextual-panel smoke opens both
new dialogs and checks their command availability. `ObjectEditTest` verifies
image replacement, grouped-object transforms, shared resource isolation and
failure preserving an existing output file.

Run the Debug build and `bun cmd/run-unit-tests.ts -dbg --no-build`. On a host
where Device Guard blocks execution, dispatch `.github/workflows/build.yml` for
the branch; its unit and existing Add Text/Add Image/edit-toolbar smoke suites
exercise the built application.
