# Review fixes v041

Base: `13e917a`; local branch initially `t3/cef61dad` (T3 may rename it).
Push target: `origin/t3/review-fixes-v041`. PR context: #10.
Existing edits in three `ext/darkmodelib` resource files are excluded.

| # | Verdict | Evidence / fix | Commit |
| --- | --- | --- | --- |
| 1 | FALSE-POSITIVE | `DestroyToolbar` calls `DestroyToolsPanel` before deleting the toolbar host/tree; recreation cannot retain the cached sources. | — |
| 2 | CONFIRMED, pending | Missing COM output-pointer validation; also port host identity guard and explicitly validate live controls. Existing `Child` already rejects destroyed hosts. | pending |
| 3 | CONFIRMED + FIXED | Iterate a snapshot and check membership before dereferencing; apply the same rule to suspension callbacks. Regression failed pre-fix with `idx >= len` (exit 105). | Stabilize animation callback iteration (this commit) |
| 4 | CONFIRMED, pending | Width animations invalidate the entire frame every tick. | pending |
| 5 | CONFIRMED, pending | Layout creates inactive tabs and creation never selects the saved page. | pending |
| 6 | CONFIRMED + FIXED | Restore the first enabled row after page replacement; fall back to the close control when all rows are disabled (from `1c588b4`). | `c1d3ee9` |
| 7 | CONFIRMED + FIXED | Clamp loaded preferences with the shared panel bounds (Win32 only). Layout already clamps effective widths. | `a6c948b` |
| 8 | CONFIRMED, pending | SPI is queried on every animation eligibility check. | pending |
| 9 | CONFIRMED, pending | Control/Count rebuild vectors repeatedly; focus/hit testing becomes quadratic. | pending |
| 10 | CONFIRMED, pending | Empty ribbon labels bypass tooltip fallback. | pending |
| 11 | CONFIRMED, pending | Arrow/Home/End navigation uses tabs exclusively. | pending |
| 12 | CONFIRMED, pending | Rail mode collapses the Home page's View more control. | pending |
| 13 | FALSE-POSITIVE | `RibbonTabRow` is an HBox, not a VirtCtrl. Layout containers receive/store HWND-client bounds; `lastBounds.x` is already window-relative. | — |
| 14 | CONFIRMED, pending | Refresh builds a focusable vector even without a focused control. | pending |
| 15 | FALSE-POSITIVE | `showLabel = !compact && len(label) > 0` already guards `PlatformFontMeasureText`. | — |
| 16 | CONFIRMED + FIXED | Return before recording a null root. Regression covers null suspension. | Stabilize animation callback iteration (this commit) |
| 17 | CONFIRMED, pending | Focus outlines use the Material accent without a system high-contrast override. | pending |
| 18 | CONFIRMED, deferred | Fade rewrites every pixel. Direct AlphaBlend would violate Gfx layering and Direct2D flush ordering; defer the optional backend opacity API change. | — |
| 19 | CONFIRMED, pending | Rail mode leaves its frame splitter visible/hit-testable. | pending |
| 20 | CONFIRMED, pending | Selected toggle buttons expose PRESSED rather than CHECKED. | pending |
| 21 | CONFIRMED, pending | Annotation strip lacks the accessibility native-message hook. | pending |

Older commits: `7dd532a` and `1c588b4` inspected. Relevant source changes will be ported by hand; unrelated test formatting will be excluded.

Validation: pending. Regression tests added before fixes for callback removal and COM null output pointers.
