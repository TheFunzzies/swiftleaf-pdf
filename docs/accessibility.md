# Accessibility

Basic remediation writes a copy. Untagged pages receive one P element linked
through MCID 0, page StructParents and ParentTree. Annotation elements use
OBJR and their own StructParent entries. Existing structure trees are preserved.
Orphan page StructParents reject conversion rather than overwrite source links.

This is a draft grouping, not semantic tagging or PDF/UA conversion. Page streams
retain their original paint order. The checker lists source roles in tree order,
page object references, MCID/child reference kinds and TH scope warnings.
It never certifies reading order or table header associations from tag presence.

The fixes dialog accepts decorative **whole page** ranges, such as `2,4-5`.
Only select pages with no meaningful content. Their streams are enclosed in
`/Artifact BMC` and `EMC`, and excluded from draft P tags. Annotations remain
separately tagged. This operation rejects source structure trees and orphan
StructParents rather than damage their links; invalid ranges publish no output.

For a source TH, use its object number from the tag report and choose `Row`,
`Column` or `Both` in the fixes dialog. Only that reachable TH's Table Scope
attribute changes; other attributes and source tag order remain. This does not
infer headers or validate cell associations. Leave the object number blank to
preserve scope.

Limits: no paragraph, heading or table reconstruction; no tag reorder editor.
Individual figure descriptions and decorative occurrences remain unsupported:
resources can occur repeatedly. No synthetic Artifact structure roles are
emitted. Existing stream artifacts are preserved.
Inline/nested images, role maps and complete MCID coverage need manual review.
Use an independent PDF/UA validator and an actual screen reader before delivery.

## Windows UI

Accessibility dialogs fit the monitor work area and scroll their content.
Tab and Shift+Tab follow layout order and reveal controls in scroll containers.
Language, title, destination and tools-search edits expose stable MSAA names,
independent of their values. Ribbon and tools already expose virtual controls
through MSAA and use system high-contrast colors and reduced-motion settings.

Regression checks cover scroll visibility and native MSAA names after value edits.
800x600, 200% DPI, high contrast, IME composition and real screen-reader speech
still require interactive verification; unit tests do not establish a full audit.
