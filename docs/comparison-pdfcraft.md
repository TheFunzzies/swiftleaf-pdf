# SwiftLeaf PDF versus PDFCraft

Research date: **23 October 2026**. Verdict: **niche, viable**. SwiftLeaf is a credible lightweight Windows reader with useful editing, but not an equivalent replacement for PDFCraft's broader document-production workflows. It wins on distribution size and observed memory use. It loses on workflow depth, portability and distribution trust. Neither project establishes Acrobat-level correctness merely by listing features.

## Scope and identity

There are **two unrelated PDFCraft products**:

| Product | Architecture | Examined |
|---|---|---|
| SwiftLeaf | Win32 C++, SumatraPDF fork, MuPDF | Local HEAD `b8c8d893b351bf899547f7086ab46742e7508f23`; downloaded **v0.4.1 release** |
| [Native PDFCraft](https://github.com/storytold/pdfcraft) | Rust, egui/eframe/wgpu, Hayro/LoPDF | Installed **0.9.0**; current **0.13.0-era source**, `c9ac28a04843141f2261ade4b9a19d51aed6f182` |
| [Browser PDFCraft](https://github.com/PDFCraftTool/pdfcraft) | Next.js/React, PDF.js, pdf-lib, WASM | Source only, `53e469252b616aa0ac654efa267d2e92ac821866` |

Installed `C:\Program Files\PdfCraft\pdfcraft.exe` identifies as 0.9.0 in its Windows version resource. It is **not Electron** and is not PDFCraftTool's browser project. The latter also contains Tauri packaging. Native PDFCraft's desktop rendering is not PDF.js/WASM.

Clones were created in new directories outside SwiftLeaf: `C:\Users\pstei\AI\Projects\_cmp\pdfcraft` and `C:\Users\pstei\AI\Projects\_cmp\pdfcraft-web`. No scripts, dependencies, tests or executables from the clones were run. Native v0.9.0 was fetched read-only: its command registry already contains OCR, DOCX export, form preparation, compare, signing, redaction and accessibility. Later source details are not claimed as runtime-tested 0.9.0 behavior.

SwiftLeaf was downloaded with `gh release download v0.4.1 --repo TheFunzzies/swiftleaf-pdf --pattern Swiftleaf.exe` into fresh directory `C:\Users\pstei\AI\Projects\_cmp\swiftleaf-release-aa471c2d`. SHA-256 matched GitHub metadata: `274ae0c92de5d60ce91d1938b0e83110c1f5b45572875d1b19592ed30ea9b04c`.

**The downloaded release runs successfully.** Device Guard did not block it in this session. No security policy was changed; the debug build was not used.

## Footprint and speed

Host: Windows 11 Home `10.0.26300`, Intel Core i7-1065G7, approximately 15.59 GiB visible RAM. Other applications were running. MiB means 1,048,576 bytes; directory sizes are logical bytes, not allocated disk clusters.

| Metric | SwiftLeaf 0.4.1 | Native PDFCraft 0.9.0 |
|---|---:|---:|
| GUI exe | **12.43 MiB**, 13,033,472 bytes | **89.97 MiB**, 94,340,464 bytes |
| Installer download | **12.43 MiB** | **68.66 MiB**, 71,999,488-byte x64 MSI |
| Portable download | **12.43 MiB**, executable | **82.25 MiB**, 86,247,361-byte ZIP |
| Program payload | Portable exe **12.43 MiB**; installer not installed | **166.48 MiB**, 174,567,758 bytes / 11 files |
| Other payload | Windows-provided OCR language packs | CLI **64.79 MiB**, models/notices about **11.72 MiB** |
| Authenticode | **NotSigned** | **Valid** |

Latest native PDFCraft 0.13.0 x64 assets: **69.79 MiB MSI** (73,175,040 bytes), **84.05 MiB ZIP** (88,133,416 bytes). No upgrade or 0.13.0 installed-size measurement was performed. Download sizes come from release metadata.

SwiftLeaf's exe is **7.24 times smaller** than the installed PDFCraft GUI. Its portable payload is **13.39 times smaller** than PDFCraft's entire installation, which includes a CLI/models. Settings, caches, OS runtimes, GPU memory and Windows OCR packs are excluded. Browser/WebView runtime and cached WASM/model footprint were not measured; browser PDFCraft cannot fairly be called zero-footprint.

### Local memory and navigation

Fixture: `C:\Users\pstei\AI\Projects\_cmp\benchmark-100-pages.pdf`, **100 pages, 39,922 bytes**. It repeats the six-page `docs/test/swiftleaf-sample.pdf` with pypdf, sharing resources. Text, tables and vector shapes; **not a large-file/scanned-document benchmark**. Printed footers repeat 1–6; the application count is 100.

| Observation | SwiftLeaf | Native PDFCraft |
|---|---:|---:|
| Idle working set, initial snapshot | **93.88 MiB** | **408.90 MiB** |
| Idle private bytes, same snapshot | **53.71 MiB** | **348.75 MiB** |
| 100-page working set, paired post-navigation snapshot | **95.04 MiB** | **310.29 MiB** |
| 100-page private bytes, same snapshot | **60.65 MiB** | **361.72 MiB** |
| One warm restart: launch to nonzero main-window handle | **1.344 s** | **0.440 s** |
| Open/first-page render latency | Not reliably measured | Not reliably measured |
| Navigation observed | Ctrl+End reached **100/100** | Wheel scrolling changed **page 1 to 2** |
| FPS / scroll/zoom latency | Not measured | Not measured |

Counters: `Get-Process.WorkingSet64` and `PrivateMemorySize64`. Private bytes mean committed private virtual memory, **not resident RAM**. Working set changes with Windows trimming; PDFCraft's lower loaded working set does not mean loading a PDF frees intrinsic application memory. A later fresh idle PDFCraft snapshot was 386.43 MiB working set / 348.25 MiB private bytes. Screenshot/accessibility capture and different window sizes are confounders. SwiftLeaf's lower footprint is clear; a universal multiplier is not. Loaded private bytes were about six times lower in this fixture.

Startup used Stopwatch around Start-Process and 25 ms process-metadata polling. One warm restart per app, no reboot/repeated trials. Titles remained empty at the handle threshold: an allocated window is **not a rendered/usable page**. Initial SwiftLeaf launch command took 2.043 s, including process/security overhead, not a render benchmark. The restart proxy favored PDFCraft; no SwiftLeaf startup victory is claimed.

Both visibly rendered the simple fixture without crashing. PDFCraft Ctrl+2 changed the view but the immediate exposed zoom label remained 100%, so no quantitative zoom result is claimed. Automation snapshots do not establish smoothness. Editing/save round trips, OCR accuracy, signature interoperability and destructive redaction were not functionally benchmarked.

### Large files: architectural inference

SwiftLeaf inherits MuPDF/SumatraPDF's mature rendering/cache machinery, a plausible advantage on constrained Windows systems. This does not prove universally faster or more faithful output; new editing code is younger than its renderer.

Native PDFCraft uses native Rust Hayro, tiled page rendering and an editable object graph with incremental revisions. Wgpu presentation does not imply all page rendering is GPU accelerated. Its `docs/gaps.md`, gap 13, acknowledges missing file-backed loading/global memory budgets. Upstream October 12 Linux synthetic results describe whole-file memory costs and a 9,156-page example at 403 MiB / 5.5 s. These are **another build/OS's reported results**, not measurements here; later fixes may differ.

Browser PDFCraft can incur WASM/model initialization and multiple-buffer costs, particularly for conversion. Caching and efficient WASM mitigate them. No native/browser speed ratio was measured.

## Feature matrix

**Have:** implemented source path and/or observed UI, not exhaustive validation. **Partial:** narrower workflow/material limits. **Missing:** no exposed implementation found. Native column describes current source; installed measurements remain 0.9.0. Browser is source-only. Planned entries do not count as implemented.

| Category | SwiftLeaf | Native PDFCraft | Browser PDFCraft | Qualification |
|---|---|---|---|---|
| View/search/select/copy/print | Have | Have | Have | Simple desktop rendering observed |
| Continuous/single/facing pages, zoom, fullscreen | Have | Have | Have | Smoothness unmeasured |
| Tabs/bookmarks/thumbnails/history | Have | Have | Partial | Browser toolkit workflow differs |
| EPUB/MOBI/CBZ/CBR/XPS/DjVu/CHM/images | Have | Partial | Partial | Import/conversion is not general ebook reading |
| Read aloud/reading bar/autoscroll | Have | Missing/partial | Unverified | Native speech documented as gap |
| Highlight/underline/strikeout/notes/ink/shapes | Have | Have | Have | Core annotation not central SwiftLeaf deficit |
| Stamps/attachments | Have | Have | Have | Native catalog sometimes lags handlers |
| Comments/replies | Have | Have | Partial | Not cloud collaboration |
| Comment import/export/summaries | Missing | Have | Partial | Native explicit workflows |
| Add text/images | Have | Have | Have | Overlays differ from original edits |
| Edit existing text | Partial | Partial | Partial | SwiftLeaf Windows-1252 limit; native font/grouping limits; web primarily overlays |
| Existing-image/multi-object edits | Partial | Have/partial | Partial | Complex layout not certified |
| Links | Have | Have | Partial | Native bulk URL-link creation/removal |
| Fill AcroForms | Have | Have | Have | Not full scripting/XFA parity |
| Create form fields | Partial | Have/partial | Partial | SwiftLeaf five basic types |
| Field detection/data exchange/calculation order | Missing | Have/partial | Partial | Native scripting still limited |
| OCR to text | Have/partial | Have/partial | Have/partial | Windows language packs vs English native model vs web Tesseract |
| Searchable OCR text layer | **Missing** | **Have** | **Have** | SwiftLeaf plain text export only |
| Scanner acquisition | Missing | Missing | Unverified | Scan label is not scanner support |
| Merge/split/extract/insert/rotate/reorder/delete | Have | Have | Have | Basic assembly exists |
| Preserve imported interactive forms/comments | **Partial** | Have, source/test claims | Unverified | SwiftLeaf bakes imported documents |
| Crop/page boxes/headers/footers/watermarks/numbering | Partial | Have | Have | View trim is not production tooling |
| PDF to images/plain text | Have | Have | Have | Formats differ |
| PDF to DOCX/PPTX/HTML/RTF | **Missing** | **Partial** | **Partial** | Exports exist, exact layout not guaranteed |
| PDF to Excel/table extraction | Missing | Missing, XLSX planned | Have/partial | Web broader here |
| Office to PDF | Missing | Missing, sidecar/provider roadmap | Have/partial | Web LibreOffice dependency; not tested |
| Images/supported documents to PDF | Have/partial | Have/partial | Have | Generic Create PDF does not imply Office |
| Compress/optimize | Have | Have | Have | SwiftLeaf real MuPDF cleaning/resampling |
| Password encrypt/decrypt | Have | Have | Have | Certificate encryption separate |
| Draw/type/image signature | Have | Have | Have | Not cryptographic signing |
| Certificate signing | Have/partial | Have/partial | Have/partial | Advanced trust/timestamps differ |
| Mark/apply permanent redaction | Have | Have | Have/partial | SwiftLeaf MuPDF redaction, not just black rectangle |
| Find-and-redact/hidden-data sanitation | Missing | Have | Have | Saved-file forensics still needed |
| Flatten comments/forms | Have | Have | Have | Intentional flatten differs from merge side effect |
| Compare PDFs | Missing | Have/partial | Have | Native scanned/filter limitations |
| Distance/perimeter/area/scale measurements | Partial | Have | Unverified | SwiftLeaf cursor coordinates narrower |
| Batch workflows/automation | Partial | Have | Have/partial | Printing CLI not JSON CLI/MCP parity |
| PDF/A | Missing | Partial | Partial | Generation not independent conformance validation |
| PDF/X/PDF/UA/full preflight | Missing | Missing/partial | Missing/unverified | Many native entries planned |
| Accessibility checking/tag authoring | Missing | Partial | Unverified | Separate from application accessibility |
| Coherent undo/autosave/crash recovery | Partial | Have | Unverified | SwiftLeaf separate page/annotation undo |
| Cross-platform releases | Windows | Windows/macOS/Linux/FreeBSD/web | Browser; Tauri source | Experimental SwiftLeaf ng not release parity |

## Tool inventories and limitations

### Native PDFCraft

Static extraction found **204 registered command entries**, including file/view/menu actions, not 204 independent PDF tools. The catalog exposes these families:

- **Export:** DOCX/PPTX/PNG/extracted images/HTML/RTF/text; XLSX and PostScript/EPS planned.
- **Fill & Sign:** text/check/cross/dot/line/date/signature/initials and saved-signature management.
- **Edit:** original text/images, add text/image/link, URL links, headers/footers, watermarks/backgrounds, Bates numbering.
- **Create:** supported files, multiple/separate files, images, clipboard. **Combine:** multi-document assembly.
- **Organize:** insert/duplicate/reverse/move/crop/page boxes/rotate/delete/replace/extract/split/number.
- **Comments:** notes/text boxes/highlight/underline/strikeout/squiggle/ink/eraser/line/arrow/rectangle/oval/polygon/polyline/callout/caret/replacement marks/attachments, import/export/summarize/flatten.
- **OCR:** recognition/batch/camera enhancement; scanned enhancement/correction entries planned.
- **Protect:** password/permissions/security properties/hidden-information removal; certificate encryption planned.
- **Redact:** text/images/pages/find/properties/apply/clear/sanitize.
- **Compress:** reduced size/advanced optimization; space audit planned.
- **Forms:** detect/properties/text/checkbox/radio/combo/list/button/image/date/signature, tab/calculation order, data import/export, flatten.
- **Stamps; certificates:** signing/certification/validation/signature panel; timestamp catalog item planned despite lower-level timestamp code.
- **Print production:** largely planned output preview/preflight/color conversion/printer marks/hairlines.
- **Measure:** distance/perimeter/area/scale/snapping/export; geospatial planned.
- **Compare; guided actions:** comparison/action wizard/distribution/scan workflows; archive/make-accessible actions planned.
- **Accessibility:** checker/report/tags/alt text/autotag commands; full remediation incomplete.
- **Standards:** limited PDF/A; PDF/X/PDF/UA planned. **Search index:** planned.
- **JavaScript:** registered console/document-JS commands and scripting crates coexist with planned catalog labels; reachability/status inconsistent.
- **AI assistant:** optional provider, off by default, not a bundled offline model.

Some catalog statuses lag handlers, including edit/protect and stamp entries. Neither button presence nor roadmap label alone is decisive. Commands, handlers and relevant modules were checked.

Native OCR source explicitly limits its bundled alphabet to English/unaccented Latin and creates invisible PDF text. SwiftLeaf OCR docs explicitly say no searchable layer, scanner acquisition, layout preservation or language picker. It uses one installed Windows OCR language per job and reads saved document content.

SwiftLeaf form authoring supports text, checkbox, radio, dropdown and signature fields. Radio option count cannot change in properties. Advanced validation/scripts/calculations/XFA/pushbuttons and nested field hierarchy editing are outside scope. Existing-text replacement rejects unsupported characters before deleting source text but remains Windows-1252 based. Compression is genuinely implemented: cleaning plus medium/high resampling to 150/96 DPI with JPEG settings. Imported documents are baked before page grafting (`EngineMupdf.cpp` near 10218).

### Browser PDFCraft

README: **90+ tools**. Current component tree: **122 tool directories**. Format variants share machinery; these are not normalized capability counts.

- Assembly: merge/interleave/split/extract/delete/reverse/reorder/duplicate/blank pages/divide/N-up/grid/booklet/posterize/long-page combine.
- Editing/review: reader/annotation overlays/stamps/visual signatures/crop/bookmarks/TOC/numbering/watermark/header/footer/background and text color/invert/remove annotations/forms/layers/compare.
- Conversion/extraction: images/text/tables/JSON/DOCX/PPTX/Excel/Markdown/SVG; inputs include Office, images/PSD, text/JSON/Markdown, RTF/email, EPUB/MOBI/FB2/CBZ, DjVu/XPS.
- Scans/optimization: OCR/deskew/blank-page removal/compress/repair/linearize/rasterize/font outlines/grayscale/page dimensions/size fixes.
- Security: encrypt/decrypt/permissions/restrictions/redact/find-and-redact/sanitize/metadata removal/flatten/digital signing/validation.
- Packaging/workflows: attachments add/extract/edit, metadata view/edit, ZIP and workflows.

These are source capabilities, not certified output fidelity. This toolbox can complement SwiftLeaf. Its tool count does not belong to the installed Rust app.

## UI and usability

Actual application screenshots:

- [SwiftLeaf initial narrow window](comparison-assets/swiftleaf-start.jpg)
- [SwiftLeaf page 100](comparison-assets/swiftleaf-100-pages.jpg)
- [PDFCraft initial window](comparison-assets/pdfcraft-start.jpg)
- [PDFCraft benchmark view](comparison-assets/pdfcraft-100-pages.jpg)

Viewports differ: SwiftLeaf 569 × 746; PDFCraft 1198 × 726. These document states, not an equal-size visual contest. Occluded captures were replaced with foreground images. Indexed automation clicks sometimes failed (`coordinate input geometry is unavailable`; `element 43 has no cached bounds`); keyboard navigation continued. Those helper limitations are not attributed to the apps.

Both expose All tools/Read/Edit/Convert/E-Sign modes, a left catalog, command search and document controls. PDFCraft's colored icons, recommended tools and descriptive subworkflows make breadth easier to discover. Its community/Discord promotion uses substantial home space. Planned entries can overstate completeness.

SwiftLeaf's narrow initial window clipped welcome text and command search. Its sidebar was readable but left little document space, and the floating annotation strip occupied page area. Adaptive-panel work has not eliminated small-window issues. README still describes nine Foxit-style tabs although 0.4.1 has five modes: an onboarding/documentation mismatch.

**Keyboard:** SwiftLeaf has standard file/find/print actions, command palette, shortcuts help and page navigation; Ctrl+End worked. PDFCraft source includes Ctrl+K search commands, Ctrl+S save, Ctrl+Shift+S save-as, Ctrl+F find, Ctrl+G next match, Ctrl+1/2/0 view modes and Ctrl+Shift+N go-to-page. Ctrl+O worked in installed PDFCraft. Full keyboard-only editing was not tested.

**Accessibility:** both exposed named controls to Windows UI Automation. SwiftLeaf exposed benchmark page text; the captured PDFCraft canvas exposed page-number text rather than full document reading content. That favors SwiftLeaf's observed text exposure but is not screen-reader certification. SwiftLeaf 0.4.1 adds focus/high-contrast/toggle work; PDFCraft has egui accessibility and tests. NVDA/Narrator, high contrast, IME and multi-monitor DPI were not tested.

**Dark/reduced motion:** both implement light/dark themes. SwiftLeaf separately exposes page inversion/document colors. Native PDFCraft `motion.rs` follows system reduced motion with On/Off/System override, covering wheel/zoom easing. SwiftLeaf animation code honors Windows animation settings. Preferences were not toggled and dark-state screenshots were not collected.

**Localization:** SwiftLeaf registry has 73 language entries; native PDFCraft has 33 TSV catalogs plus English. Counts do not prove new-string coverage. PDFCraft documents font-delivery gaps, incomplete RTL mirroring and mixed-direction input limitations. SwiftLeaf's text-edit encoding limit likewise prevents equating many UI languages with multilingual editing.

## Privacy, platforms, licenses and maintenance

All three support local document processing. Basic desktop work needs no cloud upload. SwiftLeaf checks GitHub for updates and exposes external search/translation/AI actions: offline-capable is not network-silent. Native PDFCraft states no telemetry/account and supports optional providers; architecture describes update checks on request. Browser PDFCraft needs application/WASM/model assets loaded/cached/self-hosted for offline use. No packet capture: these are source statements, not verified network isolation.

SwiftLeaf releases here target Windows. Native PDFCraft distributes Windows/macOS/Linux/FreeBSD/web. Browser PDFCraft runs in browsers and has Tauri packaging source. Cross-platform teams have a strong reason to choose PDFCraft.

SwiftLeaf identifies GPLv3, BSD portions and MuPDF AGPLv3. Native workspace code is MIT OR Apache-2.0, with separate dependency/font/model notices and ArtCraft branding restrictions; OCR models are CC-BY-SA-4.0. Browser PDFCraft is AGPL-3.0 in repository metadata. Permissive native code does not eliminate asset obligations.

GitHub API snapshot, October 23:

| Repository | Created | Stars | Open issues + PRs combined | Last 100 commits' author-date span, UTC |
|---|---|---:|---:|---|
| SwiftLeaf | Oct 8, 2026 | 0 | 1 | Oct 15 22:38 → Oct 23 17:35 |
| Native PDFCraft | Sep 30, 2026 | 17,396 | 1,863 | Oct 22 15:26 → Oct 23 18:41 |
| Browser PDFCraft | Jan 3, 2026 | 9,640 | 103 | Jun 18 16:38 → Oct 23 07:53 |

Bounded samples, not full daily rates; author dates differ from merge dates. All three pushed October 23. Native v0.13.0 published 00:43 UTC; SwiftLeaf v0.4.1 at 17:40 UTC. Activity is not stability. SwiftLeaf's issue-only query returned none; a tiny audience can explain that better than superior quality.

Recent native reports: [rotated form text](https://github.com/storytold/pdfcraft/issues/4296), [form dialog branching](https://github.com/storytold/pdfcraft/issues/4291), [compression requests](https://github.com/storytold/pdfcraft/issues/4283). Browser reports: [movable signatures](https://github.com/PDFCraftTool/pdfcraft/issues/244), [Chinese support](https://github.com/PDFCraftTool/pdfcraft/issues/243), [merge selection](https://github.com/PDFCraftTool/pdfcraft/issues/242). Not reproduced here; feature requests do not negate existing implementations.

SwiftLeaf's latest three queried CI runs succeeded, including HEAD/v0.4.1 work. CI builds release/debug and runs unit/smoke checks with known-failure handling. Changelog records six carried known smoke failures in 0.3.4: green does not mean zero defects. Native PDFCraft documents independent qpdf checks, many tests and recovery, but openly records rendering/editing/Office/standards gaps. Some roadmap text lags code. No local builds or cloned test suites ran.

## Wins, losses and top gaps

**SwiftLeaf wins:** tiny portable executable; much lower observed memory; mature reading foundation; broad ebook/comic/document formats; useful real annotations/page editing/redaction/signatures; Windows OCR language-pack potential; reading conveniences and observed accessible PDF text.

**SwiftLeaf loses:** searchable OCR, Office export, rich forms/data exchange, batch/compare and production tools; interactive merge preservation; Unicode/font editing; unified recovery/undo; signed distribution and multiple platforms. Similar sidebars do not mean equivalent workflows.

PDFCraft costs more resources and retains fidelity gaps, narrow bundled OCR languages and unfinished promises. Neither cheerleading for SwiftLeaf nor calling PDFCraft perfect fits the evidence.

### Top 10 SwiftLeaf gaps, ranked by user impact

| Rank | Gap | Practical completion criterion |
|---:|---|---|
| 1 | Editing reliability, unified undo and crash recovery | Consistent page/text/annotation history, atomic saves, recoverable unsaved work |
| 2 | Existing-text Unicode/font fidelity | Preserve styles, embed supported replacements, safely reject unsupported cases; verify saved renders/extraction |
| 3 | Searchable OCR | Aligned invisible layer, language selection and save/reopen search tests |
| 4 | Lossless assembly | Preserve imported fields/comments/links/bookmarks/attachments instead of implicit baking |
| 5 | Word/PowerPoint and broader conversion | Useful explicitly limited export before layout-perfect promises |
| 6 | Advanced forms/data interchange | Detection, more types, calculations/validation, tab order, data import/export |
| 7 | Batch workflows and compare | Previewable repeated OCR/compress/redact/extract and useful document differences |
| 8 | Search redaction and sanitation | Repeated-identifier and hidden-data removal validated on saved objects/text |
| 9 | Trusted distribution and broader deployment | Sign releases and strengthen update provenance; cross-platform is a larger separate investment |
| 10 | Production polish/discoverability | Headers/footers/watermarks/Bates, PDF/A validation, small-window layout, accurate docs and accessibility completion |

Ranks are judgments for likely Windows reader/editor users. Multinational/macOS/Linux teams would rank platforms/localization higher. Document integrity should precede copying every production-tool button.

## Decision

Choose **SwiftLeaf** for compact Windows reading, non-PDF formats, comments, modest page work and straightforward edits. Choose **native PDFCraft** for searchable English OCR, richer production/forms/exports, automation, compare and multiple operating systems, while validating difficult outputs. Treat **browser PDFCraft** as a separate specialist toolbox.

SwiftLeaf does **not pale as a reader**. It **does trail substantially as a general PDF workbench**. Its defensible position is a lightweight Windows niche, not feature-equivalent PDFCraft replacement.

## Evidence and reproducibility

Primary links:

- [SwiftLeaf 0.4.1 release](https://github.com/TheFunzzies/swiftleaf-pdf/releases/tag/v0.4.1).
- [PDFCraft 0.9.0 release](https://github.com/storytold/pdfcraft/releases/tag/v0.9.0), [0.13.0 release](https://github.com/storytold/pdfcraft/releases/tag/v0.13.0).
- Native pinned [architecture](https://github.com/storytold/pdfcraft/blob/c9ac28a04843141f2261ade4b9a19d51aed6f182/docs/architecture.md), [gaps](https://github.com/storytold/pdfcraft/blob/c9ac28a04843141f2261ade4b9a19d51aed6f182/docs/gaps.md), [command registry](https://github.com/storytold/pdfcraft/blob/c9ac28a04843141f2261ade4b9a19d51aed6f182/crates/engine/src/commands.rs), [tool catalog](https://github.com/storytold/pdfcraft/blob/c9ac28a04843141f2261ade4b9a19d51aed6f182/crates/engine/src/catalog.rs), [format limitations](https://github.com/storytold/pdfcraft/blob/c9ac28a04843141f2261ade4b9a19d51aed6f182/docs/file-format-parity.md).
- [Browser source snapshot](https://github.com/PDFCraftTool/pdfcraft/tree/53e469252b616aa0ac654efa267d2e92ac821866).

Local SwiftLeaf evidence: `src/shared/Commands.h` (actual location), `src/Ribbon.cpp`, `src/ToolsPanel.cpp`, `src/PdfTools.cpp`, `src/shared/EngineMupdf.cpp` (encoding near 9750; import baking near 10218), `src/UpdateCheck.cpp`, `src/shared/TranslationLangs.cpp`, `docs/ocr.md`, `docs/form-authoring.md`, README/CHANGELOG and `.github/workflows/build.yml`. Native evidence: engine commands/catalog and UI handlers, OCR/export/optimize/sign modules, `motion.rs`, architecture/gaps/editing/localization/format documents. Browser evidence: package manifest, README and tool components.

Useful measurement reproduction: query release asset sizes with `gh release view TAG --repo OWNER/REPO --json assets`; sum installed file Length values recursively; inspect version resources and Get-AuthenticodeSignature; sample WorkingSet64/PrivateMemorySize64 after waiting for the same UI state. Repeat starts and loaded-document samples with identical view sizes for publication-quality numbers. Do not use the debug SwiftLeaf binary as a release-size proxy.

Progress was saved during research. No commit/push, application upgrade, security-setting change or user-document edit occurred. Remaining empirical gaps: repeatable equal-window cold/warm starts, first-page render timing, FPS/latency, image-heavy/large PDFs, fidelity corpus, editing round trips, OCR accuracy, signing trust and redaction forensics. Those limits prevent broad speed, safety or fidelity guarantees; they do not negate measured footprint or source-established gaps.
