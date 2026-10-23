# Review fixes v041

Base: `13e917a`; local branch initially `t3/cef61dad` (T3 may rename it).
Push target: `origin/t3/review-fixes-v041`. PR context: #10.
Existing edits in three `ext/darkmodelib` resource files are excluded.

| # | Verdict | Evidence / fix | Commit |
| --- | --- | --- | --- |
| 1 | FALSE-POSITIVE | `DestroyToolbar` calls `DestroyToolsPanel` before deleting the toolbar host/tree; recreation cannot retain the cached sources. | — |
| 2 | CONFIRMED + FIXED | Validate COM output pointers and live controls; port host identity guard. Existing Child already rejects destroyed hosts, so ordinary teardown was overstated. | `88a7505` |
| 3 | CONFIRMED + FIXED | Iterate a snapshot and check membership before dereferencing; apply the same rule to suspension callbacks. Regression failed pre-fix with `idx >= len` (exit 105). | `adabab5` |
| 4 | CONFIRMED + FIXED | Width animations delegate painting to layout; invalidate only the union of each panel slot and its old/new splitter bounds. Also avoid invalidating all desktop windows for null test HWNDs. | `b088bfd` |
| 5 | CONFIRMED + FIXED | Apply the saved ribbon page after building the new toolbar tree. | `83348d0` |
| 6 | CONFIRMED + FIXED | Restore the first enabled row after page replacement; fall back to the close control when all rows are disabled (from `1c588b4`). | `c1d3ee9` |
| 7 | CONFIRMED + FIXED | Clamp loaded preferences with the shared panel bounds (Win32 only). Layout already clamps effective widths. | `a6c948b` |
| 8 | CONFIRMED + FIXED | Cache client-area animation preference lazily and refresh it on WM_SETTINGCHANGE. | `0a3084b` |
| 9 | CONFIRMED + FIXED | Allocation-free Control/Count traversal; collect once for focus/hit testing and focus notifications. | `88a7505` |
| 10 | CONFIRMED + FIXED | Empty ribbon labels fall back to tooltip text. | `88a7505` |
| 11 | CONFIRMED + FIXED | Arrow/Home/End navigation traverses all enabled virtual top-bar tab stops, including Menu/Search/Properties/Save/Print. | `83348d0` |
| 12 | CONFIRMED + FIXED | Keep View more visible and keyboard-focusable as an icon-only ribbon button in rail mode. | Preserve command access in the tools rail (this commit) |
| 13 | FALSE-POSITIVE | `RibbonTabRow` is an HBox, not a VirtCtrl. Layout containers receive/store HWND-client bounds; `lastBounds.x` is already window-relative. | — |
| 14 | CONFIRMED, pending | Refresh builds a focusable vector even without a focused control. | pending |
| 15 | FALSE-POSITIVE | `showLabel = !compact && len(label) > 0` already guards `PlatformFontMeasureText`. | — |
| 16 | CONFIRMED + FIXED | Return before recording a null root. Regression covers null suspension. | `adabab5` |
| 17 | CONFIRMED, pending | Focus outlines use the Material accent without a system high-contrast override. | pending |
| 18 | CONFIRMED, deferred | Fade rewrites every pixel. Direct AlphaBlend would violate Gfx layering and Direct2D flush ordering; defer the optional backend opacity API change. | — |
| 19 | CONFIRMED + FIXED | Collapse the tools splitter in rail mode and exclude it from the applied handle width. | Preserve command access in the tools rail (this commit) |
| 20 | CONFIRMED + FIXED | Expose selected toggle buttons as CHECKED. | `88a7505` |
| 21 | CONFIRMED + FIXED | Forward the annotation strip WM_GETOBJECT messages. | `88a7505` |

Older commits: `7dd532a` and `1c588b4` inspected. Host identity guard, top-bar/panel focus notifications, disabled-panel focus fallback and removal of leading menu separators ported by hand; arrow encoding remains pending. The older hover-option keyboard hook targets code absent here. Unrelated test formatting excluded.

Validation: pending. Regression tests added before fixes for callback removal and COM null output pointers.
