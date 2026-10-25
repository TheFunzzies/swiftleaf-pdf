/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

// Lucide icons (https://lucide.dev, ISC license, see THIRD_PARTY.md), the set
// PDFCraft uses. Generated from the upstream SVGs; stroke-width is 1.75.

#include "base/Base.h"
#include "LucideIcons.h"

#define LUCIDE_SVG(body)                                                                                        \
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" " \
    "stroke=\"currentColor\" stroke-width=\"1.75\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" body "</svg>"

// file-output
const char* gLucideFileOutput = LUCIDE_SVG(
    R"(<path d="M4.226 20.925A2 2 0 0 0 6 22h12a2 2 0 0 0 2-2V8a2.4 2.4 0 0 0-.706-1.706l-3.588-3.588A2.4 2.4 0 0 0 14 2H6a2 2 0 0 0-2 2v3.127" /><path d="M14 2v5a1 1 0 0 0 1 1h5" /><path d="m5 11-3 3" /><path d="m5 17-3-3h10" />)");
// pen-tool
const char* gLucidePenTool = LUCIDE_SVG(
    R"(<path d="m18 13-.654-4.736a4 4 0 00-2.756-3.266l-9.163-2.9a2 2 0 00-2.017.493l-.82.819a2 2 0 00-.492 2.017l2.9 9.163a4 4 0 003.266 2.756L13 18" /><path d="M18.648 12.352a1.205 1.205 0 011.704 0l1.296 1.296a1.205 1.205 0 010 1.704l-6.296 6.296a1.205 1.205 0 01-1.704 0l-1.296-1.296a1.205 1.205 0 010-1.704z" /><path d="m3 3 6.586 6.586" /><circle cx="11" cy="11" r="2" />)");
// file-pen
const char* gLucideFilePen = LUCIDE_SVG(
    R"(<path d="M12.659 22H18a2 2 0 0 0 2-2V8a2.4 2.4 0 0 0-.706-1.706l-3.588-3.588A2.4 2.4 0 0 0 14 2H6a2 2 0 0 0-2 2v9.34" /><path d="M14 2v5a1 1 0 0 0 1 1h5" /><path d="M10.378 12.622a1 1 0 0 1 3 3.003L8.36 20.637a2 2 0 0 1-.854.506l-2.867.837a.5.5 0 0 1-.62-.62l.836-2.869a2 2 0 0 1 .506-.853z" />)");
// file-plus-2
const char* gLucideFilePlus = LUCIDE_SVG(
    R"(<path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z" /><path d="M14 2v5a1 1 0 0 0 1 1h5" /><path d="M9 15h6" /><path d="M12 18v-6" />)");
// combine
const char* gLucideCombine = LUCIDE_SVG(
    R"(<path d="M14 3a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1" /><path d="M19 3a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1" /><path d="m7 15 3 3" /><path d="m7 21 3-3H5a2 2 0 0 1-2-2v-2" /><rect x="14" y="14" width="7" height="7" rx="1" /><rect x="3" y="3" width="7" height="7" rx="1" />)");
// merge
const char* gLucideMerge =
    LUCIDE_SVG(R"(<path d="m8 6 4-4 4 4" /><path d="M12 2v10.3a4 4 0 0 1-1.172 2.872L4 22" /><path d="m20 22-5-5" />)");
// layout-grid
const char* gLucideLayoutGrid = LUCIDE_SVG(
    R"(<rect width="7" height="7" x="3" y="3" rx="1" /><rect width="7" height="7" x="14" y="3" rx="1" /><rect width="7" height="7" x="14" y="14" rx="1" /><rect width="7" height="7" x="3" y="14" rx="1" />)");
// message-square-text
const char* gLucideMessageSquareText = LUCIDE_SVG(
    R"(<path d="M22 17a2 2 0 0 1-2 2H6.828a2 2 0 0 0-1.414.586l-2.202 2.202A.71.71 0 0 1 2 21.286V5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2z" /><path d="M7 11h10" /><path d="M7 15h6" /><path d="M7 7h8" />)");
// scan-text
const char* gLucideScanText = LUCIDE_SVG(
    R"(<path d="M3 7V5a2 2 0 0 1 2-2h2" /><path d="M17 3h2a2 2 0 0 1 2 2v2" /><path d="M21 17v2a2 2 0 0 1-2 2h-2" /><path d="M7 21H5a2 2 0 0 1-2-2v-2" /><path d="M7 8h8" /><path d="M7 12h10" /><path d="M7 16h6" />)");
// shield
const char* gLucideShield = LUCIDE_SVG(
    R"(<path d="M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z" />)");
// rectangle-horizontal
const char* gLucideRectangleHorizontal = LUCIDE_SVG(R"(<rect width="20" height="12" x="2" y="6" rx="2" />)");
// file-archive
const char* gLucideFileArchive = LUCIDE_SVG(
    R"(<path d="M13.659 22H18a2 2 0 0 0 2-2V8a2.4 2.4 0 0 0-.706-1.706l-3.588-3.588A2.4 2.4 0 0 0 14 2H6a2 2 0 0 0-2 2v11.5" /><path d="M14 2v5a1 1 0 0 0 1 1h5" /><path d="M8 12v-1" /><path d="M8 18v-2" /><path d="M8 7V6" /><circle cx="8" cy="20" r="2" />)");
// text-cursor-input
const char* gLucideTextCursorInput = LUCIDE_SVG(
    R"(<path d="M12 20h-1a2 2 0 0 1-2-2 2 2 0 0 1-2 2H6" /><path d="M13 8h7a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2h-7" /><path d="M5 16H4a2 2 0 0 1-2-2v-4a2 2 0 0 1 2-2h1" /><path d="M6 4h1a2 2 0 0 1 2 2 2 2 0 0 1 2-2h1" /><path d="M9 6v12" />)");
// stamp
const char* gLucideStamp = LUCIDE_SVG(
    R"(<path d="M14 13V8.5C14 7 15 7 15 5a3 3 0 0 0-6 0c0 2 1 2 1 3.5V13" /><path d="M20 15.5a2.5 2.5 0 0 0-2.5-2.5h-11A2.5 2.5 0 0 0 4 15.5V17a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1z" /><path d="M5 22h14" />)");
// badge-check
const char* gLucideBadgeCheck = LUCIDE_SVG(
    R"(<path d="M3.85 8.62a4 4 0 0 1 4.78-4.77 4 4 0 0 1 6.74 0 4 4 0 0 1 4.78 4.78 4 4 0 0 1 0 6.74 4 4 0 0 1-4.77 4.78 4 4 0 0 1-6.75 0 4 4 0 0 1-4.78-4.77 4 4 0 0 1 0-6.76Z" /><path d="m16 9-5.5 5.5L8 12" />)");
// check
const char* gLucideCheck = LUCIDE_SVG(R"(<path d="M20 6 9 17l-5-5" />)");
// star
const char* gLucideStar = LUCIDE_SVG(
    R"(<path d="m12 3 2.7 5.47 6.03.88-4.36 4.25 1.03 6.01L12 16.77l-5.4 2.84 1.03-6.01L3.27 9.35l6.03-.88L12 3z" />)");
// x
const char* gLucideX = LUCIDE_SVG(R"(<path d="M18 6 6 18" /><path d="m6 6 12 12" />)");
// panel-left
const char* gLucidePanelLeft = LUCIDE_SVG(R"(<rect width="18" height="18" x="3" y="3" rx="2" /><path d="M9 3v18" />)");
// search
const char* gLucideSearch = LUCIDE_SVG(R"(<path d="m21 21-4.34-4.34" /><circle cx="11" cy="11" r="8" />)");
// info
const char* gLucideInfo =
    LUCIDE_SVG(R"(<circle cx="12" cy="12" r="10" /><path d="M12 16v-4" /><path d="M12 8h.01" />)");
// save
const char* gLucideSave = LUCIDE_SVG(
    R"(<path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z" /><path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7" /><path d="M7 3v4a1 1 0 0 0 1 1h7" />)");
// printer
const char* gLucidePrinter = LUCIDE_SVG(
    R"(<path d="M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2" /><path d="M6 9V3a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v6" /><rect x="6" y="14" width="12" height="8" rx="1" />)");
// message-square
const char* gLucideMessageSquare = LUCIDE_SVG(
    R"(<path d="M22 17a2 2 0 0 1-2 2H6.828a2 2 0 0 0-1.414.586l-2.202 2.202A.71.71 0 0 1 2 21.286V5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2z" />)");
// bookmark
const char* gLucideBookmark = LUCIDE_SVG(
    R"(<path d="M17 3a2 2 0 0 1 2 2v15a1 1 0 0 1-1.496.868l-4.512-2.578a2 2 0 0 0-1.984 0l-4.512 2.578A1 1 0 0 1 5 20V5a2 2 0 0 1 2-2z" />)");
// copy
const char* gLucideCopy = LUCIDE_SVG(
    R"(<rect width="14" height="14" x="8" y="8" rx="2" ry="2" /><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2" />)");
// layers
const char* gLucideLayers = LUCIDE_SVG(
    R"(<path d="M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83z" /><path d="M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12" /><path d="M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17" />)");
// paperclip
const char* gLucidePaperclip = LUCIDE_SVG(
    R"(<path d="m16 6-8.414 8.586a2 2 0 0 0 2.829 2.829l8.414-8.586a4 4 0 1 0-5.657-5.657l-8.379 8.551a6 6 0 1 0 8.485 8.485l8.379-8.551" />)");
// mouse-pointer-2
const char* gLucideMousePointer = LUCIDE_SVG(
    R"(<path d="M4.037 4.688a.495.495 0 0 1 .651-.651l16 6.5a.5.5 0 0 1-.063.947l-6.124 1.58a2 2 0 0 0-1.438 1.435l-1.579 6.126a.5.5 0 0 1-.947.063z" />)");
// hand
const char* gLucideHand = LUCIDE_SVG(
    R"(<path d="M18 11V6a2 2 0 0 0-2-2a2 2 0 0 0-2 2" /><path d="M14 10V4a2 2 0 0 0-2-2a2 2 0 0 0-2 2v2" /><path d="M10 10.5V6a2 2 0 0 0-2-2a2 2 0 0 0-2 2v8" /><path d="M18 8a2 2 0 1 1 4 0v6a8 8 0 0 1-8 8h-2c-2.8 0-4.5-.86-5.99-2.34l-3.6-3.6a2 2 0 0 1 2.83-2.82L7 15" />)");
// highlighter
const char* gLucideHighlighter = LUCIDE_SVG(
    R"(<path d="m9 11-6 6v3h9l3-3" /><path d="m22 12-4.6 4.6a2 2 0 0 1-2.8 0l-5.2-5.2a2 2 0 0 1 0-2.8L14 4" />)");
// pencil
const char* gLucidePencil = LUCIDE_SVG(
    R"(<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z" /><path d="m15 5 4 4" />)");
// type
const char* gLucideType =
    LUCIDE_SVG(R"(<path d="M12 4v16" /><path d="M4 7V5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v2" /><path d="M9 20h6" />)");
// pen-line
const char* gLucidePenLine = LUCIDE_SVG(
    R"(<path d="M13 21h8" /><path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z" />)");
// sticky-note
const char* gLucideStickyNote = LUCIDE_SVG(
    R"(<path d="M21 9a2.4 2.4 0 0 0-.706-1.706l-3.588-3.588A2.4 2.4 0 0 0 15 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2z" /><path d="M15 3v5a1 1 0 0 0 1 1h5" />)");
// underline
const char* gLucideUnderline =
    LUCIDE_SVG(R"(<path d="M6 4v6a6 6 0 0 0 12 0V4" /><line x1="4" x2="20" y1="20" y2="20" />)");
// signature
const char* gLucideSignature = LUCIDE_SVG(
    R"(<path d="m21 17-2.156-1.868A.5.5 0 0 0 18 15.5v.5a1 1 0 0 1-1 1h-2a1 1 0 0 1-1-1c0-2.545-3.991-3.97-8.5-4a1 1 0 0 0 0 5c4.153 0 4.745-11.295 5.708-13.5a2.5 2.5 0 1 1 3.31 3.284" /><path d="M3 21h18" />)");
// ruler
const char* gLucideRuler = LUCIDE_SVG(
    R"(<path d="M21.3 15.3a2.4 2.4 0 0 1 0 3.4l-2.6 2.6a2.4 2.4 0 0 1-3.4 0L2.7 8.7a2.41 2.41 0 0 1 0-3.4l2.6-2.6a2.41 2.41 0 0 1 3.4 0Z" /><path d="m14.5 12.5 2-2" /><path d="m11.5 9.5 2-2" /><path d="m8.5 6.5 2-2" /><path d="m17.5 15.5 2-2" />)");
