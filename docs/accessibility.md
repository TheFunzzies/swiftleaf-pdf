# Accessibility

Basic remediation writes a copy. Untagged pages receive one P element linked
through MCID 0, page StructParents and ParentTree. Annotation elements use
OBJR and their own StructParent entries. Existing structure trees are preserved.
Orphan page StructParents reject conversion rather than overwrite source links.

This is a draft grouping, not semantic tagging or PDF/UA conversion. Page streams
retain their original paint order. The checker lists source roles in tree order,
inherited page references, MCID and OBJR identities, and TH scope warnings.
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
independent of their values. Tools search provides search result help and connects
with panel tools via Tab, Shift+Tab, and Down/Enter.
Ribbon collapses into an overflow menu and compacts tabs/separators when window
width is constrained. Further width pressure moves inactive modes and page
navigation to overflow. Narrow tool rails show tool icons; favorite stars return
when expanded. Native page edits hide with their navigation group.
Virtual controls expose MSAA roles and keyboard shortcuts, and animations
respect Windows reduce-motion settings.

Regression checks cover scroll visibility, stable native MSAA names, ribbon
overflow and restoration, rail icon width, and native search-to-tools focus.
`bun tests/tools-panel.ts --no-build` passes at normal DPI and with
`SUMATRA_DPI_OVERRIDE=200`, including the 800x600 rail. Visual checks at the 200%
override cover that frame and the fixes dialog resized to 780x580, including
keyboard scrolling to Apply and Cancel. Instructions use short separate lines.
Actual monitor transitions, high contrast, IME composition and screen-reader
speech still require interactive verification. This is not a full UI audit.
