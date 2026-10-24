/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"

#if IS_DEBUG

#include "base/WinDynCalls.h"
#include "base/DbgHelpDyn.h"
#include "base/File.h"
#include "base/GuessFileType.h"
#include "base/Pixmap.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"

#include "AppTools.h"
#include "Settings.h"
#include "DocController.h"
#include "DocProperties.h"
#include "EngineBase.h"
#include "AppSettings.h"
#include "Flags.h"
#include "Commands.h"
#include "SvgIcons.h"
#include "EngineAll.h"
#include "Annotation.h"
#include "shared/PdfTools.h"
#include <mupdf/pdf.h>
#include "AppUnitTests.h"
#include "Ocr.h"

// must be last to over-write assert()
#include "base/tests/UtAssert.h"

// in src/base/tests/
void ArchiveTest();
void AppendStoreTest();
void BaseUtilTest();
void ByteOrderTests();
void ClipboardImageTest();
void CryptoUtilTest();
void CssParser_UnitTests();
void DictTest();
void DirRemoveAllTest();
void FileUtilTest();
void DocumentPanel_UnitTests();
void PanelLayout_UnitTests();
void Animation_UnitTests();
void RibbonAccess_UnitTests();
bool EngineMupdf_UnitTestPanelData();
void UpdateTempFileTests();
void GuessFileTypeTest();
void JsonTest();
void RefHoverTest();
void SettingsUtilTest();
void SquareTreeTest();
void StrFormatTest();
void StrTest();
void StrVecTest();
void VecTest();
void WinUtilTest();

// in src/tests/*_ut.cpp
void ChapterTable_UnitTests();
void LitDoc_UnitTests();
void MobiDoc_UnitTests();
void PagePosition_UnitTests();
void PdfSync_UnitTests();
void CachedObjects_UnitTests();
bool PdfDarkModeImageStats_UnitTest();
void SimpleLogTest();

void CommandPaletteModel_UnitTests();
void TextSelection_UnitTests();
void EngineDjvuDec_UnitTests();
void Layout_UnitTests();
void VirtCtrl_UnitTests();
bool TableOfContents_UnitTestSnapshotNamedDest();
bool MarkdownModel_UnitTestBrowserNavigationUrl();
bool MarkdownToc_UnitTestHtmlLinks();
bool MarkdownToc_UnitTestHtmlHeadings();
bool MarkdownToc_UnitTestMermaid();
bool EbookDoc_UnitTestNormalizeURL();
bool EbookDoc_UnitTestLoading();
bool GumboHtmlParser_UnitTest();
bool DisplayMode_UnitTestZoom();
bool ExternalViewers_UnitTestPDFXChangePaths();
bool Canvas_UnitTestScrollLineAmount();
bool EngineMupdf_UnitTestEbookLineSpacingCss();
bool EngineMupdf_UnitTestEbookFontFamilyCss();
bool EngineMupdf_UnitTestEbookMarginCss();
bool EngineMupdf_UnitTestMergeEBookUI();
bool EngineMupdf_UnitTestPageLabels();
bool Accelerators_UnitTestFolderNavIsSafe();
bool Accelerators_UnitTestTreeTakesLetters();
bool Accelerators_UnitTestCreateAnnotEdit();
bool Accelerators_UnitTestCustomShortcutShown();
bool ShortcutParse_UnitTestShiftedPunct();
bool AnnotSearch_UnitTests();
void ReadAloudHighlight_UnitTests();
bool RenderCache_UnitTestCookieUnlocked();

static void ParseFileArgsTest() {
    FileArgs* fa = ParseFileArgs(StrL("C:\\foo.pdf?page=4"));
    utassert(fa && str::Eq(fa->cleanPath, StrL("C:\\foo.pdf")) && fa->pageNumber == 4);
    delete fa;
    utassert(!ParseFileArgs(StrL("C:\\foo.pdf")));
    utassert(!ParseFileArgs(StrL("\\\\?\\C:\\foo.pdf")));
    // a garbled drive letter: no file before the '?'
    utassert(!ParseFileArgs(StrL("?:\\foo.pdf")));
}

static void ParseCommandLineTest() {
    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -bench foo.pdf", i);
        utassert(2 == len(i.pathsToBenchmark));
        utassert(str::Eq(StrL("foo.pdf"), i.pathsToBenchmark[0]));
        utassert(len(i.pathsToBenchmark[1]) == 0);
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -bench foo.pdf -fwdsearch-width 5", i);
        utassert(len(i.globalPrefArgs) == 2);
        Str s = i.globalPrefArgs[0];
        utassert(str::Eq(s, StrL("-fwdsearch-width")));
        s = i.globalPrefArgs[1];
        utassert(str::Eq(s, StrL("5")));
        utassert(2 == len(i.pathsToBenchmark));
        utassert(str::Eq(StrL("foo.pdf"), i.pathsToBenchmark[0]));
        utassert(len(i.pathsToBenchmark[1]) == 0);
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -bench bar.pdf loadonly", i);
        utassert(2 == len(i.pathsToBenchmark));
        utassert(str::Eq(StrL("bar.pdf"), i.pathsToBenchmark[0]));
        utassert(str::Eq(StrL("loadonly"), i.pathsToBenchmark[1]));
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -bench bar.pdf 1 -set-color-range 0x123456 #abCDef", i);
        utassert(len(i.globalPrefArgs) == 3);
        utassert(2 == len(i.pathsToBenchmark));
        utassert(str::Eq(StrL("bar.pdf"), i.pathsToBenchmark[0]));
        utassert(str::Eq(StrL("1"), i.pathsToBenchmark[1]));
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -bench bar.pdf 1-5,3   -bench some.pdf 1,3,8-34", i);
        utassert(4 == len(i.pathsToBenchmark));
        utassert(str::Eq(StrL("bar.pdf"), i.pathsToBenchmark[0]));
        utassert(str::Eq(StrL("1-5,3"), i.pathsToBenchmark[1]));
        utassert(str::Eq(StrL("some.pdf"), i.pathsToBenchmark[2]));
        utassert(str::Eq(StrL("1,3,8-34"), i.pathsToBenchmark[3]));
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -presentation -bgcolor 0xaa0c13 foo.pdf -invert-colors bar.pdf", i);
        utassert(true == i.enterPresentation);
        utassert(true == i.invertColors);
        utassert(2 == len(i.fileNames));
        utassert(0 == i.fileNames.Find(StrL("foo.pdf")));
        utassert(1 == i.fileNames.Find(StrL("bar.pdf")));
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -bg-color 0xaa0c13 -invertcolors rosanna.pdf", i);
        utassert(true == i.invertColors);
        utassert(1 == len(i.fileNames));
        utassert(0 == i.fileNames.Find(StrL("rosanna.pdf")));
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), LR"(Swiftleaf.exe "foo \" bar \\.pdf" un\"quoted.pdf)", i);
        utassert(2 == len(i.fileNames));
        utassert(0 == i.fileNames.Find(StrL(R"(foo " bar \\.pdf)")));
        utassert(1 == i.fileNames.Find(StrL(R"(un"quoted.pdf)")));
    }

    {
        Flags i;
        ParseFlags(
            GetPermArena(),
            L"Swiftleaf.exe -page 37 -view continuousfacing -zoom fitcontent -scroll 45,1234         -reuse-instance",
            i);
        utassert(0 == len(i.fileNames));
        utassert(i.pageNumber == 37);
        utassert(i.startView == DisplayMode::ContinuousFacing);
        utassert(i.startZoom == kZoomFitContent);
        utassert(i.startScroll.x == 45 && i.startScroll.y == 1234);
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), LR"(Swiftleaf.exe -view "single page" -zoom 237.45 -scroll -21,-1)", i);
        utassert(0 == len(i.fileNames));
        utassert(i.startView == DisplayMode::SinglePage);
        utassert(i.startZoom == 237.45f);
        utassert(i.startScroll.x == -21 && i.startScroll.y == -1);
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -zoom 35%", i);
        utassert(0 == len(i.fileNames));
        utassert(i.startZoom == 35.f);
    }

    {
        Flags i;
        ParseFlags(GetPermArena(), L"Swiftleaf.exe -zoom fit-content", i);
        utassert(i.startZoom == kZoomFitContent);
        utassert(0 == len(i.fileNames));
    }
}

static void BenchRangeTest() {
    utassert(IsBenchPagesInfo(StrL("1")));
    utassert(IsBenchPagesInfo(StrL("2-4")));
    utassert(IsBenchPagesInfo(StrL("5,7")));
    utassert(IsBenchPagesInfo(StrL("6,8,")));
    utassert(IsBenchPagesInfo(StrL("1-3,4,6-9,13")));
    utassert(IsBenchPagesInfo(StrL("2-")));
    utassert(IsBenchPagesInfo(StrL("loadonly")));

    utassert(!IsBenchPagesInfo(StrL("")));
    utassert(!IsBenchPagesInfo(StrL("-2")));
    utassert(!IsBenchPagesInfo(StrL("2--4")));
    utassert(!IsBenchPagesInfo(StrL("4-2")));
    utassert(!IsBenchPagesInfo(StrL("1-3,loadonly")));
    utassert(!IsBenchPagesInfo({}));
}

static void versioncheck_test() {
    utassert(IsValidProgramVersion(StrL("1")));
    utassert(IsValidProgramVersion(StrL("1.1")));
    utassert(IsValidProgramVersion(StrL("1.1.1\r\n")));
    utassert(IsValidProgramVersion(StrL("2662")));

    utassert(!IsValidProgramVersion(StrL("1.1b")));
    utassert(!IsValidProgramVersion(StrL("1..1")));
    utassert(!IsValidProgramVersion(StrL("1.1\r\n.1")));

    utassert(CompareProgramVersion(StrL("0.9.3.900"), StrL("0.9.3")) > 0);
    utassert(CompareProgramVersion(StrL("1.09.300"), StrL("1.09.3")) > 0);
    utassert(CompareProgramVersion(StrL("1.9.1"), StrL("1.09.3")) < 0);
    utassert(CompareProgramVersion(StrL("1.2.0"), StrL("1.2")) == 0);
    utassert(CompareProgramVersion(StrL("1.3.0"), StrL("2662")) < 0);
}

static void hexstrTest() {
    u8 buf[6] = {1, 2, 33, 255, 0, 18};
    u8 buf2[6]{};
    TempStr s = str::MemToHexTemp(Str((const char*)buf, dimofi(buf)));
    utassert(str::Eq(s, StrL("010221ff0012")));
    bool ok = str::HexToMem(s, Str((char*)buf2, dimofi(buf2)));
    utassert(ok);
    utassert(MemEq(buf, buf2, dimofi(buf)));

    FILETIME ft1{123, 456}, ft2;
    s = str::MemToHexTemp(Str((const char*)&ft1, sizeofi(ft1)));
    str::HexToMem(s, Str((char*)&ft2, sizeofi(ft2)));
    DWORD diff = FileTimeDiffInSecs(ft1, ft2);
    utassert(0 == diff);
    utassert(FileTimeEq(ft1, ft2));

    s = str::MemToHexTemp(Str());
    utassert(str::Eq(s, StrL("")));
    ok = str::HexToMem(s, Str());
    utassert(ok);
}

static void assertSerializedColor(Color c, Str s) {
    TempStr s2 = SerializeColorTemp(c);
    utassert(str::Eq(s2, s));
}

static void colorTest() {
    Color c = 0;
    bool ok = ParseColor(&c, StrL("0x01020304"));
    utassert(ok);
    assertSerializedColor(c, StrL("#01020304"));

    ok = ParseColor(&c, StrL("#01020304"));
    utassert(ok);
    assertSerializedColor(c, StrL("#01020304"));

    Color c2 = MkRgba(2, 3, 4, 1);
    assertSerializedColor(c2, StrL("#01020304"));
    utassert(c == c2);

    c2 = MkRgba(5, 7, 6, 8);
    assertSerializedColor(c2, StrL("#08050706"));
    ok = ParseColor(&c, StrL("#08050706"));
    utassert(ok);
    utassert(c == c2);
}

static void assertGoToNextPage3(int cmdId) {
    auto* cmd = FindCustomCommand(cmdId);
    utassert(cmd->origId == CmdGoToNextPage);
    auto* arg = GetCommandArg(cmd, kCmdArgN);
    utassert(arg->intVal == 3);
}

static void parseCommandsTest() {
    CommandArg* arg;

    {
        // names match case-insensitively, so a re-cased name keeps old shortcuts working
        utassert(GetCommandIdByName(StrL("CmdOpenWithFoxit")) == CmdOpenWithFoxit);
        utassert(GetCommandIdByName(StrL("CmdOpenWithFoxIt")) == CmdOpenWithFoxit);
        utassert(GetCommandIdByName(StrL("cmdopenwithfoxitphantom")) == CmdOpenWithFoxitPhantom);
    }
    {
        auto* cmd = CreateCommandFromDefinition(StrL(" CmdCreateAnnotHighlight   #00ff00 openEdit copytoclipboard"));
        utassert(cmd->origId == CmdCreateAnnotHighlight);

        arg = GetCommandArg(cmd, kCmdArgColor);
        utassert(arg != nullptr);
        arg = GetCommandArg(cmd, kCmdArgOpenEdit);
        utassert(arg != nullptr);
        utassert(GetCommandBoolArg(cmd, kCmdArgOpenEdit, false) == true);
        utassert(GetCommandBoolArg(cmd, kCmdArgCopyToClipboard, false) == true);
    }
    {
        auto* cmd = CreateCommandFromDefinition(StrL(" CmdCreateAnnotHighlight   #00ff00 OpenEdit=yes"));
        utassert(cmd->origId == CmdCreateAnnotHighlight);

        utassert(GetCommandArg(cmd, kCmdArgColor) != nullptr);
        utassert(GetCommandArg(cmd, kCmdArgOpenEdit) != nullptr);
        utassert(GetCommandBoolArg(cmd, kCmdArgOpenEdit, false) == true);
    }
    {
        auto* cmd = CreateCommandFromDefinition(StrL(" CmdCreateAnnotHighlight   #00ff00 OpenEdit=no"));
        utassert(cmd->origId == CmdCreateAnnotHighlight);

        utassert(GetCommandArg(cmd, kCmdArgColor) != nullptr);
        utassert(GetCommandArg(cmd, kCmdArgOpenEdit) != nullptr);
        utassert(GetCommandBoolArg(cmd, kCmdArgOpenEdit, true) == false);
    }
    {
        auto* cmd = CreateCommandFromDefinition(StrL("CmdCreateAnnotHighlight OpenEdit=bogus"));
        utassert(cmd == nullptr);
    }
    {
        auto* cmd = CreateCommandFromDefinition(StrL("CmdCreateAnnotHighlight OpenEdit: bogus"));
        utassert(cmd == nullptr);
    }
    {
        {
            auto* cmd = CreateCommandFromDefinition(StrL("CmdGoToNextPage 3"));
            assertGoToNextPage3(cmd->id);
        }
        {
            auto* cmd = CreateCommandFromDefinition(StrL("CmdGoToNextPage n 3"));
            assertGoToNextPage3(cmd->id);
        }
        {
            auto* cmd = CreateCommandFromDefinition(StrL("CmdGoToNextPage n: 3"));
            assertGoToNextPage3(cmd->id);
        }
        {
            auto* cmd = CreateCommandFromDefinition(StrL("CmdGoToNextPage n=3"));
            assertGoToNextPage3(cmd->id);
        }
    }
    {
        Str argStr = StrL(R"("C:\Program Files\FoxitReader\FoxitReader.exe" /A page=%p "%1)");
        Str s = str::JoinTemp(StrL("CmdExec   "), argStr);
        auto* cmd = CreateCommandFromDefinition(s);
        utassert(cmd->origId == CmdExec);
        auto* cmd2 = FindCustomCommand(cmd->id);
        utassert(cmd == cmd2);
        arg = GetCommandArg(cmd, kCmdArgExe);
        utassert(str::Eq(arg->strVal, argStr));
    }
    {
        Str argStr = StrL(R"("C:\Program Files\FoxitReader\FoxitReader.exe" /A page=%p "%1)");
        Str s = str::JoinTemp(StrL("CmdExec  filter: *.jpeg "), argStr);
        auto* cmd = CreateCommandFromDefinition(s);
        utassert(cmd->origId == CmdExec);
        auto* cmd2 = FindCustomCommand(cmd->id);
        utassert(cmd == cmd2);
        arg = GetCommandArg(cmd, kCmdArgExe);
        utassert(str::Eq(arg->strVal, argStr));
        arg = GetCommandArg(cmd, kCmdArgFilter);
        utassert(str::Eq(arg->strVal, StrL("*.jpeg")));
    }
}

static void DocPropertiesTest() {
    // gPropNames round-trips: first (Title=1), a middle one (FocalLength35mm=27)
    // and the last property (ImagePath=53), both directions.
    utassert(str::Eq(PropNameTemp(DocProp::Title), StrL("title")));
    utassert(str::Eq(PropNameTemp(DocProp::FocalLength35mm), StrL("focalLength35mm")));
    utassert(str::Eq(PropNameTemp(DocProp::ImagePath), StrL("imagePath")));
    utassert(PropFromName(StrL("title")) == DocProp::Title);
    utassert(PropFromName(StrL("focalLength35mm")) == DocProp::FocalLength35mm);
    utassert(PropFromName(StrL("imagePath")) == DocProp::ImagePath);
    // a couple more, plus unknown/None
    utassert(str::Eq(PropNameTemp(DocProp::CreationDate), StrL("creationDate")));
    utassert(PropFromName(StrL("modDate")) == DocProp::ModificationDate);
    utassert(PropFromName(StrL("bogusPropName")) == DocProp::None);
}

static void SumatraPDF_UnitTests() {
    CachedObjects_UnitTests();
    CommandPaletteModel_UnitTests();
    DocPropertiesTest();
    parseCommandsTest();
    colorTest();
    BenchRangeTest();
    ParseCommandLineTest();
    ParseFileArgsTest();
    versioncheck_test();
    hexstrTest();
}

static void ParseTipExpectWordsLinks(Str input, int expWords, int expLinks) {
    VirtRichText* tip = ParseTip(input);
    utassert(TipWordCount(tip) == expWords);
    utassert(TipLinkCount(tip) == expLinks);
    delete tip;
}

static void ParseTipExpectPlainContains(Str input, Str needle) {
    VirtRichText* tip = ParseTip(input);
    TempStr plain = tip->PlainTextTemp();
    utassert(plain && str::Contains(plain, needle));
    delete tip;
}

static void ParseTipExpectLinkCmd(Str input, Str expCmd) {
    VirtRichText* tip = ParseTip(input);
    utassert(TipLinkCount(tip) == 1);
    utassert(str::Eq(tip->links.next->cmd, expCmd));
    delete tip;
}

static int CountPaintedPixels(Pixmap* px) {
    int n = 0;
    for (int y = 0; y < px->height; y++) {
        u8* d = px->data + ((size_t)px->stride * (size_t)y);
        for (int x = 0; x < px->width; x++) {
            if (d[3] != 0) {
                n++;
            }
            d += 4;
        }
    }
    return n;
}

// issue #6186: an icon whose only paint is a <text> needs a base14 font, which
// mupdf only has once the embedded font loader is installed
static void SvgTextIcon_UnitTests() {
    const char* svgFmt =
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><text x="-1" y="24" font-size="32" font-family="sans-serif" fill="currentColor">%s</text></svg>)";

    // "A" is in Helvetica; the reporter's glyph needs the fallback font
    Str glyphs[] = {StrL("A"), StrL("\xE2\x96\xA6")};
    for (Str glyph : glyphs) {
        TempStr svg = fmt(svgFmt, glyph);
        Pixmap* px = GetCachedPixmapForSvg(svg, 24, 24, kColBlack, kColWhite);
        utassert(px != nullptr);
        utassert(CountPaintedPixels(px) > 0);
    }
}

static void ParseTip_UnitTests() {
    // issue #5752: brackets in filenames must not hang
    ParseTipExpectPlainContains(StrL("Loading Apocalypse Bringer Mynoghra_01 [CIW].pdf ..."), StrL("[CIW]"));

    // empty link text must not create a zero-word link (DrawTipWords crash)
    ParseTipExpectWordsLinks(StrL("[](CmdFoo)"), 1, 0);
    ParseTipExpectPlainContains(StrL("[](CmdFoo)"), StrL("[](CmdFoo)"));

    // URLs may contain balanced parentheses
    ParseTipExpectLinkCmd(StrL("[text](https://example.com/foo(bar))"), StrL("https://example.com/foo(bar)"));
    ParseTipExpectWordsLinks(StrL("[text](https://example.com/foo(bar))"), 1, 1);

    // Help/ link followed by trailing punctuation: the resolved URL must stop at
    // the link's ')' and not pull in the following ")." (the link cmd is a
    // non-NUL-terminated view into the tip line)
    ParseTipExpectLinkCmd(StrL("You can [extract text from PDF file](Help/Tool-x-extract-text-from-pdf)."),
                          StrL("https://www.sumatrapdfreader.org/docs/Tool-x-extract-text-from-pdf"));

    // nested brackets in link text
    ParseTipExpectWordsLinks(StrL("[foo [bar]](CmdFoo)"), 2, 1);
    ParseTipExpectPlainContains(StrL("[foo [bar]](CmdFoo)"), StrL("foo"));
    ParseTipExpectPlainContains(StrL("[foo [bar]](CmdFoo)"), StrL("[bar]"));

    // (Key/...) only expands for real commands
    ParseTipExpectPlainContains(StrL("file (Key/foo).pdf"), StrL("(Key/foo).pdf"));
    ParseTipExpectPlainContains(StrL("(Key/CmdCommandPalette)"), StrL("Ctrl"));
    ParseTipExpectPlainContains(StrL("(Key/CmdToggleKeyboardHelp)"), StrL("?"));

    // (Kbd/...) draws as a key-cap word; nests with (Key/...)
    {
        VirtRichText* tip = ParseTip(StrL("(Kbd/Cmd+Shift)"));
        utassert(TipWordCount(tip) == 1);
        utassert(tip->words.next->isKbd);
        utassert(str::Eq(tip->words.next->text, StrL("Cmd+Shift")));
        utassert(tip->HasRichContent());
        delete tip;
    }
    {
        VirtRichText* tip = ParseTip(StrL("(Kbd/(Key/CmdCommandPalette)): go"));
        utassert(TipWordCount(tip) >= 2);
        TipWord* w0 = tip->words.next;
        TipWord* w1 = w0->next;
        utassert(w0->isKbd);
        // expanded shortcut contains Ctrl (default binding)
        utassert(str::Contains(w0->text, StrL("Ctrl")));
        // ':' abuts the key-cap with no space
        utassert(w1->noSpaceBefore);
        utassert(str::Eq(w1->text, StrL(":")));
        delete tip;
    }

    // whitespace: tab and newline break words
    ParseTipExpectWordsLinks(StrL("line1\nline2"), 2, 0);
    ParseTipExpectWordsLinks(StrL("tab\there"), 2, 0);

    // ordinary tips still work
    ParseTipExpectWordsLinks(StrL("before [valid](CmdFoo)"), 2, 1);
    ParseTipExpectWordsLinks(StrL("[valid](CmdFoo) after"), 2, 1);

    // GHSA-2wv2-qm2f-vmxh: a file name can contain the markup, so text from
    // outside the app must never become a link. AddPlainText / AddPlainLink are
    // how such text gets in
    {
        Str evil = StrL("a[b](CmdExec calc.exe)c");
        VirtRichText* tip = new VirtRichText();
        tip->AddPlainText(evil);
        utassert(TipLinkCount(tip) == 0);
        utassert(str::Contains(tip->PlainTextTemp(), StrL("(CmdExec")));
        delete tip;

        // the same text as a link: exactly one link, and to our command
        tip = new VirtRichText();
        tip->AddPlainLink(evil, StrL("CmdOpenNextFileInFolder"));
        utassert(TipLinkCount(tip) == 1);
        utassert(str::Eq(tip->links.next->cmd, StrL("CmdOpenNextFileInFolder")));
        delete tip;

        // mixing our markup with outside text keeps them apart
        tip = new VirtRichText();
        ParseTipInto(tip, StrL("open"));
        tip->AddPlainLink(evil, StrL("CmdOpenNextFileInFolder"));
        ParseTipInto(tip, StrL("[browse](CmdNavigateFilesInFolder)"));
        utassert(TipLinkCount(tip) == 2);
        utassert(str::Eq(tip->links.next->cmd, StrL("CmdOpenNextFileInFolder")));
        utassert(str::Eq(tip->links.next->next->cmd, StrL("CmdNavigateFilesInFolder")));
        delete tip;
    }
}

static void FormAuthoringTest() {
    const char* objects[] = {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
                             "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Rotate 90 >>"};
    str::Builder pdf;
    pdf.Append(StrL("%PDF-1.7\n"));
    int offsets[3];
    for (int i = 0; i < 3; i++) {
        offsets[i] = len(pdf);
        pdf.Append(fmt("%d 0 obj\n%s\nendobj\n", i + 1, Str(objects[i])));
    }
    int xref = len(pdf);
    pdf.Append(StrL("xref\n0 4\n0000000000 65535 f \n"));
    for (int offset : offsets) pdf.Append(fmt("%010d 00000 n \n", offset));
    pdf.Append(fmt("trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n", xref));
    EngineBase* e = CreateEngineMupdfFromData(ToStrTemp(pdf), StrL("form-authoring.pdf"), nullptr);
    utassert(e != nullptr);
    Str error;
    FormFieldProperties p;
    Annotation* fields[5]{};
    for (int i = 0; i < 5; i++) {
        p = {};
        p.kind = (FormFieldKind)i;
        p.name = fmt("Field%d", i);
        p.options = StrL("First\nSecond");
        fields[i] = CreateFormField(e, 1, {40.f, 40.f + 70.f * i, 160.f, 50.f}, p, error);
        utassert(fields[i] && AnnotationIsLive(fields[i]));
    }
    utassert(EngineMupdfCountFormFields(e) == 5);
    Vec<Annotation*> widgets;
    EngineMupdfGetPageWidgets(e, 1, widgets);
    utassert(len(widgets) == 6);
    Vec<Annotation*> removed;
    utassert(EngineMupdfUndo(e, removed));
    for (Annotation* a : removed) DeleteAnnotation(a);
    utassert(EngineMupdfCountFormFields(e) == 4);
    VecClear(removed);
    utassert(EngineMupdfRedo(e, removed));
    utassert(len(removed) == 0 && EngineMupdfCountFormFields(e) == 5);
    EngineMupdfGetPageWidgets(e, 1, widgets);
    Vec<RectF> highlights;
    EngineMupdfGetFormFieldHighlightRects(e, 1, nullptr, highlights);
    utassert(len(highlights) == 6);
    utassert(ToggleFormButton(fields[1]));
    utassert(str::Eq(GetWidgetValue(fields[1]), StrL("Yes")));
    utassert(ToggleFormButton(fields[2]));
    utassert(str::Eq(GetWidgetValue(fields[2]), StrL("First")));
    utassert(ToggleFormButton(widgets[3]));
    utassert(str::Eq(GetWidgetValue(fields[2]), StrL("Second")));
    utassert(SetWidgetChoiceValue(fields[3], StrL("Second")));
    utassert(GetFormFieldProperties(fields[0], p));
    p.name = StrL("Customer");
    p.defaultValue = StrL("Ada");
    p.required = true;
    p.readOnly = true;
    p.fontSize = 18;
    utassert(SetFormFieldProperties(fields[0], p, error));
    utassert(GetWidgetFieldFlags(fields[0]) & PDF_FIELD_IS_READ_ONLY);
    utassert(GetWidgetFieldFlags(fields[0]) & PDF_FIELD_IS_REQUIRED);
    utassert(GetWidgetFontSize(fields[0]) == 18);
    utassert(str::Eq(GetWidgetValue(fields[0]), StrL("Ada")));
    p.kind = FormFieldKind::Text;
    utassert(!CreateFormField(e, 1, {10, 10, 20, 20}, p, error));
    RectF moved{85, 95, 220, 36};
    SetRect(fields[0], moved);
    utassert(GetBounds(fields[0]) == moved);
    utassert(GetFormFieldProperties(fields[2], p));
    p.options = StrL("Red\nBlue");
    p.defaultValue = StrL("Blue");
    utassert(SetFormFieldProperties(fields[2], p, error));
    p.options = StrL("Red\nBlue\nGreen");
    utassert(!SetFormFieldProperties(fields[2], p, error));
    TempStr recovery = GetTempFilePathTemp(StrL("recovery"));
    utassert(EngineMupdfAutoSave(e, recovery));
    utassert(EngineHasUnsavedAnnotations(e));
    Str snapshot = file::ReadFile(recovery);
    utassert(len(snapshot) > 0);
    FILETIME snapshotTime = file::GetModificationTime(recovery);
    utassert(EngineMupdfAutoSave(e, recovery));
    utassert(FileTimeEq(snapshotTime, file::GetModificationTime(recovery)));
    EngineBase* recovered = CreateEngineMupdfFromFile(recovery, FileType::PDF, 96);
    utassert(recovered && EngineMupdfCountFormFields(recovered) == 5);
    SafeEngineRelease(&recovered);
    utassert(EngineMupdfUndo(e, removed));
    EngineMupdfRefreshModifiedState(e);
    utassert(EngineMupdfRedo(e, removed));
    EngineMupdfRefreshModifiedState(e);
    utassert(EngineMupdfAutoSave(e, recovery));
    str::Free(snapshot);

    TempStr path = GetTempFilePathTemp(StrL("form-authoring"));
#if OS_WIN
    HANDLE locked = CreateFileW(CWStrTemp(path), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    utassert(locked != INVALID_HANDLE_VALUE);
    utassert(!EngineMupdfSaveUpdated(e, path, {}));
    utassert(EngineHasUnsavedAnnotations(e) && file::Exists(recovery));
    utassert(file::GetSize(path) == 0);
    CloseHandle(locked);
#endif
    utassert(EngineMupdfSaveUpdated(e, path, {}));
    utassert(!file::Exists(recovery));
    SafeEngineRelease(&e);

    e = CreateEngineMupdfFromFile(path, FileType::PDF, 96);
    utassert(e != nullptr);
    EngineMupdfGetPageWidgets(e, 1, widgets);
    utassert(len(widgets) == 6 && EngineMupdfCountFormFields(e) == 5);
    utassert(GetFormFieldProperties(widgets[0], p));
    utassert(str::Eq(p.name, StrL("Customer")) && str::Eq(p.defaultValue, StrL("Ada")));
    utassert(p.required && p.readOnly && p.fontSize == 18);
    utassert(GetBounds(widgets[0]) == moved);
    utassert(str::Eq(GetWidgetValue(widgets[2]), StrL("Blue")));
    utassert(str::Eq(GetWidgetValue(widgets[4]), StrL("Second")));
    utassert(GetWidgetType(widgets[5]) == PDF_WIDGET_TYPE_SIGNATURE);
    utassert(EngineMupdfSaveCopy(e, recovery));
    DeleteAnnotation(widgets[1]);
    utassert(EngineMupdfCountFormFields(e) == 4);
    utassert(EngineMupdfSaveUpdated(e, path, {}));
    utassert(SetWidgetChoiceValue(widgets[4], StrL("First")));
    utassert(EngineMupdfSaveUpdated(e, path, {}));
    utassert(SetWidgetChoiceValue(widgets[4], StrL("Second")));
    utassert(EngineMupdfSaveUpdated(e, path, {}));
    file::Delete(recovery);
    SafeEngineRelease(&e);

    // Open the saved file directly, independent of the application's wrappers.
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    pdf_document* doc = nullptr;
    pdf_page* page = nullptr;
    bool valid = false;
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(path));
        page = pdf_load_page(ctx, doc, 0);
        pdf_obj* roots = pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/AcroForm/Fields");
        int count = 0;
        int radioOn = 0;
        for (pdf_annot* a = pdf_first_widget(ctx, page); a; a = pdf_next_widget(ctx, a)) {
            count++;
            utassert(pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(P)));
            utassert(pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(AP)));
            if (pdf_widget_type(ctx, a) == PDF_WIDGET_TYPE_RADIOBUTTON) {
                pdf_obj* state = pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(AS));
                if (!pdf_name_eq(ctx, state, PDF_NAME(Off))) {
                    utassert(str::Eq(Str(pdf_to_name(ctx, state)), StrL("Blue")));
                    radioOn++;
                }
            }
        }
        valid = count == 5 && pdf_array_len(ctx, roots) == 4 && radioOn == 1;
#if OS_WIN
        TempStr target = GetTempFilePathTemp(StrL("atomic-pdf"));
        utassert(file::WriteFile(target, StrL("previous file")));
        HANDLE lockedTarget = CreateFileW(CWStrTemp(target), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, 0, nullptr);
        utassert(lockedTarget != INVALID_HANDLE_VALUE);
        bool rejected = false;
        fz_var(rejected);
        fz_try(ctx) {
            pdf_save_document(ctx, doc, CStrTemp(target), nullptr);
        }
        fz_catch(ctx) {
            rejected = true;
        }
        utassert(rejected);
        Str previous = file::ReadFile(target);
        utassert(str::Eq(previous, StrL("previous file")));
        str::Free(previous);
        CloseHandle(lockedTarget);
        pdf_save_document(ctx, doc, CStrTemp(target), nullptr);
        EngineBase* published = CreateEngineMupdfFromFile(target, FileType::PDF, 96);
        utassert(published && EngineMupdfCountFormFields(published) == 4);
        SafeEngineRelease(&published);
        file::Delete(target);
#endif
    }
    fz_always(ctx) {
        fz_drop_page(ctx, (fz_page*)page);
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    e = CreateEngineMupdfFromFile(path, FileType::PDF, 96);
    utassert(e != nullptr);
    EngineMupdfGetPageWidgets(e, 1, widgets);
    DeleteAnnotation(widgets[1]);
    DeleteAnnotation(widgets[2]);
    utassert(EngineMupdfCountFormFields(e) == 3);
    utassert(EngineMupdfSaveUpdated(e, path, {}));
    SafeEngineRelease(&e);
    e = CreateEngineMupdfFromFile(path, FileType::PDF, 96);
    utassert(e != nullptr && EngineMupdfCountFormFields(e) == 3);
    SafeEngineRelease(&e);
    file::Delete(path);
    utassert(valid);
}

static void PageStampsTest() {
    const char* objects[] = {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Count 2 /Kids [3 0 R 4 0 R] >>",
                             "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
                             "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>"};
    str::Builder pdf;
    pdf.Append(StrL("%PDF-1.7\n"));
    int offsets[4];
    for (int i = 0; i < 4; i++) {
        offsets[i] = len(pdf);
        pdf.Append(fmt("%d 0 obj\n%s\nendobj\n", i + 1, Str(objects[i])));
    }
    int xref = len(pdf);
    pdf.Append(StrL("xref\n0 5\n0000000000 65535 f \n"));
    for (int offset : offsets) pdf.Append(fmt("%010d 00000 n \n", offset));
    pdf.Append(fmt("trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n", xref));

    TempStr basePdf = GetTempFilePathTemp(StrL("base-stamps.pdf"));
    utassert(file::WriteFile(basePdf, ToStrTemp(pdf)));

    // 1. Watermark test (apply to page 1 only)
    {
        PdfWatermarkParams wm;
        wm.text = StrL("CONFIDENTIAL");
        wm.opacity = 0.5f;
        wm.angle = 45.0f;
        wm.position = PdfWatermarkPos::Center;
        wm.fontSize = 36.0f;
        wm.color = RGB(255, 0, 0);
        wm.pagesSpec = StrL("1");

        TempStr wmPdf = GetTempFilePathTemp(StrL("wm-test.pdf"));
        bool ok = PdfApplyWatermark(basePdf, wmPdf, wm);
        utassert(ok);

        EngineBase* e = CreateEngineMupdfFromFile(wmPdf, FileType::PDF, 96);
        utassert(e != nullptr);
        utassert(e->PageCount() == 2);
        PageText pt1 = e->ExtractPageText(1);
        PageText pt2 = e->ExtractPageText(2);
        utassert(str::Contains(pt1.text, StrL("CONFIDENTIAL")));
        utassert(!str::Contains(pt2.text, StrL("CONFIDENTIAL")));
        FreePageText(&pt1);
        FreePageText(&pt2);
        SafeEngineRelease(&e);
#if OS_WIN
        Str before = file::ReadFile(wmPdf);
        HANDLE lockedStamp = CreateFileW(CWStrTemp(wmPdf), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                         OPEN_EXISTING, 0, nullptr);
        utassert(lockedStamp != INVALID_HANDLE_VALUE);
        utassert(!PdfApplyWatermark(wmPdf, wmPdf, wm));
        Str after = file::ReadFile(wmPdf);
        utassert(str::Eq(before, after));
        str::Free(before);
        str::Free(after);
        CloseHandle(lockedStamp);
        utassert(PdfApplyWatermark(wmPdf, wmPdf, wm));
#endif
        file::Delete(wmPdf);
    }

    // 2. Header / Footer test (apply to page 2 only)
    {
        PdfHeaderFooterParams hf;
        hf.headerLeft = StrL("LeftHeader");
        hf.headerCenter = StrL("CenterHeader");
        hf.headerRight = StrL("Doc: {filename}");
        hf.footerLeft = StrL("Page {page} of {pages}");
        hf.footerCenter = StrL("Date: {date}");
        hf.footerRight = StrL("RightFooter");
        hf.fontSize = 12.0f;
        hf.color = RGB(0, 0, 0);
        hf.pagesSpec = StrL("2");

        TempStr hfPdf = GetTempFilePathTemp(StrL("hf-test.pdf"));
        bool ok = PdfApplyHeaderFooter(basePdf, hfPdf, hf);
        utassert(ok);

        EngineBase* e = CreateEngineMupdfFromFile(hfPdf, FileType::PDF, 96);
        utassert(e != nullptr);
        utassert(e->PageCount() == 2);
        PageText pt1 = e->ExtractPageText(1);
        PageText pt2 = e->ExtractPageText(2);
        utassert(!str::Contains(pt1.text, StrL("Page 1 of 2")));
        utassert(!str::Contains(pt1.text, StrL("LeftHeader")));
        utassert(str::Contains(pt2.text, StrL("Page 2 of 2")));
        utassert(str::Contains(pt2.text, StrL("LeftHeader")));
        utassert(str::Contains(pt2.text, StrL("CenterHeader")));
        utassert(str::Contains(pt2.text, StrL("RightFooter")));
        FreePageText(&pt1);
        FreePageText(&pt2);
        SafeEngineRelease(&e);
        file::Delete(hfPdf);
    }

    // 3. Bates numbering test (apply to all pages)
    {
        PdfBatesParams bp;
        bp.prefix = StrL("BATES-");
        bp.startNumber = 100;
        bp.digitCount = 5;
        bp.position = PdfBatesPos::BottomRight;
        bp.fontSize = 10.0f;
        bp.color = RGB(0, 0, 128);
        bp.pagesSpec = StrL("all");

        TempStr batesPdf = GetTempFilePathTemp(StrL("bates-test.pdf"));
        bool ok = PdfApplyBatesNumbering(basePdf, batesPdf, bp);
        utassert(ok);

        EngineBase* e = CreateEngineMupdfFromFile(batesPdf, FileType::PDF, 96);
        utassert(e != nullptr);
        utassert(e->PageCount() == 2);
        PageText pt1 = e->ExtractPageText(1);
        PageText pt2 = e->ExtractPageText(2);
        utassert(str::Contains(pt1.text, StrL("BATES-00100")));
        utassert(str::Contains(pt2.text, StrL("BATES-00101")));
        FreePageText(&pt1);
        FreePageText(&pt2);
        SafeEngineRelease(&e);
        file::Delete(batesPdf);
    }

    file::Delete(basePdf);
}

static LONG WINAPI ForAiCrashHandler(EXCEPTION_POINTERS* ei) {
    printf("unit tests crash\n");
    str::Builder s;
    dbghelp::GetExceptionInfo(s, ei);
    Str info = ToStr(s);
    printf("%.*s", info.len, info.s);
    fflush(stdout);
    ExitProcess(7);
    return EXCEPTION_EXECUTE_HANDLER;
}

// -for-ai: print assertion and crash callstacks to stdout instead of breaking
// into a debugger, so a script can report the failure
static void SetupForAi() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    utassert_set_for_ai(true);
    dbghelp::Initialize(ToWStrTemp(GetSelfExeDirTemp()), true);
    SetUnhandledExceptionFilter(ForAiCrashHandler);
}

int RunAppUnitTests(bool forAi) {
    if (forAi) {
        SetupForAi();
    }
    printf("Running unit tests\n");
    utassert(Ocr_UnitTests());
    DocumentPanel_UnitTests();
    PanelLayout_UnitTests();
    Animation_UnitTests();
    RibbonAccess_UnitTests();
    Settings* panels = NewSettings(StrL("ToolsPanelWidth = 336\nCommentsPanelWidth = 412\n"));
    utassert(panels->toolsPanelWidth == 336 && panels->commentsPanelWidth == 412);
    Str savedPanels = SerializeSettings(panels, {});
    Settings* restoredPanels = NewSettings(savedPanels);
    utassert(restoredPanels->toolsPanelWidth == 336 && restoredPanels->commentsPanelWidth == 412);
    DeleteSettings(restoredPanels);
    DeleteSettings(panels);
    str::Free(savedPanels);
    printf("Panel layout, settings and animation tests passed\n");
    utassert(EngineMupdf_UnitTestPanelData());
    FormAuthoringTest();
    PageStampsTest();

    ArchiveTest();
    AppendStoreTest();
    BaseUtilTest();
    ByteOrderTests();
    ClipboardImageTest();
    CryptoUtilTest();
    CssParser_UnitTests();
    DictTest();
    DirRemoveAllTest();
    FileUtilTest();
    UpdateTempFileTests();
    GuessFileTypeTest();
    JsonTest();
    RefHoverTest();
    SettingsUtilTest();
    SimpleLogTest();
    SquareTreeTest();
    StrFormatTest();
    StrTest();
    StrVecTest();
    VecTest();
    WinUtilTest();

    ChapterTable_UnitTests();
    LitDoc_UnitTests();
    MobiDoc_UnitTests();
    PagePosition_UnitTests();
    PdfSync_UnitTests();
    utassert(PdfDarkModeImageStats_UnitTest());
    SumatraPDF_UnitTests();

    ParseTip_UnitTests();
    SvgTextIcon_UnitTests();
#if IS_DEBUG
    TextSelection_UnitTests();
    EngineDjvuDec_UnitTests();
    Layout_UnitTests();
    LayoutWin_UnitTests();
    VirtCtrl_UnitTests();
    utassert(TableOfContents_UnitTestSnapshotNamedDest());
    utassert(MarkdownModel_UnitTestBrowserNavigationUrl());
    utassert(MarkdownToc_UnitTestHtmlLinks());
    utassert(MarkdownToc_UnitTestHtmlHeadings());
    utassert(MarkdownToc_UnitTestMermaid());
    utassert(EbookDoc_UnitTestNormalizeURL());
    utassert(EbookDoc_UnitTestLoading());
    utassert(GumboHtmlParser_UnitTest());
    utassert(DisplayMode_UnitTestZoom());
    utassert(ExternalViewers_UnitTestPDFXChangePaths());
    utassert(Canvas_UnitTestScrollLineAmount());
    utassert(EngineMupdf_UnitTestEbookLineSpacingCss());
    utassert(EngineMupdf_UnitTestEbookFontFamilyCss());
    utassert(EngineMupdf_UnitTestEbookMarginCss());
    utassert(EngineMupdf_UnitTestMergeEBookUI());
    utassert(EngineMupdf_UnitTestPageLabels());
    utassert(Accelerators_UnitTestFolderNavIsSafe());
    utassert(Accelerators_UnitTestTreeTakesLetters());
    utassert(Accelerators_UnitTestCreateAnnotEdit());
    utassert(Accelerators_UnitTestCustomShortcutShown());
    utassert(ShortcutParse_UnitTestShiftedPunct());
    utassert(AnnotSearch_UnitTests());
    utassert(RenderCache_UnitTestCookieUnlocked());
    ReadAloudHighlight_UnitTests();
#endif
    return utassert_print_results();
}

#endif
