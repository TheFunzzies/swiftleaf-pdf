# Accessibility and standards: current limits

These tools inspect selected PDF properties. They do not establish PDF/UA,
WCAG, Section 508 or PDF/A compliance.

## Accessibility

Language, title and field descriptions are checked as stored metadata.
Reading order always requires manual review. MarkInfo and a nonempty K
array do not prove a usable tag tree.

Direct page image resources are checked for Figure Alt text linked by OBJR.
MCID marked-content associations, ParentTree consistency, inherited resources,
inline images, nested forms and vector figures are not fully verified. Unverified
associations produce a warning, never an Alt-text pass. Review Alt quality,
decorative content, table header scope and reading order with assistive technology.

Auto-tagging links each untagged page's content to a draft P through MCID 0
and ParentTree. Annotation roles use OBJR and StructParent. Draft output leaves
Marked false. Existing source structure trees are preserved. These links do not
reconstruct paragraphs, headings, tables or semantic reading order. Figure
occurrence editing remains unsupported. See [Accessibility](accessibility.md)
for supported manual repairs and limits. Independent PDF/UA validation remains
necessary.

## PDF/A preparation

Preparation writes identification metadata and an RGB output intent. It requires
parseable ICC bytes and fails without a valid profile. No placeholder profile is
written. Fonts are not embedded and transparency is not flattened. Original
transparency is retained to avoid changing appearance by deleting graphics state.
Output is an unverified candidate, not a compliant archive.

The checker reports selected metadata, font and transparency findings. It always
reports compliance as unverified: marker strings alone cannot prove conformance.
Nested resources, font glyph coverage and XMP semantics require independent
validation, such as veraPDF. Check rendering and extraction after saving.

## Windows UI

The tools search edit participates in native Tab navigation. Its width is bounded
by the available client area when resized. Ribbon overflow uses DPI-scaled widths
and existing high-contrast colors. No animation was added; reduced-motion behavior
is unchanged. 800x600, high DPI, high contrast, IME and Narrator/NVDA still require
interactive verification; these changes do not constitute a full UI audit.
