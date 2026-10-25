/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "base/Base.h"
#include "base/LzmaSimpleArchive.h"
#include "EmbeddedResources.h"

#include "SumatraConfig.h"
#include "Version.h"

#include "Translations.h"

namespace trans {

extern int FindLangIdx(Str code);
#if OS_WIN
extern LANGID GetLangIdByIdx(int idx);
#endif
extern bool IsLangRtl(int langIdx);

// A view into the shared language table.
static Str gCurrLangCode;
static int gCurrLangIdx = 0;
// for each translation: english string followed by a translation
static StrVec* gTranslationCache = nullptr;

static TempStr UnescapeTemp(Str sOrig) {
    TempStr s = str::DupTemp(sOrig);
    char* unescaped = s.s;
    char* dst = s.s;
    char* src = s.s;
    char c, c2;
    while (*src) {
        c = *src++;
        if (c != '\\') {
            *dst++ = c;
            continue;
        }
        c2 = *src;
        switch (c2) {
            case '\\':
                *dst++ = '\\';
                break;
            case 't':
                *dst++ = '\t';
                break;
            case 'n':
                *dst++ = '\n';
                break;
            case 'r':
                *dst++ = '\r';
                break;
            default:
                *dst++ = c;
                break;
        }
        src++;
    }
    *dst = 0;
    return s;
}

static void FreeTranslations() {
    delete gTranslationCache;
    gTranslationCache = nullptr;
}

static void ParseTranslationsTxt(Str d, Str langCode) {
    TempStr langCodePref = str::JoinTemp(langCode, StrL(":"));

    StrVec lines;
    Split(&lines, d, StrL("\n"), true);
    int nStrings = 0;
    for (Str l : lines) {
        if (l && l.s[0] == ':') {
            nStrings++;
        }
    }
    int nLines = len(lines);
    logf("ParseTranslationsTxt: %d lines, nStrings: %d\n", nLines, nStrings);

    delete gTranslationCache;
    gTranslationCache = new StrVec();
    auto* c = gTranslationCache;
    int nUntranslated = 0;

    Str orig;
    Str trans;
    int i = 2; // skip first 2 header lines
    while (i < nLines) {
        Str origLine = lines[i];
        ReportDebugIf(len(origLine) == 0 || origLine.s[0] != ':');
        orig = Str(origLine.s + 1, origLine.len - 1);
        i++;
        trans = {};
        while (i < nLines && lines[i] && lines[i].s[0] != ':') {
            if (len(trans) == 0) {
                Str line = lines[i];
                if (str::TrimPrefix(line, langCodePref)) {
                    trans = line;
                }
            }
            i++;
        }
        if (len(trans) == 0) {
            nUntranslated++;
        }
        TempStr unescaped = UnescapeTemp(orig);
        c->Append(unescaped);
        if (len(trans) == 0) {
            c->Append({});
            continue;
        }
        unescaped = UnescapeTemp(trans);
        c->Append(unescaped);
    }
    ReportDebugIf(len(*c) != nStrings * 2);
    if (nUntranslated > 0 && !str::Eq(langCodePref, StrL("en:"))) {
        logf("Untranslated strings: %d for lang '%s'\n", nUntranslated, langCodePref);
    }
}

// SwiftLeaf: the UI strings name the app "SwiftLeaf"; translations (made for the
// upstream app) still say "SwiftLeaf" and are rebranded here.
// Pairs of original, rebranded string, made once per string.
static StrVec* gRebrandedCache = nullptr;

static Str Rebrand(Str s) {
    if (!str::Contains(s, StrL("SumatraPDF"))) {
        return s;
    }
    if (!gRebrandedCache) {
        gRebrandedCache = new StrVec();
    }
    StrVec* c = gRebrandedCache;
    for (int i = 0; i + 1 < len(*c); i += 2) {
        if (str::Eq(c->At(i), s)) {
            return c->At(i + 1);
        }
    }
    c->Append(s);
    c->Append(str::ReplaceTemp(s, StrL("SumatraPDF"), StrL("SwiftLeaf")));
    return c->At(len(*c) - 1);
}

static bool IsKnownNewString(Str s) {
    static const char* known[] = {
        "Recommended",
        "Export", "Create", "Edit", "Organize", "Review", "Forms",
        "Protect & Redact", "Optimize", "Sign", "Scan/OCR", "Standards",
        "Export a PDF", "Save a copy of the open document.",
        "To images", "Export pages as image files.",
        "Fill & Sign", "Fill a form or add a drawn signature.",
        "Edit a PDF", "Edit text and objects in the document.",
        "Create a PDF", "Create a PDF from files or clipboard content.",
        "Combine files", "Join files into one PDF.",
        "Organize pages", "Reorder, rotate, or remove pages.",
        "Add comments", "Add notes and markups to pages.",
        "Scan & OCR", "Recognize text in scanned pages.",
        "Protect a PDF", "Set a password and permissions.",
        "Redact a PDF", "Permanently remove sensitive content.",
        "Compress a PDF", "Reduce the document file size.",
        "Prepare a form", "Add fillable form fields.",
        "Detect fields", "Find likely form fields on the page.",
        "Add a stamp", "Place a stamp annotation on a page.",
        "Use a certificate", "Sign the document with a certificate.",
        "Add watermark", "Place text or an image behind page content.",
        "Add header/footer", "Add text to page headers or footers.",
        "Bates numbering", "Add sequential Bates numbers.",
        "Sanitize document", "Remove hidden data and document metadata.",
        "Redact all matches", "Find and redact matching text.",
        "Measure", "Measure distances on the page.",
        "Review & comments", "Review, summarize, and export comments.",
        "Crop pages", "Set page crop boxes and remove margins.",
        "N-up pages", "Arrange multiple pages on each sheet.",
        "Page background", "Set a page color or image background.",
        "Duplicate pages", "Copy selected pages in the document.",
        "Document properties", "Edit title, author, and other metadata.",
        "Attachments", "View and manage embedded files.",
        "Edit bookmarks", "Create and organize document bookmarks.",
        "Page numbering", "Change page labels and numbering.",
        "Repair PDF", "Repair damaged PDF structure when possible.",
        "Document layers", "Show and manage optional content layers.",
        "Accessibility checker", "Check tags, reading order, language, and alternative text.",
        "Accessibility fixes", "Add document language, title, and basic structure tags.",
        "Convert to PDF/A", "Convert the document to a PDF/A archive format.",
        "Validate PDF/A", "Check the document against PDF/A requirements.",
        "Preflight", "Inspect fonts, colors, images, and page boxes.",
        "Convert colors", "Convert document colors to grayscale or RGB.",
        "Acquire from scanner", "Scan pages and create a PDF.",
        "Import camera image", "Import and straighten a camera photo.",
        "Batch OCR", "Recognize text across multiple documents.",
        "Digital signatures", "Review document signatures and certificate status.",
        "Certify document", "Apply a document certification signature.",
        "Select", "Select text and objects.",
        "Hand", "Pan around the page.",
        "Add a note", "Add a note annotation.",
        "Highlight", "Highlight text on the page.",
        "Draw", "Draw freehand on the page.",
        "Add text", "Add text to the page.",
        "All tools", "Read", "Edit", "Convert", "E-Sign",
        "Quick tour: 1. Open a document. 2. Search recent files. 3. Browse tools by category. 4. Star your favorites. Double-click this card to dismiss.",
        "Quick tour: 1. Open a document. 2. Search recent files. 3. Browse tools by category. 4. Star your favorites. Double-click to dismiss.",
        "Dismiss quick tour",
        "Search tools by name or description",
        "Find tools and commands",
        "Close tools panel",
        "Add to favorites", "Remove from favorites",
        "View more",
        "More options",
        "Panels and all tools",
        "Save changes to the PDF",
        "Menu",
    };
    for (const char* k : known) {
        if (str::Eq(s, Str(k))) return true;
    }
    return false;
}

static Str GetTranslationRaw(Str s);

// don't free the result
Str GetTranslation(Str s) {
    return Rebrand(GetTranslationRaw(s));
}

static Str GetTranslationRaw(Str s) {
    if (gCurrLangIdx == 0) {
        // 0 is english, no translation needed
        return s;
    }
    auto* c = gTranslationCache;
    if (!c) {
        // translations failed to load (e.g. corrupted resource data)
        return s;
    }
    int n = len(*c);
    ReportDebugIf(n % 2 != 0);
    n = n / 2;
    // the translation keys were made for the upstream app, which called itself
    // "SwiftLeaf": match a rebranded source string against its original key
    Str legacy =
        str::Contains(s, StrL("SwiftLeaf")) ? Str(str::ReplaceTemp(s, StrL("SwiftLeaf"), StrL("SumatraPDF"))) : s;
    int sLen = len(s);
    int legacyLen = len(legacy);
    for (int i = 0; i < n; i++) {
        int idx = i * 2;
        Str s2 = c->At(idx);
        if ((s2.len == sLen && str::Eq(s, s2)) || (s2.len == legacyLen && str::Eq(legacy, s2))) {
            Str tr = c->At(idx + 1);
            if (len(tr) == 0) {
                logf("Didn't find translation for '%s'\n", s);
                return s;
            }
            // special case of "Change Language"
            // if we accidentally change language, we want be able to
            // change it back so add ("Change Language") to translation
            if (str::ContainsI(s, StrL("Change Language")) && !str::ContainsI(tr, StrL("Change Language"))) {
                tr = str::JoinTemp(tr, StrL(" (Change Language)"));
            }
            return tr;
        }
    }
    if (IsKnownNewString(s)) {
        return s;
    }
    ReportDebugIf(true);
    return s;
}

Str GetCurrentLangCode() {
    return gCurrLangCode;
}

// when we can't load translations, reset the language to english so that
// GetTranslation() doesn't try to use a gTranslationCache that was never built
static void FallbackToEnglish() {
    gCurrLangIdx = 0;
    gCurrLangCode = GetLangCodeByIdxTemp(0);
}

void SetCurrentLangByCode(Str langCode) {
    if (str::Eq(langCode, gCurrLangCode)) {
        return;
    }

    int idx = FindLangIdx(langCode);
    if (idx < 0) {
        logf("SetCurrentLangByCode: unknown lang code: '%s'\n", langCode);
        // set to English
        idx = 0;
    }
    ReportDebugIf(-1 == idx);
    gCurrLangIdx = idx;
    gCurrLangCode = GetLangCodeByIdxTemp(idx);
    if (idx == 0 && !gIsDebugBuild) {
        // perf: in release builds we skip parsing translations for english
        // in debug we want to execute this code to catch errors
        return;
    }
    int dataSize = 0;
    u8* data = GetEmbeddedFileData(StrL("translations.txt"), &dataSize);
    if (!data) {
        logf("SetCurrentLangByCode: translations.txt not found in IDR_EMBEDDED_PAK\n");
        FallbackToEnglish();
        return;
    }
    logf("SetCurrentLangByCode: translations.txt uncompressed=%d\n", dataSize);
    // empty file is expected when TRANS_UPLOAD_SECRET was missing at build time
    if (dataSize <= 0) {
        logf("SetCurrentLangByCode: translations.txt is empty (no translations available)\n");
        free(data);
        FallbackToEnglish();
        return;
    }
    Str d = Str((char*)data, dataSize);
    // whitespace-only / header-only with no strings: treat as empty
    bool hasString = false;
    for (int i = 0; i < d.len; i++) {
        if (d.s[i] == ':' && (i == 0 || d.s[i - 1] == '\n')) {
            hasString = true;
            break;
        }
    }
    if (!hasString) {
        logf("SetCurrentLangByCode: translations.txt has no strings (no translations available)\n");
        free(data);
        FallbackToEnglish();
        return;
    }
    ParseTranslationsTxt(d, langCode);
    free(data);
}

Str ValidateLangCode(Str langCode) {
    if (len(langCode) == 0) return {};
    int idx = FindLangIdx(langCode);
    if (idx < 0) {
        return {};
    }
    return GetLangCodeByIdxTemp(idx);
}

bool IsCurrLangRtl() {
    return IsLangRtl(gCurrLangIdx);
}

#if !OS_WIN
// ng: the win32 half below maps a LANGID to our language code. POSIX has no
// LANGID; $LC_ALL / $LANG ("de_DE.UTF-8") name the language directly.
Str DetectUserLang() {
    const char* envNames[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* name : envNames) {
        const char* v = getenv(name);
        if (!v || !*v) {
            continue;
        }
        Str s = Str((char*)v);
        // "de_DE.UTF-8" -> try "de-de", then "de"
        int n = 0;
        while (n < s.len && s.s[n] != '.' && s.s[n] != '@') {
            n++;
        }
        TempStr code = str::DupTemp(Str(s.s, n));
        str::ToLowerInPlace(code);
        for (int i = 0; i < len(code); i++) {
            if (code.s[i] == '_') {
                code.s[i] = '-';
            }
        }
        int idx = FindLangIdx(code);
        if (idx < 0) {
            int dash = str::IndexOfChar(code, '-');
            if (dash > 0) {
                idx = FindLangIdx(Str(code.s, dash));
            }
        }
        if (idx >= 0) {
            return GetLangCodeByIdxTemp(idx);
        }
    }
    return StrL("en");
}
#else
Str DetectUserLang() {
    LANGID langId = GetUserDefaultUILanguage();
    // try the exact match
    for (int i = 0; i < GetLangsCount(); i++) {
        if (langId == GetLangIdByIdx(i)) {
            return GetLangCodeByIdxTemp(i);
        }
    }

    // see if we have a translation in a language that has the same
    // primary id as user's language and neutral sublang
    LANGID userLangIdNeutral = MAKELANGID(PRIMARYLANGID(langId), SUBLANG_NEUTRAL);
    for (int i = 0; i < GetLangsCount(); i++) {
        if (userLangIdNeutral == GetLangIdByIdx(i)) {
            return GetLangCodeByIdxTemp(i);
        }
    }

    return StrL("en");
}
#endif

void Destroy() {
    delete gRebrandedCache;
    gRebrandedCache = nullptr;
    FreeTranslations();
}

} // namespace trans

Str Tr(Str s) {
    return trans::GetTranslation(s);
}
