# Layout and animation

Branch / push target: `t3/layout-animation`. Base: `a19ef51` (contextual panel).

## Done

- Persisted ToolsPanelWidth / CommentsPanelWidth in DIP; regenerated settings.
- Captured inner-edge splitters with resize cursors, DPI scaling and 180/220..480 DIP limits.
- Progressive shrink, tools icon rail with labels hidden/tooltips retained, comments auto-hide, and automatic restoration without changing preferences.
- Reserved 320 DIP for document, including other panes in the budget; native resize minimum respects DPI too.
- Comments and document details share right-side space. Tightened tools rows; fallback annotation strip hides while tools are visible.
- One shared idle-stopped 160ms cubic ease-out timer; SPI_GETCLIENTAREAANIMATION and native/splitter resize suppression.
- Panel width slides, ribbon underline/highlight movement, hover/press/selection color transitions, and tab-switch row cross-fade. Input remains live; timer state and bitmaps clean up on teardown.
- Unit tests for collapse budgets at 100/150/200%, settings round-trip, motion policy/easing and idle timer.
- Registered layout-animation GUI smoke in SwiftLeaf CI and fast suite: DPI thresholds, restoration, cursor drag, save/reopen persistence, idle animations; optional screenshot capture.
- Corrected contextual smoke's fixed-pixel narrow widths for the 150% desktop.
- Initialized the PDF DLL before engine unit tests; original startup order caused delay-load exception C06D007E.

## Validation

- Requested Debug build: PASS, no warnings/errors (final rebuild after native minimum change).
- Requested unit runner: PASS, 110,839 assertions, including new panel/settings/animation tests.
- layout-animation: PASS in full CI (19.1s); PASS again with screenshots (75.7s), all three DPI scales. Inspected full and icon-rail screenshots; no overlap in panel content.
- contextual-panel: initial full-CI DPI assertion failure corrected; standalone rerun PASS (31.8s). One earlier standalone startup could not connect to its pipe.
- Branding: PASS (630 files). CI exception tests: 3 PASS. Smoke script bundles; git diff --check clean.
- Full requested SwiftLeaf CI: 22/27 PASS initially; contextual failure fixed and verified separately. Four other cases remain below. Full logs: `.work/swiftleaf-ci/results.md` and per-case `output.txt`.
- Runtime was available; these results were not blocked by Windows Application Control.

## Next

- Investigate remaining full-CI failures; no assertions or known-failure patterns were weakened:
  - annot-undo-redo: square resize stayed 90x90; reproduced in isolated rerun.
  - pdf-edit-toolbar-interaction: expected UTC metadata date differs from local output (08:34); existing exception only accepts 12:34, so CI correctly reported FAIL.
  - edit-safety: needs missing Release static libraries (`out/rel64/obj-s/base.lib`); only Debug build requested/performed.
  - annot-color-dropdown: timeout with real-cursor movement; isolated rerun later failed to connect on an app restart. Root cause unconfirmed.
- Optional smooth scrolling of the comments list remains unimplemented.

## Gotchas

- No STL. Read `docs/development.md` and `contextual-panel.md`.
- Original branch hand-edited ShowDocumentPanel / DocumentPanelMode into generated files; both are now defined in the generator.
- Generator skips website docs when external website checkout is absent; generated branding text changes are expected.
- Width changes force layout because legacy layout snapshots omit panel preferences.
- GUI automation logged concurrent real cursor movement; preserve test failures rather than treating them as passed.
- Set SWIFTLEAF_CAPTURE_LAYOUT=1 for screenshots in the layout smoke's temporary directories; later tests wipe those directories.
- Commit messages end with prompt: line; push each completed step to origin HEAD:t3/layout-animation.
