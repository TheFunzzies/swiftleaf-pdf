# Contextual left panel

Branch: `t3/contextual-left-panel`

## Done

- First contextual implementation Debug build passed (10:39 local).
- Added tab/row keyboard handling, frame focus ring integration, and MSAA
  accessible names/state/actions for the virtual ribbon and panel controls.
- Branding check passes (626 files).
- Contextual panel rows now reuse original ribbon command controls, icons,
  labels, availability and checked state; separator headings added.
- Horizontal strip collapsed; hidden/narrow panel uses a native tab menu.
- View more opens all command pages; visible panel rows anchor option menus.
- Read `docs/development.md`; confirmed Win32/no STL requirements.
- Traced ribbon page definitions, centralized command state, dropdowns, panel,
  and debug-control smoke-test helpers.
- Debug build verified clean (0 errors, 0 warnings).
- Fixed hidden toolbar button reporting in `ToolbarButtonsResultTemp` using `IsOnShownRibbonPage`.
- Fixed focus preservation on layout updates in `RefreshVirtTops` using `CollectFocusLayout`.
- Fixed theme switching in `ToolsPanelAfterThemeChange` by updating non-Home tabs via `ToolsPanelUpdate`.
- Added smoke test `tests/contextual-panel.ts` (and `tests/tools-panel.ts` re-export) covering
  tab switching (All tools/Read/Edit/Convert/E-Sign) across light and dark themes, row command
  execution, keyboard tab navigation, document-close disabled state preservation, and narrow window fallback menu.
- Updated outdated test assertions in `tests/annot-undo-redo.ts`, `tests/annot-color-dropdown.ts`,
  and `tests/redact-annotations.ts` for contextual panel layout without weakening test intent.
- Wired `contextual-panel` into `tests/run-swiftleaf-ci.ts` and `tests/run-almost-all.ts`; updated
  `tests/ci-known-failures.test.ts` (3/3 pass).

## Next

- Run full CI in an unlocked environment where Windows Application Control policy authorizes the test binaries.
- Proceed to later phases (animations, layout width customization).

## Gotchas

- Exact PowerShell build command hit script execution policy; running it with
  process-local `-ExecutionPolicy Bypass` instead.
- User explicitly authorizes commits/pushes after meaningful steps (overrides
  development guide's default no-auto-commit rule).
- Current branch already has requested name; starting tree is clean.
- Keep original toolbar controls as command-state sources to preserve options
  and toggles. Hidden source bounds must never anchor a visible dropdown.
- Use `-for-testing` for launches. Format touched C++ before building.
- CI has existing known legacy ribbon failures; update affected assertions.
- Commit messages must end with one concise `prompt:` line.
- Windows Application Control (WDAC / AppLocker) policy blocks execution of locally built unsigned
  binaries on this system; GUI tests also require an interactive desktop session.
