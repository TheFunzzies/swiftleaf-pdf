# UI overhaul handoff (from Claude Opus agent, stopped early: usage limit)

Repo: SwiftLeaf PDF (Win32 C++, fork of upstream reader). Conventions: docs/development.md (no STL, src/base containers).
Branch state: recolor done (src/Material.cpp, src/shared/Theme.cpp, rounded tabs, blue accent) + WIP:
- icons: Lucide SVG icons already added (commit "Add Lucide icons used by PDFCraft")
- settings ShowToolsPanel / ShowCommentsPanel added (src/Settings.*)
- UNFINISHED scaffold in src/Ribbon.*, src/Toolbar.*, new src/ToolsPanel.h (read `git log -5 -p` to see what exists; it may not compile — finish or revert it)

Target: screenshot at C:\Users\pstei\.t3\userdata\attachments\e37ff64a-1f50-4203-aead-42aaf77124a7-79dc7852-ad2a-49c6-bada-e7c48b26354b.png (view it) + PDFCraft (https://github.com/PDFCraftTool/pdfcraft, open source; copying fonts/icons allowed; clone OUTSIDE repo, credit in README).
1. LEFT "All tools" panel (header + X; rows icon+label: Export a PDF, Fill & Sign, Edit a PDF, Create a PDF, Combine files, Organize pages, Add comments, Scan & OCR, Protect a PDF, Redact a PDF, Compress a PDF, Prepare a form, Add a stamp, Use a certificate; blue "View more" link). Each row runs the matching EXISTING command (src/Commands.*, ribbon ids); no matching command -> disabled w/ tooltip. Toggle visibility, persist in settings.
2. TOP bar: Menu button, tabs `All tools | Read | Edit | Convert | E-Sign` (map to existing ribbon pages/panels), "Find tools and commands" box (reuse src/CommandPalette.cpp, Ctrl+K), info/save/print icons right.
3. RIGHT comments panel: "Comments <n>", grouped by page, author/type/date/text, threaded replies; slim icon rail (comments, bookmarks, pages, layers, attachments; extend src/NavRail.*), floating vertical annotation tool strip over the document.
4. Optional banner ("This document contains N interactive form fields." + Highlight fields) when form present; page/zoom cluster lower right.
5. Light + dark themes both correct.
Rules: small commits; new files registered in premake5.files.lua AND vs2022/*.vcxproj (edit by hand if bin/premake5.exe blocked); do not mass-edit strings (branding rename already merged; keep "SwiftLeaf"); don't touch docs/ except README credits.
Build: `powershell -File tools/swiftleaf/build.ps1 -Config Debug`. A previous agent reported the full build produced out\dbg64\Swiftleaf.exe; if packaging is blocked by Device Guard (MSB3073), zero C++ compile errors is the local bar. Use tools/swiftleaf/shot.ts for screenshots if a runnable exe exists. Run `bun tools/swiftleaf/check-branding.ts` before finishing.
Finish: reply with branch, final SHA, files changed, implemented vs stubbed, compile status. Concise.
