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
#include "Animation.h"
#include "Ribbon.h"

//--- icons the classic toolbar doesn't have (Tabler icons, MIT license)

#define TABLER_SVG(body)                                                                                             \
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
static const char* gIconPageInsert =
    TABLER_SVG(R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
               R"(<path d="M12 11l0 6" /><path d="M9 14l6 0" />)");
// file-minus
static const char* gIconPageDelete =
    TABLER_SVG(R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
               R"(<path d="M9 14l6 0" />)");
// files-off-ish: two pages with a minus
static const char* gIconPageDeleteMany =
    TABLER_SVG(R"(<path d="M15 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M18 17h-7a2 2 0 0 1 -2 -2v-10a2 2 0 0 1 2 -2h4l5 5v7a2 2 0 0 1 -2 2z" />)"
               R"(<path d="M16 17v2a2 2 0 0 1 -2 2h-7a2 2 0 0 1 -2 -2v-10a2 2 0 0 1 2 -2h2" /><path d="M12 11h5" />)");
// file-arrow-up-ish: a page with an arrow up
static const char* gIconMoveUp =
    TABLER_SVG(R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
               R"(<path d="M12 17v-6" /><path d="M9.5 13.5l2.5 -2.5l2.5 2.5" />)");
static const char* gIconMoveDown =
    TABLER_SVG(R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
               R"(<path d="M12 11v6" /><path d="M9.5 14.5l2.5 2.5l2.5 -2.5" />)");
// file-export
static const char* gIconPageExtract =
    TABLER_SVG(R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M11.5 21h-4.5a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v5m-5 6h7m-3 -3l3 3l-3 3" />)");
// file-import
static const char* gIconPageImport =
    TABLER_SVG(R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M5 13v-8a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2h-5.5m-9.5 -2h7m-3 -3l3 3l-3 3" />)");
// files
static const char* gIconMerge =
    TABLER_SVG(R"(<path d="M15 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M18 17h-7a2 2 0 0 1 -2 -2v-10a2 2 0 0 1 2 -2h4l5 5v7a2 2 0 0 1 -2 2z" />)"
               R"(<path d="M16 17v2a2 2 0 0 1 -2 2h-7a2 2 0 0 1 -2 -2v-10a2 2 0 0 1 2 -2h2" />)");
// arrows-split
static const char* gIconSplit = TABLER_SVG(R"(<path d="M21 17h-8l-3.5 -5h-6.5" /><path d="M21 7h-8l-3.495 5" />)"
                                           R"(<path d="M18 10l3 -3l-3 -3" /><path d="M18 20l3 -3l-3 -3" />)");
// file-zip
static const char* gIconCompress =
    TABLER_SVG(R"(<path d="M6 20.735a2 2 0 0 1 -1 -1.735v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2h-1" />)"
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
static const char* gIconClipboard =
    TABLER_SVG(R"(<path d="M9 5h-2a2 2 0 0 0 -2 2v12a2 2 0 0 0 2 2h10a2 2 0 0 0 2 -2v-12a2 2 0 0 0 -2 -2h-2" />)"
               R"(<rect x="9" y="3" width="6" height="4" rx="2" />)");
// link
static const char* gIconLink =
    TABLER_SVG(R"(<path d="M9 15l6 -6" /><path d="M11 6l.463 -.536a5 5 0 0 1 7.071 7.072l-.534 .464" />)"
               R"(<path d="M13 18l-.397 .534a5.068 5.068 0 0 1 -7.127 0a4.972 4.972 0 0 1 0 -7.071l.524 -.463" />)");
// signature
static const char* gIconSignature = TABLER_SVG(
    R"(<path d="M3 17c3.333 -3.333 5 -6 5 -8c0 -3 -1 -3 -2 -3s-2.032 1.085 -2 3c.034 2.048 1.658 4.877 2.5 6c1.5 2 2.5 2.5 3.5 1l2 -3c.333 2.667 1.333 4 3 4c.53 0 2.639 -2 3 -2c.517 0 1.517 .667 3 2" />)");
// certificate
static const char* gIconCertificate =
    TABLER_SVG(R"(<path d="M12 15a3 3 0 1 0 6 0a3 3 0 1 0 -6 0" /><path d="M13 17.5v4.5l2 -1.5l2 1.5v-4.5" />)"
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
static const char* gIconBook =
    TABLER_SVG(R"(<path d="M3 19a9 9 0 0 1 9 0a9 9 0 0 1 9 0" /><path d="M3 6a9 9 0 0 1 9 0a9 9 0 0 1 9 0" />)"
               R"(<path d="M3 6l0 13" /><path d="M12 6l0 13" /><path d="M21 6l0 13" />)");
// presentation
static const char* gIconPresentation =
    TABLER_SVG(R"(<path d="M3 4l18 0" /><path d="M4 4v10a2 2 0 0 0 2 2h12a2 2 0 0 0 2 -2v-10" />)"
               R"(<path d="M12 16l0 4" /><path d="M9 20l6 0" /><path d="M8 12l3 -3l2 2l3 -3" />)");
// maximize
static const char* gIconFullscreen =
    TABLER_SVG(R"(<path d="M4 8v-2a2 2 0 0 1 2 -2h2" /><path d="M4 16v2a2 2 0 0 0 2 2h2" />)"
               R"(<path d="M16 4h2a2 2 0 0 1 2 2v2" /><path d="M16 20h2a2 2 0 0 0 2 -2v-2" />)");
// arrow-autofit-width
static const char* gIconFitWidth =
    TABLER_SVG(R"(<path d="M4 12v-6a2 2 0 0 1 2 -2h12a2 2 0 0 1 2 2v6" /><path d="M10 18h-7" /><path d="M21 18h-7" />)"
               R"(<path d="M6 15l-3 3l3 3" /><path d="M18 15l3 3l-3 3" />)");
// arrow-autofit-height
static const char* gIconFitPage =
    TABLER_SVG(R"(<path d="M12 20h-6a2 2 0 0 1 -2 -2v-12a2 2 0 0 1 2 -2h6" /><path d="M18 4v17" />)"
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
static const char* gIconEye =
    TABLER_SVG(R"(<path d="M10 12a2 2 0 1 0 4 0a2 2 0 0 0 -4 0" />)"
               R"(<path d="M21 12c-2.4 4 -5.4 6 -9 6c-3.6 0 -6.6 -2 -9 -6c2.4 -4 5.4 -6 9 -6c3.6 0 6.6 2 9 6" />)");
// typography
static const char* gIconTypewriter = TABLER_SVG(
    R"(<path d="M4 20l3 0" /><path d="M14 20l7 0" /><path d="M6.9 15l6.9 0" /><path d="M10.2 6.3l5.8 13.7" />)"
    R"(<path d="M5 20l6 -16l2 0l7 16" />)");
// lock
static const char* gIconLock =
    TABLER_SVG(R"(<rect x="5" y="11" width="14" height="10" rx="2" /><path d="M11 16a1 1 0 1 0 2 0a1 1 0 0 0 -2 0" />)"
               R"(<path d="M8 11v-4a4 4 0 1 1 8 0v4" />)");
// lock-open
static const char* gIconUnlock =
    TABLER_SVG(R"(<rect x="5" y="11" width="14" height="10" rx="2" /><path d="M11 16a1 1 0 1 0 2 0a1 1 0 0 0 -2 0" />)"
               R"(<path d="M8 11v-5a4 4 0 0 1 8 0" />)");
// palette
static const char* gIconTheme = TABLER_SVG(
    R"(<path d="M12 21a9 9 0 0 1 0 -18c4.97 0 9 3.582 9 8c0 1.06 -.474 2.078 -1.318 2.828c-.844 .75 -1.989 1.172 -3.182 1.172h-2.5a2 2 0 0 0 -1 3.75a1.3 1.3 0 0 1 -1 2.25" />)"
    R"(<path d="M7.5 10.5a1 1 0 1 0 2 0a1 1 0 1 0 -2 0" /><path d="M11.5 7.5a1 1 0 1 0 2 0a1 1 0 1 0 -2 0" />)"
    R"(<path d="M15.5 10.5a1 1 0 1 0 2 0a1 1 0 1 0 -2 0" />)");
// file-text
static const char* gIconExtractText =
    TABLER_SVG(R"(<path d="M14 3v4a1 1 0 0 0 1 1h4" />)"
               R"(<path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z" />)"
               R"(<path d="M9 9l1 0" /><path d="M9 13l6 0" /><path d="M9 17l6 0" />)");
// list-details
static const char* gIconCommentList =
    TABLER_SVG(R"(<path d="M13 5h8" /><path d="M13 9h5" /><path d="M13 15h8" /><path d="M13 19h5" />)"
               R"(<rect x="3" y="4" width="6" height="6" rx="1" /><rect x="3" y="14" width="6" height="6" rx="1" />)");
// layout-sidebar
static const char* gIconBookmarks =
    TABLER_SVG(R"(<rect x="4" y="4" width="16" height="16" rx="2" /><path d="M9 4l0 16" />)");

// pointer
static const char* gIconPointer = TABLER_SVG(
    R"(<path d="M7.904 17.563a1.2 1.2 0 0 0 2.228 .308l2.09 -3.093l4.907 4.907a1.067 1.067 0 0 0 1.509 0l1.047 -1.047a1.067 1.067 0 0 0 0 -1.509l-4.907 -4.907l3.113 -2.09a1.2 1.2 0 0 0 -.309 -2.228l-13.582 -3.904l3.904 13.563z" />)");
// camera
static const char* gIconSnapshot = TABLER_SVG(
    R"(<path d="M5 7h1a2 2 0 0 0 2 -2a1 1 0 0 1 1 -1h6a1 1 0 0 1 1 1a2 2 0 0 0 2 2h1a2 2 0 0 1 2 2v9a2 2 0 0 1 -2 2h-14a2 2 0 0 1 -2 -2v-9a2 2 0 0 1 2 -2" />)"
    R"(<path d="M9 13a3 3 0 1 0 6 0a3 3 0 0 0 -6 0" />)");
// help-circle
static const char* gIconHelp =
    TABLER_SVG(R"(<path d="M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0" /><path d="M12 17l0 .01" />)"
               R"(<path d="M12 13.5a1.5 1.5 0 0 1 1 -1.5a2.6 2.6 0 1 0 -3 -4" />)");
// keyboard
static const char* gIconKeyboard =
    TABLER_SVG(R"(<path d="M2 8a2 2 0 0 1 2 -2h16a2 2 0 0 1 2 2v8a2 2 0 0 1 -2 2h-16a2 2 0 0 1 -2 -2l0 -8" />)"
               R"(<path d="M6 10l0 .01" /><path d="M10 10l0 .01" /><path d="M14 10l0 .01" /><path d="M18 10l0 .01" />)"
               R"(<path d="M6 14l0 .01" /><path d="M18 14l0 .01" /><path d="M10 14l4 .01" />)");
// world
static const char* gIconWorld =
    TABLER_SVG(R"(<path d="M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0" /><path d="M3.6 9h16.8" /><path d="M3.6 15h16.8" />)"
               R"(<path d="M11.5 3a17 17 0 0 0 0 18" /><path d="M12.5 3a17 17 0 0 1 0 18" />)");
// cloud-download
static const char* gIconUpdate =
    TABLER_SVG(R"(<path d="M19 18a3.5 3.5 0 0 0 0 -7h-1a5 4.5 0 0 0 -11 -2a4.6 4.4 0 0 0 -2.1 8.4" />)"
               R"(<path d="M12 13l0 9" /><path d="M9 19l3 3l3 -3" />)");
// settings
static const char* gIconSettings = TABLER_SVG(
    R"(<path d="M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 -2.924 1.756 -3.35 0a1.724 1.724 0 0 0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 -1.065 -2.572c-1.756 -.426 -1.756 -2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 -2.37c1 .608 2.296 .07 2.572 -1.065z" />)"
    R"(<path d="M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0" />)");
// logout
static const char* gIconExit =
    TABLER_SVG(R"(<path d="M14 8v-2a2 2 0 0 0 -2 -2h-7a2 2 0 0 0 -2 2v12a2 2 0 0 0 2 2h7a2 2 0 0 0 2 -2v-2" />)"
               R"(<path d="M9 12h12l-3 -3" /><path d="M18 15l3 -3" />)");
// stack
static const char* gIconFlatten =
    TABLER_SVG(R"(<path d="M12 4l-8 4l8 4l8 -4l-8 -4" /><path d="M4 12l8 4l8 -4" /><path d="M4 16l8 4l8 -4" />)");
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
    {nullptr, 0, TrN("Redaction")},
    {gIconAnnotRedact, CmdCreateAnnotRedact, TrN("Redact"), TrN("Mark an area for redaction")},
    {gIconApplyRedactions, CmdApplyRedactions, TrN("Apply"), TrN("Apply redactions (permanently removes the content)")},
    {nullptr, 0, TrN("History")},
    {gIconUndo, CmdUndo, TrN("Undo")},
    {gIconRedo, CmdRedo, TrN("Redo")},
    {nullptr, 0, TrN("Export")},
    {gIconCompress, CmdPdfCompress, TrN("Compress"), TrN("Save a smaller copy of the PDF")},
    {gIconExtractText, CmdScanOcr, TrN("Scan & OCR"), TrN("Recognize text on scanned pages")},
    {gIconExtractText, CmdDocumentExtractText, TrN("To Text"), TrN("Extract the text of the document")},
    {nullptr, 0, TrN("Save")},
    {gIconSave, CmdSaveAnnotations, TrN("Save"), TrN("Save changes to the PDF")},
    {gIconSaveToNewFile, CmdSaveAnnotationsNewFile, TrN("Save Copy"), TrN("Save changes to a new PDF")},
};

static const RibbonItemDef gOrganizeItems[] = {
    {gIconThumbnails, CmdToggleThumbnails, TrN("Thumbnails"), TrN("Show page thumbnails")},
    {nullptr, 0, TrN("Insert and delete")},
    {gIconPageInsert, CmdInsertBlankPage, TrN("Blank Page"), TrN("Insert a blank page after this one")},
    {gIconPageImport, CmdInsertPagesFromFile, TrN("Insert File"), TrN("Insert the pages of another PDF after this page")},
    {gIconPageDelete, CmdDeleteCurrentPage, TrN("Delete Page"), TrN("Delete this page")},
    {gIconPageDeleteMany, CmdPdfDeletePages, TrN("Delete..."), TrN("Delete a range of pages")},
    {nullptr, 0, TrN("Rotate and move")},
    {gIconRotateLeft, CmdRotatePageLeft, TrN("Rotate Left"), TrN("Rotate this page left and save it")},
    {gIconRotateRight, CmdRotatePageRight, TrN("Rotate Right"), TrN("Rotate this page right and save it")},
    {gIconMoveUp, CmdMovePageUp, TrN("Move Up"), TrN("Move this page before the previous one")},
    {gIconMoveDown, CmdMovePageDown, TrN("Move Down"), TrN("Move this page after the next one")},
    {gIconUndo, CmdUndoPageChange, TrN("Undo"), TrN("Undo the last page change")},
    {nullptr, 0, TrN("Extract and combine")},
    {gIconPageExtract, CmdPdfExtractPages, TrN("Extract"), TrN("Extract pages to a new PDF")},
    {gIconSplit, CmdSplitPdf, TrN("Split"), TrN("Split the PDF into several files")},
    {gIconMerge, CmdMergePDF, TrN("Merge"), TrN("Combine several PDF files into one")},
    {nullptr, 0, TrN("Page marks")},
    {gIconAnnotStamp, CmdAddWatermark, TrN("Watermark"), TrN("Add watermark to pages")},
    {gIconTypewriter, CmdAddHeaderFooter, TrN("Header/Footer"), TrN("Add header or footer to pages")},
    {gIconAnnotStamp, CmdBatesNumbering, TrN("Bates Numbering"), TrN("Add Bates numbering to pages")},
    {nullptr, 0, TrN("Convert")},
    {gIconExportImages, CmdConvertPdfToImages, TrN("To Images"), TrN("Save pages as images")},
    {gIconImage, CmdConvertImageToPdf, TrN("From Image"), TrN("Create a PDF from images")},
};

static const RibbonItemDef gFormsSignItems[] = {
    {gIconForms, CmdPrepareForm, TrN("Prepare a Form"), TrN("Draw a field, then choose its type and properties")},
    {gIconForms, CmdToggleHighlightFormFields, TrN("Highlight Fields"), TrN("Highlight form fields")},
    {gIconTypewriter, CmdCreateAnnotFreeText, TrN("Typewriter"), TrN("Type text anywhere on the page")},
    {nullptr, 0, TrN("Signatures")},
    {gIconCreateSignature, CmdCreateSignature, TrN("Create Signature"), TrN("Draw, type or upload your signature")},
    {gIconSignature, CmdSignWithImage, TrN("Place Signature"), TrN("Place your signature on the page")},
    {gIconCertificate, CmdSignDocument, TrN("Digital ID"), TrN("Sign the document with a certificate")},
    {gIconAnnotStamp, CmdCreateAnnotStamp, TrN("Stamp")},
    {nullptr, 0, TrN("Save")},
    {gIconSave, CmdSaveAnnotations, TrN("Save"), TrN("Save changes to the PDF")},
};

static const RibbonItemDef gViewItems[] = {
    {gIconLayoutSinglePage, CmdSinglePageView, TrN("Single Page")},
    {gIconFacing, CmdFacingView, TrN("Two Pages"), TrN("Facing pages")},
    {gIconBook, CmdBookView, TrN("Book"), TrN("Book view")},
    {gIconLayoutContinuous, CmdToggleContinuousView, TrN("Continuous"), TrN("Scroll pages continuously")},
    {nullptr, 0, TrN("Zoom")},
    {gIconFitWidth, CmdZoomFitWidth, TrN("Fit Width")},
    {gIconFitPage, CmdZoomFitPage, TrN("Fit Page")},
    {gIconActualSize, CmdZoomActualSize, TrN("Actual Size")},
    {nullptr, 0, TrN("Panels")},
    {gIconBookmarks, CmdToggleBookmarks, TrN("Bookmarks"), TrN("Show bookmarks")},
    {gIconThumbnails, CmdToggleThumbnails, TrN("Thumbnails"), TrN("Show page thumbnails")},
    {gIconEye, CmdToggleShowAnnotations, TrN("Comments"), TrN("Show or hide comments")},
    {nullptr, 0, TrN("Reading")},
    {gIconFullscreen, CmdToggleFullscreen, TrN("Full Screen")},
    {gIconPresentation, CmdTogglePresentationMode, TrN("Present"), TrN("Presentation mode")},
    {gIconInvert, CmdInvertColors, TrN("Night Mode"), TrN("Invert page colors")},
    {gIconTheme, CmdToggleLightDarkTheme, TrN("Theme"), TrN("Switch between light and dark theme")},
    {gIconSpeak, CmdToggleReadAloud, TrN("Read Aloud")},
    {nullptr, 0, TrN("Navigation")},
    {gIconNavigateBack, CmdNavigateBack, TrN("Back"), TrN("Go back to the previous view")},
    {gIconNavigateForward, CmdNavigateForward, TrN("Forward")},
    {gIconInfo, CmdProperties, TrN("Properties"), TrN("Document properties")},
};

static const RibbonItemDef gProtectItems[] = {
    {gIconLock, CmdPdfEncrypt, TrN("Protect"), TrN("Encrypt the PDF with a password")},
    {gIconUnlock, CmdPdfDecrypt, TrN("Unprotect"), TrN("Remove the password from the PDF")},
    {nullptr, 0, TrN("Redaction")},
    {gIconAnnotRedact, CmdCreateAnnotRedact, TrN("Redact"), TrN("Mark an area for redaction")},
    {gIconApplyRedactions, CmdApplyRedactions, TrN("Apply"), TrN("Apply redactions (permanently removes the content)")},
    {nullptr, 0, TrN("Sign and flatten")},
    {gIconCertificate, CmdSignDocument, TrN("Digital ID"), TrN("Sign the document with a certificate")},
    {gIconFlatten, CmdPdfBake, TrN("Flatten"), TrN("Merge comments and form fields into the pages")},
    {nullptr, 0, TrN("Save")},
    {gIconSave, CmdSaveAnnotations, TrN("Save"), TrN("Save changes to the PDF")},
};

static const RibbonItemDef gHelpItems[] = {
    {gIconHelp, CmdHelpOpenManualOnWebsite, TrN("Help"), TrN("Open the user guide")},
    {gIconKeyboard, CmdToggleKeyboardHelp, TrN("Shortcuts"), TrN("Keyboard shortcuts")},
    {gIconCommandPalette, CmdCommandPalette, TrN("Commands"), TrN("Find any command by name")},
    {nullptr, 0, TrN("Support")},
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
    {nullptr, 0, TrN("Settings")},
    {gIconInfo, CmdProperties, TrN("Properties"), TrN("Document properties")},
    {gIconSettings, CmdOptions, TrN("Settings")},
    {nullptr, 0, TrN("Exit")},
    {gIconExit, CmdExit, TrN("Exit")},
};
// clang-format on

static const Str gPageNames[] = {
    TrN("Home"), TrN("Comment"), TrN("Edit"), TrN("Organize"), TrN("Fill & Sign"),
    TrN("View"), TrN("Protect"), TrN("Help"), TrN("File"),
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

static const Str gModeNames[] = {
    TrN("All tools"), TrN("Read"), TrN("Edit"), TrN("Convert"), TrN("E-Sign"),
};
static_assert(dimof(gModeNames) == (int)RibbonMode::Count);

static const RibbonPage gModePages[] = {
    RibbonPage::Home, RibbonPage::View, RibbonPage::Edit, RibbonPage::Organize, RibbonPage::FormsSign,
};
static_assert(dimof(gModePages) == (int)RibbonMode::Count);

int RibbonModeCount() {
    return (int)RibbonMode::Count;
}

Str RibbonModeName(RibbonMode mode) {
    return trans::GetTranslation(gModeNames[(int)mode]);
}

RibbonPage RibbonModePage(RibbonMode mode) {
    return gModePages[(int)mode];
}

// pages without a tab of their own light the tab of the closest mode
int RibbonModeForPage(RibbonPage page) {
    switch (page) {
        case RibbonPage::Comment:
            return (int)RibbonMode::Edit;
        case RibbonPage::Protect:
            return (int)RibbonMode::ESign;
        default:
            break;
    }
    for (int i = 0; i < (int)RibbonMode::Count; i++) {
        if (gModePages[i] == page) {
            return i;
        }
    }
    return -1;
}

// Home is All tools: like PDFCraft, its tools are in the left panel and the
// floating tool strip, so the top bar stands alone
bool RibbonPageHasPanel(RibbonPage page) {
    return page != RibbonPage::Home;
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

static Color RibbonFocusColor() {
    return ThemeUsesHighContrastColors() ? SysHighlightBgColor() : RibbonAccentColor();
}

// Separate the tab strip from the tools without tinting the document chrome.
Color RibbonTabRowBgColor() {
    return M3().surfaceContainerLow;
}

Color RibbonPanelBgColor() {
    return M3IsDark() ? M3().surfaceContainerLow : M3().surfaceContainerLowest;
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
    return DpiScale(20);
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

// a tool is a short pill: icon and label side by side. PDFCraft: button
// padding 10 x 5 px
constexpr int kRibbonBtnPadX = 10;
constexpr int kRibbonBtnPadY = 5;
constexpr int kRibbonBtnIconGap = 6;
constexpr int kRibbonBtnMinDx = 36;
constexpr int kRibbonBtnMaxLabelDx = 120;

RibbonButton::RibbonButton() {
    animatedBg = new AnimatedColor();
    kind = kindVirtCtrlRibbonButton;
    padding = {DpiScale(kRibbonBtnPadY), DpiScale(kRibbonBtnPadX), DpiScale(kRibbonBtnPadY), DpiScale(kRibbonBtnPadX)};
}

RibbonButton::~RibbonButton() {
    delete animatedBg;
    str::Free(label);
}

void RibbonButton::SetLabel(Str s) {
    str::ReplaceWithCopy(&label, s);
}

Size RibbonButton::GetIdealSize() {
    int iconDx = pixmap ? pixmap->width : RibbonLargeIconSize();
    int dy = RibbonButtonDy(font);
    int contentDx = iconDx + DropdownDx();
    if (!compact && len(label) > 0) {
        int textDx = std::min(PlatformFontMeasureText(font, label).dx, DpiScale(kRibbonBtnMaxLabelDx));
        contentDx += DpiScale(kRibbonBtnIconGap) + textDx;
    }
    int dx = std::max(contentDx + padding.left + padding.right, DpiScale(kRibbonBtnMinDx));
    return {dx, dy};
}

// A toggled-on button sits on the selection color; hover and press lay the
// neutral hover color, then the content color, over it. The icon and label
// share one centered run, mirrored in right-to-left UIs.
void RibbonButton::Paint(VirtPaintCtx& ctx) {
    bool enabled = IsEnabled();
    Rect r = ctx.bounds;
    int diameter = DpiScale(kChromeCornerDiameter);
    const M3Scheme& m3 = M3();
    Color container = RibbonPanelBgColor();
    if (isSelected && enabled) {
        container = m3.secondaryContainer;
    }
    if (enabled && HasFlag(vwfPressed)) {
        container = M3StateLayer(container, m3.onSurface, kM3PressedOpacity);
    } else if (enabled && HasFlag(vwfHovered)) {
        container = isSelected ? M3StateLayer(container, m3.onSurface, kM3HoverOpacity) : m3.surfaceContainerHigh;
    }
    container = animatedBg->Get(GetHwnd(), container);
    if (container != RibbonPanelBgColor()) {
        ctx.gfx->FillRoundedRect(r, diameter, container);
    }

    if (HasFlag(vwfFocused)) ctx.gfx->DrawRect(r, RibbonFocusColor(), DpiScale(1));

    Pixmap* px = (!enabled && pixmapDisabled) ? pixmapDisabled : pixmap;
    int iconDx = px ? px->width : RibbonLargeIconSize();
    int iconDy = px ? px->height : RibbonLargeIconSize();
    int dropDx = DropdownDx();
    bool showLabel = !compact && len(label) > 0;
    int gap = DpiScale(kRibbonBtnIconGap);
    int lineDy = PlatformFontLineHeight(font);
    int textDx = 0;
    if (showLabel) {
        textDx = std::min(PlatformFontMeasureText(font, label).dx, DpiScale(kRibbonBtnMaxLabelDx));
        textDx = std::min(textDx, std::max(0, r.dx - padding.left - padding.right - dropDx - iconDx - gap));
    }
    int runDx = iconDx + (showLabel ? gap + textDx : 0);
    // the split chevron stays at the right edge, where the hit test expects it
    int x = r.x + (r.dx - dropDx - runDx) / 2;
    if (alignStart) x = isRtl ? r.Right() - padding.right - runDx : r.x + padding.left;
    int iconY = r.y + (r.dy - iconDy) / 2;
    int cursor = isRtl ? x + runDx : x;
    // advance the cursor by dx; in RTL it walks leftward
    auto place = [&](int dx) {
        int left = isRtl ? cursor - dx : cursor;
        cursor = isRtl ? cursor - dx : cursor + dx;
        return left;
    };

    if (px) {
        ctx.gfx->DrawPixmap(px, {place(iconDx), iconY, px->width, px->height});
    } else {
        place(iconDx);
    }
    if (showLabel) {
        place(gap);
        Color col = enabled ? (isSelected ? m3.onSecondaryContainer : m3.onSurface)
                            : M3StateLayer(RibbonPanelBgColor(), m3.onSurface, 38);
        Rect rText{place(textDx), r.y + (r.dy - lineDy) / 2, textDx, lineDy};
        ctx.gfx->DrawText(label, rText, gfxTextLeft | gfxTextSingleLine | gfxTextEllipsis, font, col);
    }
    if (dropDx > 0) {
        Color col = enabled ? textColor : textColorDisabled;
        float pt = 11.f * (float)DpiGet() / 96.f;
        PlatformFont* chevronFont = GetPlatformFont(StrL("Segoe UI"), pt, PlatformFontStyle::Regular);
        Rect drop{r.Right() - dropDx, iconY, dropDx, iconDy};
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
    return pad + std::max(RibbonLargeIconSize(), PlatformFontLineHeight(font)) + pad;
}

//--- RibbonPanelFit

RibbonPanelFit::RibbonPanelFit(ILayout* row) : Padding(row, Insets{}) {}

static void CollectRibbonButtons(ILayout* l, Vec<RibbonButton*>& buttons) {
    if (IsCollapsed(l)) {
        return;
    }
    if (auto* b = AsRibbonButton(l)) {
        b->compact = false;
        VecAppend(buttons, b);
        return;
    }
    int n = l->LayoutChildCount();
    for (int i = 0; i < n; i++) {
        CollectRibbonButtons(l->LayoutChildAt(i), buttons);
    }
}

Size RibbonPanelFit::Layout(Constraints bc) {
    Vec<RibbonButton*> buttons;
    CollectRibbonButtons(child, buttons);
    int width = child->MinIntrinsicWidth(bc.max.dy);
    // Keep as many labels as possible; compact the widest labels first.
    while (bc.HasBoundedWidth() && width > bc.max.dx) {
        RibbonButton* candidate = nullptr;
        int saving = 0;
        for (RibbonButton* b : buttons) {
            if (b->compact) {
                continue;
            }
            int fullDx = b->GetIdealSize().dx;
            b->compact = true;
            int delta = fullDx - b->GetIdealSize().dx;
            b->compact = false;
            if (delta > saving) {
                candidate = b;
                saving = delta;
            }
        }
        if (!candidate) {
            break;
        }
        candidate->compact = true;
        width -= saving;
    }
    return Padding::Layout(bc);
}

//--- RibbonTab

static Kind kindRibbonTab = "ribbonTab";

constexpr int kRibbonTabPadX = 14;
constexpr int kRibbonTabMinPadX = 4;
constexpr int kRibbonTabPadY = 7;
constexpr int kRibbonTabUnderlineDy = 3;

RibbonTab::RibbonTab(Str s, PlatformFont* f) {
    animatedBg = new AnimatedColor();
    kind = kindRibbonTab;
    label = str::Dup(s);
    font = f;
    paddingDx = DpiScale(kRibbonTabPadX);
    cursor = CursorId::Hand;
    onMouseEnter = MkMethod0<RibbonTab, &RibbonTab::OnMouseEnter>(this);
    onMouseLeave = MkMethod0<RibbonTab, &RibbonTab::OnMouseLeave>(this);
}

RibbonTab::~RibbonTab() {
    delete animatedBg;
    str::Free(label);
}

int RibbonTabDy(PlatformFont* font) {
    return PlatformFontLineHeight(font) + 2 * DpiScale(kRibbonTabPadY) + DpiScale(kRibbonTabUnderlineDy);
}

Size RibbonTab::GetIdealSize() {
    Size sz = PlatformFontMeasureText(font, label);
    int dx = sz.dx + 2 * paddingDx;
    return {dx, RibbonTabDy(font)};
}

// A surface-backed active tab connects the selected category to its tools.
void RibbonTab::Paint(VirtPaintCtx& ctx) {
    Rect r = ctx.bounds;
    const M3Scheme& m3 = M3();
    if (isActive) {
        ctx.gfx->FillRect(r, RibbonPanelBgColor());
    }
    Color bg = isActive ? RibbonPanelBgColor() : RibbonTabRowBgColor();
    Color hover = bg;
    if (HasFlag(vwfHovered) || HasFlag(vwfPressed)) {
        int layer = HasFlag(vwfPressed) ? kM3PressedOpacity : kM3HoverOpacity;
        hover = M3StateLayer(bg, m3.onSurface, layer);
    }
    Rect hi = r;
    hi.y += DpiScale(4);
    hi.dy -= DpiScale(4) + DpiScale(kRibbonTabUnderlineDy);
    ctx.gfx->FillRoundedRect(hi, DpiScale(6), animatedBg->Get(GetHwnd(), hover));
    if (HasFlag(vwfFocused)) ctx.gfx->DrawRect(r, RibbonFocusColor(), DpiScale(1));
    // PDFCraft: neutral active text for underlined mode bar
    Color col = isActive ? m3.onSurface : m3.onSurfaceVariant;
    Rect rText = r;
    rText.dy -= DpiScale(kRibbonTabUnderlineDy);
    ctx.gfx->DrawText(label, rText, gfxTextCenter | gfxTextVCenter | gfxTextSingleLine, font, col);
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

RibbonTabRow::RibbonTabRow() {
    underlineX = new Animation();
    underlineWidth = new Animation();
}

RibbonTabRow::~RibbonTabRow() {
    delete underlineX;
    delete underlineWidth;
}

void RibbonTabRow::PaintUnderline(Gfx* gfx, HWND hwnd) {
    for (auto& item : children) {
        auto* tab = AsRibbonTab(item.layout);
        if (!tab || !tab->isActive || IsCollapsed(tab)) continue;
        Rect r = tab->BoundsInWindow();
        int width = std::max(PlatformFontMeasureText(tab->font, tab->label).dx, DpiScale(24));
        int x = r.x + (r.dx - width) / 2;
        Size size{lastBounds.dx, lastBounds.dy};
        if (!(size == paintedSize)) {
            underlineX->Snap((float)x);
            underlineWidth->Snap((float)width);
            paintedSize = size;
        }
        int left = (int)(underlineX->To(hwnd, (float)x) + .5f);
        int dx = (int)(underlineWidth->To(hwnd, (float)width) + .5f);
        int dy = DpiScale(kRibbonTabUnderlineDy);
        Rect bar{left, r.Bottom() - dy, dx, dy * 2};
        gfx->PushClip({lastBounds.x, r.Bottom() - dy, lastBounds.dx, dy});
        gfx->FillRoundedRect(bar, dy, RibbonAccentColor());
        gfx->PopClip();
        return;
    }
}

Size RibbonTabRow::Layout(Constraints bc) {
    int nTabs = 0;
    int pad = DpiScale(kRibbonTabPadX);
    for (auto& item : children) {
        if (auto* tab = AsRibbonTab(item.layout)) {
            tab->paddingDx = pad;
            if (!IsCollapsed(tab)) {
                nTabs++;
            }
        }
    }
    // Reclaim tab padding before the right-hand controls are pushed outside the window.
    int width = MinIntrinsicWidth(bc.max.dy);
    if (nTabs > 0 && bc.HasBoundedWidth() && width > bc.max.dx) {
        int sides = nTabs * 2;
        int reduction = (width - bc.max.dx + sides - 1) / sides;
        pad = std::max(DpiScale(kRibbonTabMinPadX), pad - reduction);
        for (auto& item : children) {
            if (auto* tab = AsRibbonTab(item.layout)) {
                tab->paddingDx = pad;
            }
        }
    }
    return HBox::Layout(bc);
}

//--- RibbonSearchBox

static Kind kindRibbonSearchBox = "ribbonSearchBox";

// PDFCraft: a pill 36 px high, 12 px padding, icon then placeholder
constexpr int kSearchBoxDx = 300;
constexpr int kSearchBoxMinDx = 160;
constexpr int kSearchBoxPadX = 12;
constexpr int kSearchBoxPadY = 7;
constexpr int kSearchBoxIconGap = 8;

RibbonSearchBox::RibbonSearchBox(Str ph, Str sc, PlatformFont* f) {
    kind = kindRibbonSearchBox;
    placeholder = str::Dup(ph);
    shortcut = str::Dup(sc);
    font = f;
    cursor = CursorId::Hand;
    onMouseEnter = MkMethod0<RibbonSearchBox, &RibbonSearchBox::OnHoverChanged>(this);
    onMouseLeave = MkMethod0<RibbonSearchBox, &RibbonSearchBox::OnHoverChanged>(this);
}

RibbonSearchBox::~RibbonSearchBox() {
    str::Free(placeholder);
    str::Free(shortcut);
}

void RibbonSearchBox::OnHoverChanged() {
    Invalidate();
}

Size RibbonSearchBox::GetIdealSize() {
    int dy = PlatformFontLineHeight(font) + 2 * DpiScale(kSearchBoxPadY);
    return {DpiScale(kSearchBoxDx), dy};
}

void RibbonSearchBox::Paint(VirtPaintCtx& ctx) {
    const M3Scheme& m3 = M3();
    Rect r = ctx.bounds;
    if (r.dx < DpiScale(kSearchBoxMinDx)) {
        // too narrow for the text: the icon alone still opens the palette
        r.x += r.dx - std::min(r.dx, r.dy);
        r.dx = std::min(r.dx, r.dy);
    }
    bool hot = HasFlag(vwfHovered) || HasFlag(vwfPressed);
    Color bg = m3.surfaceContainerLowest;
    Color border = hot ? m3.primary : m3.outline;
    ctx.gfx->FillRoundedRect(r, r.dy, bg, border);

    int padX = DpiScale(kSearchBoxPadX);
    int x = r.x + padX;
    if (icon) {
        ctx.gfx->DrawPixmap(icon, {x, r.y + (r.dy - icon->height) / 2, icon->width, icon->height});
        x += icon->width + DpiScale(kSearchBoxIconGap);
    }
    int right = r.Right() - padX;
    if (x >= right) {
        return;
    }
    Color dim = M3StateLayer(bg, m3.onSurface, 55);
    if (len(shortcut) > 0) {
        int scDx = PlatformFontMeasureText(font, shortcut).dx;
        Rect rs{right - scDx, r.y, scDx, r.dy};
        ctx.gfx->DrawText(shortcut, rs, gfxTextRight | gfxTextVCenter | gfxTextSingleLine, font,
                          M3StateLayer(bg, m3.onSurface, 40));
        right -= scDx + DpiScale(kSearchBoxIconGap);
    }
    if (right > x) {
        Rect rt{x, r.y, right - x, r.dy};
        ctx.gfx->DrawText(placeholder, rt, gfxTextLeft | gfxTextVCenter | gfxTextSingleLine | gfxTextEllipsis, font,
                          dim);
    }
}

RibbonSearchBox* AsRibbonSearchBox(ILayout* l) {
    if (l && l->GetKind() == kindRibbonSearchBox) {
        return (RibbonSearchBox*)l;
    }
    return nullptr;
}
