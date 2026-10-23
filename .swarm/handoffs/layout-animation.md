# Layout and animation

Branch / push target: `t3/layout-animation`.
Base: `a19ef51` (contextual left panel).

## Done

- Persisted ToolsPanelWidth / CommentsPanelWidth in DIP; regenerated settings.
- DPI-aware captured inner-edge splitters, bounded to 180/220..480 DIP.
- Progressive shrink, tools icon rail with tooltips, comments auto-hide, automatic restoration without overwriting preferences.
- Reserved 320 DIP document budget including sidebar/chat/navigation/details; comments and details share right-side space.
- Tightened tools spacing; hide fallback annotation strip when tools are visible.
- Single shared idle-stopped 160ms cubic ease-out timer; Windows client-area animation policy and resize/capture suppression.
- Panel width slides, ribbon underline movement, row/button hover/press/selection color transitions, tab-switch bitmap cross-fade.
- Animator/bitmap teardown; immediate input and command state throughout transitions.
- Unit coverage for collapse budgets at 100/150/200%, settings serialization, reduced-motion policy, easing and idle timer.
- Added layout-animation GUI smoke: DPI collapse/restore, idle animation timer, live handle drag when desktop permits, save/reopen persistence. Registered in SwiftLeaf CI and fast suite.
- Branding passed (629 files). Smoke script bundles successfully.
- Debug build passed initial integration; cross-fade compile caught and fixed diagnostic shadowing and formatter argument checks. Final rebuild running.

- Final integration Debug build passed; layout-animation smoke passed (all DPI scales, live handle drag, persistence, idle timer).
- Unit runner reached engine tests after new tests passed, then delay-load crashed (C06D007E): unit startup preceded eager PDF DLL loading. Added normal eager-load path to unit startup; rebuild pending.
- Fixed older contextual test's fixed-pixel narrow widths to follow actual DPI (150% desktop).
- Reject rail resize when minimum panel width cannot fit, preserving saved preference.

## Next

- Finish final Debug rebuild.
- Run unit runner, full requested SwiftLeaf CI, branding and diff checks; record exact results.
- Optional smooth comments scrolling is not implemented.

## Gotchas

- No STL. Read docs/development.md and contextual-panel.md.
- Original branch hand-edited ShowDocumentPanel / DocumentPanelMode into generated files; added them to generator so regeneration preserves them.
- Generator skips website docs because external website checkout is absent; generated branding text changes are expected.
- Panel width changes force layout because legacy snapshot does not include panel preferences.
- Build: powershell -NoProfile -ExecutionPolicy Bypass -File tools/swiftleaf/build.ps1 -Config Debug.
- clang-format is under VS2022 BuildTools/VC/Tools/Llvm/bin.
- GUI/unit execution may be blocked by Windows Application Control; do not call blocked tests passed.
- Commit messages end with prompt: line; push each step to origin HEAD:t3/layout-animation.
