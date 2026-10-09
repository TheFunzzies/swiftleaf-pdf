/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "gui/Dpi.h"
#include "base/Pixmap.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"

#include "Commands.h"
#include "SvgIcons.h"
#include "Theme.h"
#include "Translations.h"
#include "Material.h"
#include "Ribbon.h"

//--- icons the classic toolbar doesn't have (Tabler icons, MIT license)

#define TABLER_SVG(body)                                                                                            \
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" stroke-width=\"1\" " \
    "stroke=\"currentColor\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" body "</svg>"

// hand-stop
static const char* gIconHand = TABLER_SVG(
    R"(<path d="M8 13v-7.5a1.5 1.5 0 0 1 3 0v6.5" /><path d="M11 5.5v-2a1.5 1.5 0 1 1 3 0v8.5" />)"
    R"(<path d="M14 5.5a1.5 1.5 0 0 1 3 0v6.5" />)"
    R"(<path d="M17 7.5a1.5 1.5 0 0 1 3 0v8.5a6 6 0 0 1 -6 6h-2h.208a6 6 0 0 1 -5.012 -2.7l-.196 -.3c-.312 -.479 -1.407 -2.388 -3.286 -5.728a1.5 1.5 0 0 1 .536 -2.022a1.867 1.867 0 0 1 2.28 .28l1.47 1.47" />)");
// layout-grid
static const char* gIconThumbnails = TABLER_SVG(
    R"(<rect x="4" y="4" width="6" height="6" rx="1" /><rect x="14" y="4" width="6" height="6" rx="1" />)"
    R"(<rect x="4" y="14" width="6" height="6" rx="1" /><rect x="14" y="14" width="6" height="6" rx="1" />)");
// file-plus
static const char* gIconPageInsert = TABLER_SVG(
    R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
    R"(<path d="M12 11l0 6" /><path d="M9 14l6 0" />)");
// file-minus
static const char* gIconPageDelete = TABLER_SVG(
    R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
    R"(<path d="M9 14l6 0" />)");
// files-off-ish: two pages with a minus
static const char* gIconPageDeleteMany = TABLER_SVG(
    R"(<path d="M15 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M18 17h-7a2 2 0 0 1 -2 -2v-10a2 2 0 0 1 2 -2h4l5 5v7a2 2 0 0 1 -2 2z" />)"
    R"(<path d="M16 17v2a2 2 0 0 1 -2 2h-7a2 2 0 0 1 -2 -2v-10a2 2 0 0 1 2 -2h2" /><path d="M12 11h5" />)");
// file-arrow-up-ish: a page with an arrow up
static const char* gIconMoveUp = TABLER_SVG(
    R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
    R"(<path d="M12 17v-6" /><path d="M9.5 13.5l2.5 -2.5l2.5 2.5" />)");
static const char* gIconMoveDown = TABLER_SVG(
    R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
    R"(<path d="M12 11v6" /><path d="M9.5 14.5l2.5 2.5l2.5 -2.5" />)");
// file-export
static const char* gIconPageExtract = TABLER_SVG(
    R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M11.5 21h-4.5a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v5m-5 6h7m-3 -3l3 3l-3 3" />)");
// file-import
static const char* gIconPageImport = TABLER_SVG(
    R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M5 13v-8a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2h-5.5m-9.5 -2h7m-3 -3l3 3l-3 3" />)");
// files
static const char* gIconMerge = TABLER_SVG(
    R"(<path d="M15 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M18 17h-7a2 2 0 0 1 -2 -2v-10a2 2 0 0 1 2 -2h4l5 5v7a2 2 0 0 1 -2 2z" />)"
    R"(<path d="M16 17v2a2 2 0 0 1 -2 2h-7a2 2 0 0 1 -2 -2v-10a2 2 0 0 1 2 -2h2" />)");
// arrows-split
static const char* gIconSplit = TABLER_SVG(
    R"(<path d="M21 17h-8l-3.5 -5h-6.5" /><path d="M21 7h-8l-3.495 5" />)"
    R"(<path d="M18 10l3 -3l-3 -3" /><path d="M18 20l3 -3l-3 -3" />)");
// file-zip
static const char* gIconCompress = TABLER_SVG(
    R"(<path d="M6 20.735a2 2 0 0 1 -1 -1.735v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2h-1" />)"
    R"(<path d="M11 17a2 2 0 0 1 2 2v2a1 1 0 0 1 -1 1h-2a1 1 0 0 1 -1 -1v-2a2 2 0 0 1 2 -2z" />)"
    R"(<path d="M11 5l-1 0" /><path d="M13 7l-1 0" /><path d="M11 9l-1 0" /><path d="M13 11l-1 0" />)"
    R"(<path d="M11 13l-1 0" /><path d="M13 15l-1 0" />)");
// photo
static const char* gIconImage = TABLER_SVG(
    R"(<path d="M15 8h.01" />)"
    R"(<path d="M3 6a3 3 0 0 1 3 -3h12a3 3 0 0 1 3 3v12a3 3 0 0 1 -3 3h-12a3 3 0 0 1 -3 -3v-12z" />)"
    R"(<path d="M3 16l5 -5c.928 -.893 2.072 -.893 3 0l5 5" /><path d="M14 14l1 -1c.928 -.893 2.072 -.893 3 0l3 3" />)");
// photo-down
static const char* gIconExportImages = TABLER_SVG(
    R"(<path d="M15 8h.01" /><path d="M12.5 21h-6.5a3 3 0 0 1 -3 -3v-12a3 3 0 0 1 3 -3h12a3 3 0 0 1 3 3v6.5" />)"
    R"(<path d="M3 16l5 -5c.928 -.893 2.072 -.893 3 0l4 4" /><path d="M14 14l1 -1c.653 -.629 1.413 -.815 2.13 -.559" />)"
    R"(<path d="M19 16v6" /><path d="M22 19l-3 3l-3 -3" />)");
// clipboard
static const char* gIconClipboard = TABLER_SVG(
    R"(<path d="M9 5h-2a2 2 0 0 0 -2 2v12a2 2 0 0 0 2 2h10a2 2 0 0 0 2 -2v-12a2 2 0 0 0 -2 -2h-2" />)"
    R"(<rect x="9" y="3" width="6" height="4" rx="2" />)");
// link
static const char* gIconLink = TABLER_SVG(
    R"(<path d="M9 15l6 -6" /><path d="M11 6l.463 -.536a5 5 0 0 1 7.071 7.072l-.534 .464" />)"
    R"(<path d="M13 18l-.397 .534a5.068 5.068 0 0 1 -7.127 0a4.972 4.972 0 0 1 0 -7.071l.524 -.463" />)");
// signature
static const char* gIconSignature = TABLER_SVG(
    R"(<path d="M3 17c3.333 -3.333 5 -6 5 -8c0 -3 -1 -3 -2 -3s-2.032 1.085 -2 3c.034 2.048 1.658 4.877 2.5 6c1.5 2 2.5 2.5 3.5 1l2 -3c.333 2.667 1.333 4 3 4c.53 0 2.639 -2 3 -2c.517 0 1.517 .667 3 2" />)");
// certificate
static const char* gIconCertificate = TABLER_SVG(
    R"(<path d="M12 15a3 3 0 1 0 6 0a3 3 0 1 0 -6 0" /><path d="M13 17.5v4.5l2 -1.5l2 1.5v-4.5" />)"
    R"(<path d="M10 19h-5a2 2 0 0 1 -2 -2v-10c0 -1.1 .9 -2 2 -2h14a2 2 0 0 1 2 2v10a2 2 0 0 1 -1 1.73" />)"
    R"(<path d="M6 9l12 0" /><path d="M6 12l3 0" /><path d="M6 15l2 0" />)");
// forms
static const char* gIconForms = TABLER_SVG(
    R"(<path d="M12 3a3 3 0 0 0 -3 3v12a3 3 0 0 0 3 3" /><path d="M6 3a3 3 0 0 1 3 3v12a3 3 0 0 1 -3 3" />)"
    R"(<path d="M13 7h7a1 1 0 0 1 1 1v8a1 1 0 0 1 -1 1h-7" /><path d="M5 7h-1a1 1 0 0 0 -1 1v8a1 1 0 0 0 1 1h1" />)"
    R"(<path d="M17 12h.01" /><path d="M13 12h.01" />)");
// columns-2
static const char* gIconFacing =
    TABLER_SVG(R"(<rect x="3" y="3" width="18" height="18" rx="2" /><path d="M12 3v18" />)");
// book
static const char* gIconBook = TABLER_SVG(
    R"(<path d="M3 19a9 9 0 0 1 9 0a9 9 0 0 1 9 0" /><path d="M3 6a9 9 0 0 1 9 0a9 9 0 0 1 9 0" />)"
    R"(<path d="M3 6l0 13" /><path d="M12 6l0 13" /><path d="M21 6l0 13" />)");
// presentation
static const char* gIconPresentation = TABLER_SVG(
    R"(<path d="M3 4l18 0" /><path d="M4 4v10a2 2 0 0 0 2 2h12a2 2 0 0 0 2 -2v-10" />)"
    R"(<path d="M12 16l0 4" /><path d="M9 20l6 0" /><path d="M8 12l3 -3l2 2l3 -3" />)");
// maximize
static const char* gIconFullscreen = TABLER_SVG(
    R"(<path d="M4 8v-2a2 2 0 0 1 2 -2h2" /><path d="M4 16v2a2 2 0 0 0 2 2h2" />)"
    R"(<path d="M16 4h2a2 2 0 0 1 2 2v2" /><path d="M16 20h2a2 2 0 0 0 2 -2v-2" />)");
// arrow-autofit-width
static const char* gIconFitWidth = TABLER_SVG(
    R"(<path d="M4 12v-6a2 2 0 0 1 2 -2h12a2 2 0 0 1 2 2v6" /><path d="M10 18h-7" /><path d="M21 18h-7" />)"
    R"(<path d="M6 15l-3 3l3 3" /><path d="M18 15l3 3l-3 3" />)");
// arrow-autofit-height
static const char* gIconFitPage = TABLER_SVG(
    R"(<path d="M12 20h-6a2 2 0 0 1 -2 -2v-12a2 2 0 0 1 2 -2h6" /><path d="M18 4v17" />)"
    R"(<path d="M15 18l3 3l3 -3" /><path d="M15 7l3 -3l3 3" />)");
// a page with "1:1" on it
static const char* gIconActualSize = TABLER_SVG(
    R"(<rect x="3" y="5" width="18" height="14" rx="2" /><path d="M7 10l1.5 -1v6" /><path d="M15 10l1.5 -1v6" />)"
    R"(<path d="M12 10.5v.01" /><path d="M12 13.5v.01" />)");
// moon
static const char* gIconInvert =
    TABLER_SVG(R"(<path d="M12 3c.132 0 .263 0 .393 0a7.5 7.5 0 0 0 7.92 12.446a9 9 0 1 1 -8.313 -12.454z" />)");
// info-circle
static const char* gIconInfo =
    TABLER_SVG(R"(<path d="M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0" /><path d="M12 9h.01" /><path d="M11 12h1v4h1" />)");
// eye
static const char* gIconEye = TABLER_SVG(
    R"(<path d="M10 12a2 2 0 1 0 4 0a2 2 0 0 0 -4 0" />)"
    R"(<path d="M21 12c-2.4 4 -5.4 6 -9 6c-3.6 0 -6.6 -2 -9 -6c2.4 -4 5.4 -6 9 -6c3.6 0 6.6 2 9 6" />)");
// typography
static const char* gIconTypewriter = TABLER_SVG(
    R"(<path d="M4 20l3 0" /><path d="M14 20l7 0" /><path d="M6.9 15l6.9 0" /><path d="M10.2 6.3l5.8 13.7" />)"
    R"(<path d="M5 20l6 -16l2 0l7 16" />)");
// lock
static const char* gIconLock = TABLER_SVG(
    R"(<rect x="5" y="11" width="14" height="10" rx="2" /><path d="M11 16a1 1 0 1 0 2 0a1 1 0 0 0 -2 0" />)"
    R"(<path d="M8 11v-4a4 4 0 1 1 8 0v4" />)");
// lock-open
static const char* gIconUnlock = TABLER_SVG(
    R"(<rect x="5" y="11" width="14" height="10" rx="2" /><path d="M11 16a1 1 0 1 0 2 0a1 1 0 0 0 -2 0" />)"
    R"(<path d="M8 11v-5a4 4 0 0 1 8 0" />)");
// palette
static const char* gIconTheme = TABLER_SVG(
    R"(<path d="M12 21a9 9 0 0 1 0 -18c4.97 0 9 3.582 9 8c0 1.06 -.474 2.078 -1.318 2.828c-.844 .75 -1.989 1.172 -3.182 1.172h-2.5a2 2 0 0 0 -1 3.75a1.3 1.3 0 0 1 -1 2.25" />)"
    R"(<path d="M7.5 10.5a1 1 0 1 0 2 0a1 1 0 1 0 -2 0" /><path d="M11.5 7.5a1 1 0 1 0 2 0a1 1 0 1 0 -2 0" />)"
    R"(<path d="M15.5 10.5a1 1 0 1 0 2 0a1 1 0 1 0 -2 0" />)");
// file-text
static const char* gIconExtractText = TABLER_SVG(
    R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
    R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
    R"(<path d="M9 9l1 0" /><path d="M9 13l6 0" /><path d="M9 17l6 0" />)");
// list-details
static const char* gIconCommentList = TABLER_SVG(
    R"(<path d="M13 5h8" /><path d="M13 9h5" /><path d="M13 15h8" /><path d="M13 19h5" />)"
    R"(<rect x="3" y="4" width="6" height="6" rx="1" /><rect x="3" y="14" width="6" height="6" rx="1" />)");
// layout-sidebar
static const char* gIconBookmarks = TABLER_SVG(
    R"(<rect x="4" y="4" width="16" height="16" rx="2" /><path d="M9 4l0 16" />)");

// pointer
static const char* gIconPointer = TABLER_SVG(
    R"(<path d="M7.904 17.563a1.2 1.2 0 0 0 2.228 .308l2.09 -3.093l4.907 4.907a1.067 1.067 0 0 0 1.509 0l1.047 -1.047a1.067 1.067 0 0 0 0 -1.509l-4.907 -4.907l3.113 -2.09a1.2 1.2 0 0 0 -.309 -2.228l-13.582 -3.904l3.904 13.563z" />)");
// camera
static const char* gIconSnapshot = TABLER_SVG(
    R"(<path d="M5 7h1a2 2 0 0 0 2 -2a1 1 0 0 1 1 -1h6a1 1 0 0 1 1 1a2 2 0 0 0 2 2h1a2 2 0 0 1 2 2v9a2 2 0 0 1 -2 2h-14a2 2 0 0 1 -2 -2v-9a2 2 0 0 1 2 -2" />)"
    R"(<path d="M9 13a3 3 0 1 0 6 0a3 3 0 0 0 -6 0" />)");
// help-circle
static const char* gIconHelp = TABLER_SVG(
    R"(<path d="M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0" /><path d="M12 17l0 .01" />)"
    R"(<path d="M12 13.5a1.5 1.5 0 0 1 1 -1.5a2.6 2.6 0 1 0 -3 -4" />)");
// keyboard
static const char* gIconKeyboard = TABLER_SVG(
    R"(<path d="M2 8a2 2 0 0 1 2 -2h16a2 2 0 0 1 2 2v8a2 2 0 0 1 -2 2h-16a2 2 0 0 1 -2 -2l0 -8" />)"
    R"(<path d="M6 10l0 .01" /><path d="M10 10l0 .01" /><path d="M14 10l0 .01" /><path d="M18 10l0 .01" />)"
    R"(<path d="M6 14l0 .01" /><path d="M18 14l0 .01" /><path d="M10 14l4 .01" />)");
// world
static const char* gIconWorld = TABLER_SVG(
    R"(<path d="M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0" /><path d="M3.6 9h16.8" /><path d="M3.6 15h16.8" />)"
    R"(<path d="M11.5 3a17 17 0 0 0 0 18" /><path d="M12.5 3a17 17 0 0 1 0 18" />)");
// cloud-download
static const char* gIconUpdate = TABLER_SVG(
    R"(<path d="M19 18a3.5 3.5 0 0 0 0 -7h-1a5 4.5 0 0 0 -11 -2a4.6 4.4 0 0 0 -2.1 8.4" />)"
    R"(<path d="M12 13l0 9" /><path d="M9 19l3 3l3 -3" />)");
// settings
static const char* gIconSettings = TABLER_SVG(
    R"(<path d="M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 -2.924 1.756 -3.35 0a1.724 1.724 0 0 0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 -1.065 -2.572c-1.756 -.426 -1.756 -2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 -2.37c1 .608 2.296 .07 2.572 -1.065z" />)"
    R"(<path d="M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0" />)");
// logout
static const char* gIconExit = TABLER_SVG(
    R"(<path d="M14 8v-2a2 2 0 0 0 -2 -2h-7a2 2 0 0 0 -2 2v12a2 2 0 0 0 2 2h7a2 2 0 0 0 2 -2v-2" />)"
    R"(<path d="M9 12h12l-3 -3" /><path d="M18 15l3 -3" />)");
// stack
static const char* gIconFlatten = TABLER_SVG(
    R"(<path d="M12 4l-8 4l8 4l8 -4l-8 -4" /><path d="M4 12l8 4l8 -4" /><path d="M4 16l8 4l8 -4" />)");
// writing-sign
static const char* gIconCreateSignature = TABLER_SVG(
    R"(<path d="M3 19c3.333 -2 5 -4 5 -6c0 -3 -1 -3 -2 -3s-2.032 1.085 -2 3c.034 2.048 1.658 2.877 2.5 4c1.5 2 2.5 2.5 3.5 1c.667 -1 1.167 -1.833 1.5 -2.5c1 2.333 2.333 3.5 4 3.5h2.5" />)"
    R"(<path d="M20 17v-12c0 -1.121 -.879 -2 -2 -2s-2 .879 -2 2v12l2 2l2 -2z" /><path d="M16 7h4" />)");

//--- pages

// clang-format off
static const RibbonItemDef gEditItems[] = {
    {gIconEditAnnotations, CmdEditText, TrN("Edit Text"), TrN("Select text on the page, then click here to change it")},
    {gIconTypewriter, CmdCreateAnnotFreeText, TrN("Add Text"), TrN("Add a text box (typewriter)")},
    {gIconImage, CmdInsertImage, TrN("Image"), TrN("Insert an image from a file")},
    {gIconClipboard, CmdCreateAnnotImageFromClipboard, TrN("Paste Image"), TrN("Insert the image on the clipboard")},
    {gIconLink, CmdCreateAnnotLink, TrN("Link"), TrN("Add a link")},
    {nullptr, 0, {}},
    {gIconAnnotRedact, CmdCreateAnnotRedact, TrN("Redact"), TrN("Mark an area for redaction")},
    {gIconApplyRedactions, CmdApplyRedactions, TrN("Apply"), TrN("Apply redactions (permanently removes the content)")},
    {nullptr, 0, {}},
    {gIconUndo, CmdUndo, TrN("Undo")},
    {gIconRedo, CmdRedo, TrN("Redo")},
    {nullptr, 0, {}},
    {gIconCompress, CmdPdfCompress, TrN("Compress"), TrN("Save a smaller copy of the PDF")},
    {gIconExtractText, CmdDocumentExtractText, TrN("To Text"), TrN("Extract the text of the document")},
    {nullptr, 0, {}},
    {gIconSave, CmdSaveAnnotations, TrN("Save"), TrN("Save changes to the PDF")},
    {gIconSaveToNewFile, CmdSaveAnnotationsNewFile, TrN("Save Copy"), TrN("Save changes to a new PDF")},
};

static const RibbonItemDef gOrganizeItems[] = {
    {gIconThumbnails, CmdToggleThumbnails, TrN("Thumbnails"), TrN("Show page thumbnails")},
    {nullptr, 0, {}},
    {gIconPageInsert, CmdInsertBlankPage, TrN("Blank Page"), TrN("Insert a blank page after this one")},
    {gIconPageImport, CmdInsertPagesFromFile, TrN("Insert File"), TrN("Insert the pages of another PDF after this page")},
    {gIconPageDelete, CmdDeleteCurrentPage, TrN("Delete Page"), TrN("Delete this page")},
    {gIconPageDeleteMany, CmdPdfDeletePages, TrN("Delete..."), TrN("Delete a range of pages")},
    {nullptr, 0, {}},
    {gIconRotateLeft, CmdRotatePageLeft, TrN("Rotate Left"), TrN("Rotate this page left and save it")},
    {gIconRotateRight, CmdRotatePageRight, TrN("Rotate Right"), TrN("Rotate this page right and save it")},
    {gIconMoveUp, CmdMovePageUp, TrN("Move Up"), TrN("Move this page before the previous one")},
    {gIconMoveDown, CmdMovePageDown, TrN("Move Down"), TrN("Move this page after the next one")},
    {gIconUndo, CmdUndoPageChange, TrN("Undo"), TrN("Undo the last page change")},
    {nullptr, 0, {}},
    {gIconPageExtract, CmdPdfExtractPages, TrN("Extract"), TrN("Extract pages to a new PDF")},
    {gIconSplit, CmdSplitPdf, TrN("Split"), TrN("Split the PDF into several files")},
    {gIconMerge, CmdMergePDF, TrN("Merge"), TrN("Combine several PDF files into one")},
    {nullptr, 0, {}},
    {gIconExportImages, CmdConvertPdfToImages, TrN("To Images"), TrN("Save pages as images")},
    {gIconImage, CmdConvertImageToPdf, TrN("From Image"), TrN("Create a PDF from images")},
};

static const RibbonItemDef gFormsSignItems[] = {
    {gIconForms, CmdToggleHighlightFormFields, TrN("Highlight Fields"), TrN("Highlight form fields")},
    {gIconTypewriter, CmdCreateAnnotFreeText, TrN("Typewriter"), TrN("Type text anywhere on the page")},
    {nullptr, 0, {}},
    {gIconCreateSignature, CmdCreateSignature, TrN("Create Signature"), TrN("Draw, type or upload your signature")},
    {gIconSignature, CmdSignWithImage, TrN("Place Signature"), TrN("Place your signature on the page")},
    {gIconCertificate, CmdSignDocument, TrN("Digital ID"), TrN("Sign the document with a certificate")},
    {gIconAnnotStamp, CmdCreateAnnotStamp, TrN("Stamp")},
    {nullptr, 0, {}},
    {gIconSave, CmdSaveAnnotations, TrN("Save"), TrN("Save changes to the PDF")},
};

static const RibbonItemDef gViewItems[] = {
    {gIconLayoutSinglePage, CmdSinglePageView, TrN("Single Page")},
    {gIconFacing, CmdFacingView, TrN("Two Pages"), TrN("Facing pages")},
    {gIconBook, CmdBookView, TrN("Book"), TrN("Book view")},
    {gIconLayoutContinuous, CmdToggleContinuousView, TrN("Continuous"), TrN("Scroll pages continuously")},
    {nullptr, 0, {}},
    {gIconFitWidth, CmdZoomFitWidth, TrN("Fit Width")},
    {gIconFitPage, CmdZoomFitPage, TrN("Fit Page")},
    {gIconActualSize, CmdZoomActualSize, TrN("Actual Size")},
    {nullptr, 0, {}},
    {gIconBookmarks, CmdToggleBookmarks, TrN("Bookmarks"), TrN("Show bookmarks")},
    {gIconThumbnails, CmdToggleThumbnails, TrN("Thumbnails"), TrN("Show page thumbnails")},
    {gIconEye, CmdToggleShowAnnotations, TrN("Comments"), TrN("Show or hide comments")},
    {nullptr, 0, {}},
    {gIconFullscreen, CmdToggleFullscreen, TrN("Full Screen")},
    {gIconPresentation, CmdTogglePresentationMode, TrN("Present"), TrN("Presentation mode")},
    {gIconInvert, CmdInvertColors, TrN("Night Mode"), TrN("Invert page colors")},
    {gIconTheme, CmdToggleLightDarkTheme, TrN("Theme"), TrN("Switch between light and dark theme")},
    {gIconSpeak, CmdToggleReadAloud, TrN("Read Aloud")},
    {nullptr, 0, {}},
    {gIconNavigateBack, CmdNavigateBack, TrN("Back"), TrN("Go back to the previous view")},
    {gIconNavigateForward, CmdNavigateForward, TrN("Forward")},
    {gIconInfo, CmdProperties, TrN("Properties"), TrN("Document properties")},
};

static const RibbonItemDef gProtectItems[] = {
    {gIconLock, CmdPdfEncrypt, TrN("Protect"), TrN("Encrypt the PDF with a password")},
    {gIconUnlock, CmdPdfDecrypt, TrN("Unprotect"), TrN("Remove the password from the PDF")},
    {nullptr, 0, {}},
    {gIconAnnotRedact, CmdCreateAnnotRedact, TrN("Redact"), TrN("Mark an area for redaction")},
    {gIconApplyRedactions, CmdApplyRedactions, TrN("Apply"), TrN("Apply redactions (permanently removes the content)")},
    {nullptr, 0, {}},
    {gIconCertificate, CmdSignDocument, TrN("Digital ID"), TrN("Sign the document with a certificate")},
    {gIconFlatten, CmdPdfBake, TrN("Flatten"), TrN("Merge comments and form fields into the pages")},
    {nullptr, 0, {}},
    {gIconSave, CmdSaveAnnotations, TrN("Save"), TrN("Save changes to the PDF")},
};

static const RibbonItemDef gHelpItems[] = {
    {gIconHelp, CmdHelpOpenManual, TrN("Help"), TrN("Open the manual")},
    {gIconKeyboard, CmdHelpOpenKeyboardShortcuts, TrN("Shortcuts"), TrN("Keyboard shortcuts")},
    {gIconCommandPalette, CmdCommandPalette, TrN("Commands"), TrN("Find any command by name")},
    {nullptr, 0, {}},
    {gIconUpdate, CmdCheckUpdate, TrN("Update"), TrN("Check for a new version and install it")},
    {gIconWorld, CmdHelpVisitWebsite, TrN("Website"), TrN("Visit the Swiftleaf PDF website")},
    {gIconInfo, CmdHelpAbout, TrN("About")},
};

static const RibbonItemDef gFileItems[] = {
    {gIconFileOpen, CmdOpenFile, TrN("Open")},
    {gIconSave, CmdSaveAnnotations, TrN("Save"), TrN("Save changes to the PDF")},
    {gIconSaveToNewFile, CmdSaveAs, TrN("Save As")},
    {gIconPrint, CmdPrint, TrN("Print")},
    {gIconClose, CmdClose, TrN("Close"), TrN("Close the document")},
    {nullptr, 0, {}},
    {gIconInfo, CmdProperties, TrN("Properties"), TrN("Document properties")},
    {gIconSettings, CmdOptions, TrN("Settings")},
    {nullptr, 0, {}},
    {gIconExit, CmdExit, TrN("Exit")},
};
// clang-format on

static const Str gPageNames[] = {
    TrN("Home"), TrN("Comment"), TrN("Edit"),    TrN("Organize"), TrN("Fill & Sign"),
    TrN("View"), TrN("Protect"), TrN("Help"),    TrN("File"),
};
static_assert(dimof(gPageNames) == (int)RibbonPage::Count);

// the order of the tabs, like Foxit PDF Editor's
static const RibbonPage gTabOrder[] = {
    RibbonPage::File, RibbonPage::Home,      RibbonPage::Comment, RibbonPage::Edit, RibbonPage::Organize,
    RibbonPage::View, RibbonPage::FormsSign, RibbonPage::Protect, RibbonPage::Help,
};
static_assert(dimof(gTabOrder) == (int)RibbonPage::Count);

int RibbonPageCount() {
    return (int)RibbonPage::Count;
}

Str RibbonPageName(RibbonPage page) {
    return trans::GetTranslation(gPageNames[(int)page]);
}

RibbonPage RibbonPageAtTab(int tabPos) {
    return gTabOrder[tabPos];
}

void RibbonPageItems(RibbonPage page, const RibbonItemDef** items, int* count) {
    *items = nullptr;
    *count = 0;
    switch (page) {
        case RibbonPage::Edit:
            *items = gEditItems;
            *count = dimof(gEditItems);
            break;
        case RibbonPage::Organize:
            *items = gOrganizeItems;
            *count = dimof(gOrganizeItems);
            break;
        case RibbonPage::FormsSign:
            *items = gFormsSignItems;
            *count = dimof(gFormsSignItems);
            break;
        case RibbonPage::View:
            *items = gViewItems;
            *count = dimof(gViewItems);
            break;
        case RibbonPage::Protect:
            *items = gProtectItems;
            *count = dimof(gProtectItems);
            break;
        case RibbonPage::Help:
            *items = gHelpItems;
            *count = dimof(gHelpItems);
            break;
        case RibbonPage::File:
            *items = gFileItems;
            *count = dimof(gFileItems);
            break;
        default:
            break;
    }
}

bool RibbonPageEditsPdf(RibbonPage page) {
    return page == RibbonPage::Comment || page == RibbonPage::Edit || page == RibbonPage::FormsSign ||
           page == RibbonPage::Protect;
}

// labels for the buttons of the Home and Comment pages, which come from the
// toolbar's own tables (whose texts are long tooltips)
Str RibbonLabelForCmd(int cmdId) {
    Str s;
    switch (cmdId) {
        case CmdOpenFile:
            s = TrN("Open");
            break;
        case CmdPrint:
            s = TrN("Print");
            break;
        case CmdSaveAs:
            s = TrN("Save As");
            break;
        case CmdSaveAnnotations:
            s = TrN("Save");
            break;
        case CmdNavigateBack:
            s = TrN("Back");
            break;
        case CmdNavigateForward:
            s = TrN("Forward");
            break;
        case CmdToggleReadAloud:
            s = TrN("Read Aloud");
            break;
        case CmdToggleFreePan:
            s = TrN("Hand");
            break;
        case CmdZoomFitWidthAndContinuous:
            s = TrN("Fit Width");
            break;
        case CmdZoomFitPageAndSinglePage:
            s = TrN("Fit Page");
            break;
        case CmdRotateLeft:
            s = TrN("Rotate Left");
            break;
        case CmdRotateRight:
            s = TrN("Rotate Right");
            break;
        case CmdZoomOut:
            s = TrN("Zoom Out");
            break;
        case CmdZoomIn:
            s = TrN("Zoom In");
            break;
        case CmdFindFirst:
            s = TrN("Find");
            break;
        case CmdToggleEditPDF:
            s = TrN("Edit Mode");
            break;
        case CmdAnnotationHighlightBrush:
            s = TrN("Highlighter");
            break;
        case CmdCreateAnnotInk:
            s = TrN("Pencil");
            break;
        case CmdCreateAnnotHighlight:
            s = TrN("Highlight");
            break;
        case CmdCreateAnnotUnderline:
            s = TrN("Underline");
            break;
        case CmdCreateAnnotSquiggly:
            s = TrN("Squiggly");
            break;
        case CmdCreateAnnotStrikeOut:
            s = TrN("Strikeout");
            break;
        case CmdCreateAnnotText:
            s = TrN("Note");
            break;
        case CmdCreateAnnotFreeText:
            s = TrN("Typewriter");
            break;
        case CmdCreateAnnotLine:
            s = TrN("Line");
            break;
        case CmdCreateAnnotPolyLine:
            s = TrN("Polyline");
            break;
        case CmdCreateAnnotSquare:
            s = TrN("Rectangle");
            break;
        case CmdCreateAnnotCircle:
            s = TrN("Oval");
            break;
        case CmdCreateAnnotPolygon:
            s = TrN("Polygon");
            break;
        case CmdCreateAnnotRedact:
            s = TrN("Redact");
            break;
        case CmdApplyRedactions:
            s = TrN("Apply");
            break;
        case CmdCreateAnnotStamp:
            s = TrN("Stamp");
            break;
        case CmdCreateAnnotCaret:
            s = TrN("Caret");
            break;
        case CmdCreateAnnotFileAttachment:
            s = TrN("Attach");
            break;
        case CmdUndo:
            s = TrN("Undo");
            break;
        case CmdRedo:
            s = TrN("Redo");
            break;
        case CmdFindAnnotation:
            s = TrN("Comments");
            break;
        case CmdToolHand:
            s = TrN("Hand");
            break;
        case CmdToolSelect:
            s = TrN("Select");
            break;
        case CmdToolSnapshot:
            s = TrN("Snapshot");
            break;
        case CmdCopySelection:
            s = TrN("Copy");
            break;
        case CmdSignWithImage:
            s = TrN("Fill & Sign");
            break;
        default:
            return {};
    }
    return trans::GetTranslation(s);
}

const char* RibbonIconForCmd(int cmdId, const char* fallback) {
    switch (cmdId) {
        case CmdToggleFreePan:
        case CmdToolHand:
            return gIconHand;
        case CmdToolSelect:
            return gIconPointer;
        case CmdToolSnapshot:
            return gIconSnapshot;
        case CmdCopySelection:
            return gIconCopy;
        case CmdSignWithImage:
            return gIconSignature;
        case CmdZoomFitWidthAndContinuous:
            return gIconFitWidth;
        case CmdZoomFitPageAndSinglePage:
            return gIconFitPage;
        case CmdFindAnnotation:
            return gIconCommentList;
        case CmdCreateAnnotFreeText:
            return gIconTypewriter;
        case CmdSaveAs:
            return gIconSaveToNewFile;
        case CmdToggleBookmarks:
            return gIconBookmarks;
    }
    return fallback;
}

///--- colors: Material 3 roles (Material.h)

Color RibbonAccentColor() {
    return M3().primary;
}

// the ribbon, navigation rail and status bar share one tonal surface, set off
// from the document by outline-variant dividers
Color RibbonTabRowBgColor() {
    return M3().surfaceContainerLow;
}

Color RibbonPanelBgColor() {
    return M3().surfaceContainerLow;
}

Color RibbonEdgeColor() {
    return M3().outlineVariant;
}

Color RibbonSelectedBgColor() {
    return M3().secondaryContainer;
}

// Material icons are monochrome, in on-surface-variant
Color RibbonIconColor(int) {
    return M3().onSurfaceVariant;
}

int RibbonLargeIconSize() {
    return DpiScale(24);
}

int RibbonSmallIconSize() {
    return DpiScale(16);
}

Pixmap* RibbonIconPixmap(Str svg, int size, Color fg, Color bg) {
    if (str::IsEmptyOrWhiteSpace(svg)) {
        return nullptr;
    }
    TempStr heavier = str::ReplaceTemp(svg, StrL("stroke-width=\"1\""), StrL("stroke-width=\"1.6\""));
    return GetCachedPixmapForSvg(heavier, size, size, fg, bg);
}

//--- RibbonButton

constexpr int kRibbonBtnPadX = 6;
constexpr int kRibbonBtnPadY = 4;
constexpr int kRibbonBtnIconGap = 3;
constexpr int kRibbonBtnMinDx = 46;

RibbonButton::RibbonButton() {
    kind = kindVirtCtrlRibbonButton;
    padding = {DpiScale(kRibbonBtnPadY), DpiScale(kRibbonBtnPadX), DpiScale(kRibbonBtnPadY), DpiScale(kRibbonBtnPadX)};
}

RibbonButton::~RibbonButton() {
    str::Free(label);
}

void RibbonButton::SetLabel(Str s) {
    str::ReplaceWithCopy(&label, s);
}

Size RibbonButton::GetIdealSize() {
    int iconDx = pixmap ? pixmap->width : RibbonLargeIconSize();
    int dy = RibbonButtonDy(font);
    if (compact) {
        return {iconDx + DropdownDx() + padding.left + padding.right, dy};
    }
    int textDx = len(label) > 0 ? PlatformFontMeasureText(font, label).dx : 0;
    int dx = std::max(iconDx + DropdownDx(), textDx) + padding.left + padding.right;
    dx = std::max(dx, DpiScale(kRibbonBtnMinDx));
    return {dx, dy};
}

// Material: a toggled-on button sits on a secondary-container shape; hover and
// press lay the content color over it at 8% / 12% (state layers)
void RibbonButton::Paint(VirtPaintCtx& ctx) {
    bool enabled = IsEnabled();
    Rect r = ctx.bounds;
    int radius = DpiScale(12);
    const M3Scheme& m3 = M3();
    Color container = RibbonPanelBgColor();
    if (isSelected && enabled) {
        container = m3.secondaryContainer;
    }
    int layer = 0;
    if (enabled && HasFlag(vwfPressed)) {
        layer = kM3PressedOpacity;
    } else if (enabled && HasFlag(vwfHovered)) {
        layer = kM3HoverOpacity;
    }
    if (layer > 0) {
        container = M3StateLayer(container, m3.onSurface, layer);
    }
    if (container != RibbonPanelBgColor()) {
        ctx.gfx->FillRoundedRect(r, radius, container);
    }

    Pixmap* px = (!enabled && pixmapDisabled) ? pixmapDisabled : pixmap;
    if (compact && px) {
        int x = r.x + ((r.dx - DropdownDx() - px->width) / 2);
        int yc = r.y + ((r.dy - px->height) / 2);
        ctx.gfx->DrawPixmap(px, {x, yc, px->width, px->height});
        return;
    }
    int y = r.y + padding.top;
    if (px) {
        int x = r.x + ((r.dx - DropdownDx() - px->width) / 2);
        ctx.gfx->DrawPixmap(px, {x, y, px->width, px->height});
        y += px->height;
    } else {
        y += RibbonLargeIconSize();
    }
    y += DpiScale(kRibbonBtnIconGap);

    if (len(label) > 0) {
        Color col = enabled ? (isSelected ? m3.onSecondaryContainer : m3.onSurface)
                            : M3StateLayer(RibbonPanelBgColor(), m3.onSurface, 38);
        int lineDy = PlatformFontLineHeight(font);
        Rect rText{r.x, y, r.dx, lineDy};
        ctx.gfx->DrawText(label, rText, gfxTextCenter | gfxTextSingleLine | gfxTextNoClip, font, col);
    }

    int dropDx = DropdownDx();
    if (dropDx > 0) {
        Color col = enabled ? textColor : textColorDisabled;
        float pt = 11.f * (float)DpiGet() / 96.f;
        PlatformFont* chevronFont = GetPlatformFont(StrL("Segoe UI"), pt, PlatformFontStyle::Regular);
        Rect drop{r.Right() - dropDx, r.y + padding.top, dropDx, RibbonLargeIconSize()};
        ctx.gfx->DrawText(StrL("\xE2\x96\xBE"), drop, gfxTextCenter | gfxTextVCenter, chevronFont, col);
    }
}

RibbonButton* AsRibbonButton(ILayout* l) {
    if (l && l->GetKind() == kindVirtCtrlRibbonButton) {
        return (RibbonButton*)l;
    }
    return nullptr;
}

int RibbonButtonDy(PlatformFont* font) {
    int pad = DpiScale(kRibbonBtnPadY);
    return pad + RibbonLargeIconSize() + DpiScale(kRibbonBtnIconGap) + PlatformFontLineHeight(font) + pad;
}

//--- RibbonPanelFit

RibbonPanelFit::RibbonPanelFit(ILayout* row) : Padding(row, Insets{}) {
}

static void SetRibbonButtonsCompact(ILayout* l, bool compact) {
    if (auto* b = AsRibbonButton(l)) {
        b->compact = compact;
        return;
    }
    int n = l->LayoutChildCount();
    for (int i = 0; i < n; i++) {
        SetRibbonButtonsCompact(l->LayoutChildAt(i), compact);
    }
}

Size RibbonPanelFit::Layout(Constraints bc) {
    SetRibbonButtonsCompact(child, false);
    if (bc.HasBoundedWidth() && child->MinIntrinsicWidth(bc.max.dy) > bc.max.dx) {
        SetRibbonButtonsCompact(child, true);
    }
    return Padding::Layout(bc);
}

//--- RibbonTab

static Kind kindRibbonTab = "ribbonTab";

constexpr int kRibbonTabPadX = 14;
constexpr int kRibbonTabPadY = 7;
constexpr int kRibbonTabUnderlineDy = 3;

RibbonTab::RibbonTab(Str s, PlatformFont* f) {
    kind = kindRibbonTab;
    label = str::Dup(s);
    font = f;
    cursor = CursorId::Hand;
    onMouseEnter = MkMethod0<RibbonTab, &RibbonTab::OnMouseEnter>(this);
    onMouseLeave = MkMethod0<RibbonTab, &RibbonTab::OnMouseLeave>(this);
}

RibbonTab::~RibbonTab() {
    str::Free(label);
}

int RibbonTabDy(PlatformFont* font) {
    return PlatformFontLineHeight(font) + 2 * DpiScale(kRibbonTabPadY) + DpiScale(kRibbonTabUnderlineDy);
}

Size RibbonTab::GetIdealSize() {
    Size sz = PlatformFontMeasureText(font, label);
    int dx = sz.dx + 2 * DpiScale(kRibbonTabPadX);
    return {dx, RibbonTabDy(font)};
}

// Material primary tabs: the active one in primary with a 3dp indicator as wide
// as its label, rounded on top; the others in on-surface-variant
void RibbonTab::Paint(VirtPaintCtx& ctx) {
    Rect r = ctx.bounds;
    const M3Scheme& m3 = M3();
    if (HasFlag(vwfHovered) || HasFlag(vwfPressed)) {
        int layer = HasFlag(vwfPressed) ? kM3PressedOpacity : kM3HoverOpacity;
        Rect hi = r;
        hi.y += DpiScale(4);
        hi.dy -= DpiScale(4) + DpiScale(kRibbonTabUnderlineDy);
        ctx.gfx->FillRoundedRect(hi, DpiScale(8), M3StateLayer(RibbonTabRowBgColor(), m3.onSurface, layer));
    }
    Color col = isActive ? m3.primary : m3.onSurfaceVariant;
    Rect rText = r;
    rText.dy -= DpiScale(kRibbonTabUnderlineDy);
    ctx.gfx->DrawText(label, rText, gfxTextCenter | gfxTextVCenter | gfxTextSingleLine, font, col);
    if (!isActive) {
        return;
    }
    int dy = DpiScale(kRibbonTabUnderlineDy);
    int textDx = PlatformFontMeasureText(font, label).dx;
    int barDx = std::max(textDx, DpiScale(24));
    Rect bar{r.x + (r.dx - barDx) / 2, r.Bottom() - dy, barDx, dy * 2};
    // round the top only: the lower half of the shape is clipped by the tab
    ctx.gfx->PushClip({r.x, r.Bottom() - dy, r.dx, dy});
    ctx.gfx->FillRoundedRect(bar, dy, m3.primary);
    ctx.gfx->PopClip();
}

void RibbonTab::OnMouseEnter() {
    Invalidate();
}

void RibbonTab::OnMouseLeave() {
    Invalidate();
}

RibbonTab* AsRibbonTab(ILayout* l) {
    if (l && l->GetKind() == kindRibbonTab) {
        return (RibbonTab*)l;
    }
    return nullptr;
}
