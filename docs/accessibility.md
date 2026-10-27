# Accessibility

Basic remediation writes a copy. Untagged pages receive one P element linked
through MCID 0, page StructParents and ParentTree. Annotation elements use
OBJR and their own StructParent entries. Existing structure trees are preserved.
Orphan page StructParents reject conversion rather than overwrite source links.

This is a draft grouping, not semantic tagging or PDF/UA conversion. Page streams
retain their original paint order. The checker lists source roles in tree order,
page object references, MCID/child reference kinds and TH scope warnings.
It never certifies reading order or table header associations from tag presence.

Limits: no paragraph, heading or table reconstruction; no tag reorder editor.
Figure descriptions and decorative marking are disabled: resource objects can
occur repeatedly, and rewriting a resource alone cannot tag individual uses.
No synthetic Artifact structure roles are emitted. Existing stream artifacts
are preserved. New artifacts require an occurrence-aware content editor.
Inline/nested images, role maps and complete MCID coverage need manual review.
Use an independent PDF/UA validator and an actual screen reader before delivery.

## Windows UI

Accessibility dialogs fit the monitor work area and scroll their content.
Tab and Shift+Tab follow layout order and reveal controls in scroll containers.
Language, title, destination and tools-search edits expose stable MSAA names,
independent of their values. Tools search provides search result help and connects
with panel tools via Tab, Shift+Tab, and Down/Enter.
Ribbon collapses into an overflow menu and compacts tabs/separators when window
width is constrained, avoiding clipping at 800x600 and high DPI.
Virtual controls expose MSAA roles and keyboard shortcuts, and animations
respect Windows reduce-motion settings.

Regression checks cover scroll visibility and native MSAA names after value edits,
ribbon overflow collapse and separator compaction, and search edit focus flow.
800x600, 200% DPI, high contrast, IME composition and real screen-reader speech
still require interactive verification; unit tests do not establish a full audit.
