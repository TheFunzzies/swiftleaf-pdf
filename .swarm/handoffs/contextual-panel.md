# Contextual left panel

Branch: `t3/contextual-left-panel`

## Done

- Read `docs/development.md`; confirmed Win32/no STL requirements.
- Traced ribbon page definitions, centralized command state, dropdowns, panel,
  and debug-control smoke-test helpers.

## Next

- Present existing ribbon commands as grouped contextual panel rows.
- Collapse horizontal strip and add hidden/narrow-panel tab menu fallback.
- Cover tab switching, command state, keyboard access and updated smoke layout.
- Run requested Debug build, unit tests, SwiftLeaf CI and branding check.
- Capture each tab in both themes under `.swarm/docs/` if launch works.

## Gotchas

- User explicitly authorizes commits/pushes after meaningful steps (overrides
  development guide's default no-auto-commit rule).
- Current branch already has requested name; starting tree is clean.
- Keep original toolbar controls as command-state sources to preserve options
  and toggles. Hidden source bounds must never anchor a visible dropdown.
- Use `-for-testing` for launches. Format touched C++ before building.
- CI has existing known legacy ribbon failures; update affected assertions.
- Commit messages must end with one concise `prompt:` line.
