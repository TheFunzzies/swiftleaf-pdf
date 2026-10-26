# Accessibility and standards: current limits

These tools inspect selected PDF properties. They do not establish PDF/UA,
WCAG, Section 508 or PDF/A compliance.

## Accessibility

Language, title and field descriptions are checked as stored metadata.
Reading order always requires manual review. MarkInfo and a nonempty K
array do not prove a usable tag tree.

Direct and inherited page image resources are checked for Figure Alt text linked
by OBJR, with a matching StructParent entry pointing back through ParentTree.
MCID marked-content associations, full ParentTree consistency,
inline images, nested forms and vector figures are not fully verified. Unverified
associations produce a warning, never an Alt-text pass. Review Alt quality,
decorative content, table header scope and reading order with assistive technology.

Auto-tagging creates draft roles inferred from extracted text. It does not
rewrite content streams with MCIDs or create a ParentTree. Draft output leaves
Marked false. Existing source structure trees are preserved. Figure descriptions
in a draft are not linked to painted content; Artifact roles do not mark stream
content as decorative. Full remediation needs a content-aware editor and
independent PDF/UA validation.

## PDF/A preparation

Preparation writes an unverified preparation note and an RGB output intent. It
does not write a PDF/A conformance declaration (`pdfaid:part`). It requires
parseable ICC bytes and fails without a valid profile. No placeholder profile is
written. Fonts are not embedded and transparency is not flattened. Original
transparency is retained to avoid changing appearance by deleting graphics state.
Output is an unverified candidate, not a compliant archive.

The checker reports selected metadata, font and transparency findings. It always
reports compliance as unverified: marker strings alone cannot prove conformance.
Nested resources, font glyph coverage and XMP semantics require independent
validation, such as veraPDF. Check rendering and extraction after saving.

## Windows UI

The tools search edit supports Down, Enter and Tab into matching tools; Shift+Tab
from the first panel control returns to search. Escape clears search, then returns
to the document. Favorites announce their action and tool name. Search width is
bounded by the panel. Ribbon overflow uses measured, DPI-scaled control widths;
high contrast uses system colors. No animation was added; reduced-motion behavior
is unchanged. The focused tools-panel regression covers 800x600 and search keyboard
entry. High DPI, high contrast, IME and Narrator/NVDA still require interactive
verification; these changes do not constitute a full UI audit.
