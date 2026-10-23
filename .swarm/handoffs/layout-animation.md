# Layout and animation

Branch: `t3/layout-animation`; push target: `t3/layout-animation`.
Base: `a19ef51` (contextual left panel).

## Done

- Added persisted ToolsPanelWidth / CommentsPanelWidth (96-DPI units); regenerated settings.
- Reused captured virtual splitters for inner-edge resizing with DPI-aware min/max.
- Added progressive panel shrinking, tools icon rail, then comments auto-hide; preserve preferences.
- Reserved 320 DIP for document; budget includes sidebar/chat/navigation/details.
- Comments and document details share right-panel space; tightened tools spacing.
- Added layout budget/threshold tests at 100/150/200% and settings serialization round-trip.
- Debug build found pre-existing hand-edited document settings absent from generator; added generator definitions. Rebuild pending; new files registered in premake and both VS projects.

## Next

- Finish compile verification; implement shared reduce-motion-aware animator.
- Animate panel widths, ribbon underline, page content and hover/press/selection.
- Add GUI smoke coverage and run requested validation; update this handoff after each step.

## Gotchas

- No STL; Win32 virtual controls. Original handoff: contextual-panel.md.
- Panel resize requests force relayout (width preferences are outside the legacy layout snapshot).
- New settings regenerate branding text as well as metadata (generator is source of truth).
- Generator skips website docs: external sumatra-website checkout is absent.
- clang-format is under VS2022 BuildTools/VC/Tools/Llvm/bin.
- GUI/unit execution may be blocked by Windows Application Control; record actual results.
- Commit messages end in one concise prompt: line. Push every completed step to origin HEAD:t3/layout-animation.
