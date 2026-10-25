/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "base/Base.h"
#include "base/File.h"
#include "base/Pixmap.h"
#include "base/ScopedWin.h"
#include "base/HtmlTags.h"
#include "base/CssParser.h"
#include "base/GuessFileType.h"

extern "C" {
#include <mupdf/pdf.h>
}

#include "gui/UIModels.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "EngineMupdf.h"
#include "AppSettings.h"
#include "Commands.h"
#include "Translations.h"
#include "Annotation.h"

// spot checks the definitions are the same
static_assert((int)AnnotationType::Link == (int)PDF_ANNOT_LINK);
static_assert((int)AnnotationType::ThreeD == (int)PDF_ANNOT_3D);
static_assert((int)AnnotationType::Sound == (int)PDF_ANNOT_SOUND);
static_assert((int)AnnotationType::Unknown == (int)PDF_ANNOT_UNKNOWN);

// clang-format off
static SeqStrings gAnnotationTextIcons = "Comment\0Help\0Insert\0Key\0NewParagraph\0Note\0Paragraph\0";
// clang-format on

SeqStrings AnnotationTextIcons() {
    return gAnnotationTextIcons;
}

// clang-format off
// indexed by AnnotationType; unlike AnnotationReadableNameTemp() these are not
// translated, so they can appear in the annotation filter's ":t=" syntax
static SeqStrings gAnnotationTypeNames =
    "Text\0Link\0FreeText\0Line\0Square\0Circle\0Polygon\0PolyLine\0Highlight\0Underline\0Squiggly\0"
    "StrikeOut\0Redact\0Stamp\0Caret\0Ink\0Popup\0FileAttachment\0Sound\0Movie\0RichMedia\0Widget\0"
    "Screen\0PrinterMark\0TrapNet\0Watermark\0ThreeD\0Projection\0";
// clang-format on

SeqStrings AnnotationTypeNames() {
    return gAnnotationTypeNames;
}

// Matches case- and space-insensitively, so "free text" and "freetext" both
// name AnnotationType::FreeText. Unknown if there is no such type.
AnnotationType AnnotationTypeFromName(Str name) {
    TempStr want = str::DupTemp(name);
    want.len -= str::RemoveCharsInPlace(want, StrL(" -_"));
    if (len(want) == 0) {
        return AnnotationType::Unknown;
    }
    int idx = 0;
    SeqStrings names = gAnnotationTypeNames;
    for (Str item = SeqStrFirst(names); len(item) > 0; item = SeqStrNext(item), idx++) {
        if (str::EqI(item, want)) {
            return (AnnotationType)idx;
        }
    }
    return AnnotationType::Unknown;
}

// Translate an English annotation type label for the UI.
// When menuKey is set (e.g. "&Highlight"), use that key — those strings are
// already translated for context menus. Otherwise use english (already in
// translations.txt for Circle/Line/…). TrN at the call site marks bare names
// for future extraction. Strip '&' access-key markers for non-menu display.
static Str TranslateAnnotTypeNameTemp(Str english, Str menuKey = {}) {
    Str key = menuKey ? menuKey : english;
    Str tr = trans::GetTranslation(key);
    if (str::Contains(tr, StrL("&"))) {
        // StrL(""), not Str{}: ReplaceTemp rejects a null replacement and
        // returns an empty string, which blanked every name with an access key
        return str::ReplaceTemp(tr, StrL("&"), StrL(""));
    }
    return tr;
}

struct AnnotTypeLabel {
    Str name;
    Str menuKey;
};

static const AnnotTypeLabel gAnnotTypeLabels[] = {
    {TrN("Text"), StrL("&Text")},
    {TrN("Link")},
    {TrN("Free Text"), StrL("&Free Text")},
    {TrN("Line")},
    {TrN("Square")},
    {TrN("Circle")},
    {TrN("Polygon")},
    {TrN("Polyline")},
    {TrN("Highlight"), StrL("&Highlight")},
    {TrN("Underline"), StrL("&Underline")},
    {TrN("Squiggly"), StrL("S&quiggly")},
    {TrN("Strike Out"), StrL("&Strike Out")},
    {TrN("Redact")},
    {TrN("Stamp"), StrL("&Stamp")},
    {TrN("Caret"), StrL("&Caret")},
    {TrN("Ink")},
    {TrN("Popup")},
    {TrN("File Attachment")},
    {TrN("Sound")},
    {TrN("Movie")},
    {TrN("RichMedia")},
    {TrN("Widget")},
    {TrN("Screen")},
    {TrN("Printer Mark")},
    {TrN("Trap Net")},
    {TrN("Watermark")},
    {TrN("3D")},
    {TrN("Projection")},
};
static_assert(dimof(gAnnotTypeLabels) == (int)AnnotationType::Last + 1);

Str AnnotationReadableNameTemp(AnnotationType tp) {
    int idx = (int)tp;
    if (idx < 0 || idx >= dimof(gAnnotTypeLabels)) {
        return TranslateAnnotTypeNameTemp(TrN("Unknown"));
    }

    const AnnotTypeLabel& label = gAnnotTypeLabels[idx];
    return TranslateAnnotTypeNameTemp(label.name, label.menuKey);
}

// annot is still owned by EngineMupdf (markup or form widget list).
static bool IsAnnotationInEngine(EngineMupdf* e, Annotation* annot) {
    if (!e || !annot) {
        return false;
    }
    int pageNo = annot->pageNo;
    ScopedRecursiveMutex scope(&e->pagesLock);
    FzPageInfo* pageInfo = e->PageInfoByPageNo(pageNo);
    if (!pageInfo) {
        return false;
    }
    return VecContains(pageInfo->annotations, annot) || VecContains(pageInfo->widgets, annot);
}

// Safe to call MuPDF with annot->pdfannot.
// True if annot is non-null, has a live pdf_annot*, and is still listed in
// its EngineMupdf page (markup or form widget). Use before any MuPDF call.
bool AnnotationIsLive(Annotation* annot) {
    if (!annot || !annot->engine || !annot->pdfannot) {
        return false;
    }
    return IsAnnotationInEngine(annot->engine, annot);
}

struct AnnotAccess {
    fz_context* ctx;
    pdf_annot* annot;
    ScopedRecursiveMutex lock;

    explicit AnnotAccess(Annotation* a) : ctx(a->engine->Ctx()), annot(a->pdfannot), lock(&a->engine->docLock) {}
};

// Centralize the lock and MuPDF error boundary shared by annotation getters.
template <typename T, typename Read>
static T ReadAnnot(Annotation* annot, T fallback, Read read) {
    if (!AnnotationIsLive(annot)) {
        return fallback;
    }
    AnnotAccess access(annot);
    fz_try(access.ctx) {
        fallback = read(access.ctx, access.annot);
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
    }
    return fallback;
}

template <typename Update>
static bool UpdateAnnotIf(Annotation* annot, Update update) {
    if (!AnnotationIsLive(annot)) {
        return false;
    }
    EngineMupdf* engine = annot->engine;
    bool changed = false;
    {
        AnnotAccess access(annot);
        fz_try(access.ctx) {
            changed = update(access.ctx, access.annot);
            if (changed) {
                pdf_update_annot(access.ctx, access.annot);
            }
        }
        fz_catch(access.ctx) {
            fz_report_error(access.ctx);
            changed = false;
        }
    }
    if (changed) {
        MarkNotificationAsModified(engine, annot);
    }
    return changed;
}

template <typename Update>
static bool UpdateAnnot(Annotation* annot, Update update) {
    return UpdateAnnotIf(annot, [update](fz_context* ctx, pdf_annot* a) {
        update(ctx, a);
        return true;
    });
}

AnnotationType Type(Annotation* annot) {
    if (!annot) {
        return AnnotationType::Unknown;
    }
    ReportIf((int)annot->type < 0);
    return annot->type;
}

int PageNo(Annotation* annot) {
    if (!annot) {
        return -1;
    }
    ReportIf(annot->pageNo < 1);
    return annot->pageNo;
}

RectF GetBounds(Annotation* annot) {
    if (!AnnotationIsLive(annot)) {
        return annot ? annot->bounds : RectF{};
    }
    AnnotAccess access(annot);
    RectF rc;

    fz_try(access.ctx) {
        rc = PdfAnnotBounds(access.ctx, access.annot);
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
        logf("GetBounds(): pdf_bound_annot() failed\n");
    }
    annot->bounds = rc;
    return annot->bounds;
}

RectF GetRect(Annotation* annot) {
    if (!annot) {
        return {};
    }
    return annot->bounds;
}

void SetRect(Annotation* annot, RectF r) {
    // Stale reference after delete/reload must not reach mupdf
    // (e.g. annotationBeingDragged).
    if (!AnnotationIsLive(annot)) {
        return;
    }
    EngineMupdf* e = annot->engine;
    auto* a = annot->pdfannot;
    bool failed = false;
    // Vec lives outside fz_try: a longjmp would skip C++ destructors.
    Vec<fz_point> pts;
    Vec<int> strokeCounts;
    Vec<fz_quad> redactQuads;
    {
        auto* ctx = e->Ctx();
        ScopedRecursiveMutex cs(&e->docLock);
        float dx = r.x - annot->bounds.x;
        float dy = r.y - annot->bounds.y;
        fz_try(ctx) {
            if (annot->type == AnnotationType::Line) {
                // /L is the two endpoints, not a rect. Translate them so a
                // drag does not rewrite a top-right/bottom-left line as
                // top-left/bottom-right of the bounds.
                fz_point p1{};
                fz_point p2{};
                pdf_annot_line(ctx, a, &p1, &p2);
                p1.x += dx;
                p1.y += dy;
                p2.x += dx;
                p2.y += dy;
                pdf_set_annot_line(ctx, a, p1, p2);
            } else if (annot->type == AnnotationType::Polygon || annot->type == AnnotationType::PolyLine) {
                // /Rect is derived from Vertices; pdf_set_annot_rect rejects these.
                int n = pdf_annot_vertex_count(ctx, a);
                for (int i = 0; i < n; i++) {
                    fz_point p = pdf_annot_vertex(ctx, a, i);
                    p.x += dx;
                    p.y += dy;
                    VecAppend(pts, p);
                }
                if (n > 0) {
                    pdf_set_annot_vertices(ctx, a, n, pts.els);
                }
            } else if (annot->type == AnnotationType::Redact) {
                // Text-selection marks store coverage in QuadPoints. Translate
                // those on a move; a new rect (no quads) is an area mark.
                int n = pdf_annot_quad_point_count(ctx, a);
                if (n > 0) {
                    for (int i = 0; i < n; i++) {
                        fz_quad q = pdf_annot_quad_point(ctx, a, i);
                        q.ul.x += dx;
                        q.ul.y += dy;
                        q.ur.x += dx;
                        q.ur.y += dy;
                        q.ll.x += dx;
                        q.ll.y += dy;
                        q.lr.x += dx;
                        q.lr.y += dy;
                        VecAppend(redactQuads, q);
                    }
                    pdf_set_annot_quad_points(ctx, a, len(redactQuads), redactQuads.els);
                } else {
                    pdf_set_annot_rect(ctx, a, ToFzRect(r));
                }
            } else if (annot->type == AnnotationType::Ink) {
                // /Rect is derived from InkList; pdf_set_annot_rect rejects Ink.
                int nStrokes = pdf_annot_ink_list_count(ctx, a);
                for (int i = 0; i < nStrokes; i++) {
                    int nv = pdf_annot_ink_list_stroke_count(ctx, a, i);
                    VecAppend(strokeCounts, nv);
                    for (int k = 0; k < nv; k++) {
                        fz_point p = pdf_annot_ink_list_stroke_vertex(ctx, a, i, k);
                        p.x += dx;
                        p.y += dy;
                        VecAppend(pts, p);
                    }
                }
                if (nStrokes > 0) {
                    pdf_set_annot_ink_list(ctx, a, nStrokes, strokeCounts.els, pts.els);
                }
            } else if (r.dx == annot->bounds.dx && r.dy == annot->bounds.dy) {
                // a move: shift /Rect itself. The bounds can be bigger than
                // /Rect (a border), so setting them as /Rect grew the
                // annotation on every move
                fz_rect rect = pdf_annot_rect(ctx, a);
                rect.x0 += dx;
                rect.x1 += dx;
                rect.y0 += dy;
                rect.y1 += dy;
                pdf_set_annot_rect(ctx, a, rect);
            } else {
                pdf_set_annot_rect(ctx, a, ToFzRect(r));
            }
            pdf_update_annot(ctx, a);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            // can happen for non-moveable annotations
            failed = true;
            logf("SetRect(): pdf_set_annot_rect() or pdf_update_annot() failed\n");
        }
    }
    ReportIf(failed);
    if (failed) {
        return;
    }
    // pdf_update_annot can rewrite the rect (rubber stamps keep a 190x50
    // aspect). Cached bounds must match that, or resize handles and hit
    // testing cover empty space around the visible stamp (issue #5933).
    annot->bounds = GetBounds(annot);
    // must be called outside docLock to avoid deadlock with pagesLock
    MarkNotificationAsModified(e, annot);
}

static Str MupdfCStrDupTemp(const char* s) {
    if (!s) {
        return {};
    }
    return str::DupTemp(Str(s));
}

static Str MupdfCStrTemp(const char* s) {
    if (str::IsEmptyOrWhiteSpace(Str(s))) {
        return {};
    }
    return str::DupTemp(Str(s));
}

// AnnotEditToolbar.cpp
Str Author(Annotation* annot) {
    return ReadAnnot(annot, Str{},
                     [](fz_context* ctx, pdf_annot* a) { return MupdfCStrTemp(pdf_annot_author(ctx, a)); });
}

SeqStrings gQuaddingNames = "Left\0Center\0Right\0";

// name of a text alignment as used by the FreeTextAlignment setting and the
// `alignment` command argument -> PDF /Q value, -1 if not a known name
int QuaddingFromName(Str s) {
    return SeqStrIndexIS(gQuaddingNames, s);
}

int Quadding(Annotation* annot) {
    return ReadAnnot(annot, 0, pdf_annot_quadding);
}

static bool IsValidQuadding(int i) {
    return i >= 0 && i <= 2;
}

static bool IsCustomFreeTextFont(Str family, int style);
static void ReadFreeTextFontLocked(fz_context* ctx, pdf_annot* a, Str& family, int& style);
static void WriteFreeTextFontLocked(fz_context* ctx, pdf_annot* a, Str family, int style);

// return true if changed
bool SetQuadding(Annotation* annot, int newQuadding) {
    ReportIf(!IsValidQuadding(newQuadding));
    if (!AnnotationIsLive(annot) || Quadding(annot) == newQuadding) {
        return false;
    }
    return UpdateAnnot(annot, [annot, newQuadding](fz_context* ctx, pdf_annot* a) {
        pdf_set_annot_quadding(ctx, a, newQuadding);
        // /DS has its own text-align, which wins over /Q.
        if (Type(annot) == AnnotationType::FreeText) {
            Str family;
            int style = 0;
            ReadFreeTextFontLocked(ctx, a, family, style);
            if (IsCustomFreeTextFont(family, style)) {
                WriteFreeTextFontLocked(ctx, a, family, style);
            }
        }
    });
}

void SetQuadPointsAsRect(Annotation* annot, const Vec<RectF>& rects) {
    if (!AnnotationIsLive(annot)) {
        return;
    }
    int n = len(rects);
    if (n == 0) {
        return;
    }
    fz_quad* quads = AllocArray<fz_quad>(n);
    if (!quads) {
        return;
    }
    AutoFree<fz_quad> freeQuads(quads);
    for (int i = 0; i < n; i++) {
        quads[i] = fz_quad_from_rect(ToFzRect(rects[i]));
    }
    UpdateAnnot(annot, [n, quads](fz_context* ctx, pdf_annot* a) {
        pdf_clear_annot_quad_points(ctx, a);
        pdf_set_annot_quad_points(ctx, a, n, quads);
    });
}

Vec<RectF> GetQuadPointsAsRect(Annotation* annot) {
    Vec<RectF> res;
    if (!AnnotationIsLive(annot)) {
        return res;
    }
    AnnotAccess access(annot);
    fz_try(access.ctx) {
        int n = pdf_annot_quad_point_count(access.ctx, access.annot);
        for (int i = 0; i < n; i++) {
            fz_quad q = pdf_annot_quad_point(access.ctx, access.annot, i);
            VecAppend(res, ToRectF(fz_rect_from_quad(q)));
        }
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
    }
    return res;
}

// Regenerate appearance streams for the whole page after a form mutation.
// A field change can affect *other* widgets (radio-group siblings sharing the
// field value, or JS-calculated fields); each caches its resolved appearance
// until pdf_update_annot runs for it, so updating only the touched widget
// leaves siblings showing a stale appearance. Call inside the caller's fz_try,
// holding docLock.
static void UpdateFormFieldPage(fz_context* ctx, pdf_annot* a) {
    pdf_page* page = pdf_annot_page(ctx, a);
    if (page) {
        pdf_update_page(ctx, page);
    }
}

// PDF form (widget) fields. GetWidgetType returns a pdf_widget_type value
// (PDF_WIDGET_TYPE_*), or 0 (UNKNOWN) when annot isn't a form widget.
int GetWidgetType(Annotation* annot) {
    if (!annot || annot->type != AnnotationType::Widget) {
        return PDF_WIDGET_TYPE_UNKNOWN;
    }
    return ReadAnnot(annot, (int)PDF_WIDGET_TYPE_UNKNOWN,
                     [](fz_context* ctx, pdf_annot* a) { return (int)pdf_widget_type(ctx, a); });
}

WidgetCursorKind GetWidgetCursorKind(Annotation* annot) {
    if (!annot || annot->type != AnnotationType::Widget) {
        return WidgetCursorKind::None;
    }
    return ReadAnnot(annot, WidgetCursorKind::None, [](fz_context* ctx, pdf_annot* a) {
        if (pdf_annot_field_flags(ctx, a) & PDF_FIELD_IS_READ_ONLY) {
            return WidgetCursorKind::None;
        }
        int wt = pdf_widget_type(ctx, a);
        if (wt == PDF_WIDGET_TYPE_TEXT || wt == PDF_WIDGET_TYPE_COMBOBOX || wt == PDF_WIDGET_TYPE_LISTBOX) {
            return WidgetCursorKind::Text;
        }
        if (wt == PDF_WIDGET_TYPE_CHECKBOX || wt == PDF_WIDGET_TYPE_RADIOBUTTON) {
            return WidgetCursorKind::Button;
        }
        return WidgetCursorKind::None;
    });
}

// Toggle a checkbox / radio-button form field in place. Returns true if it was
// a (non-read-only) checkbox/radio and got toggled.
bool ToggleFormButton(Annotation* annot) {
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Widget) {
        return false;
    }
    EngineMupdf* e = annot->engine;
    auto* a = annot->pdfannot;
    bool changed = false;
    {
        // BaseCtx(), not a Ctx() clone: toggling regenerates the appearance,
        // which runs the button's format/calculate JS; mupdf executes (and
        // rethrows errors) on _ctx, so the fz_try must be on that context.
        auto* ctx = e->BaseCtx();
        ScopedRecursiveMutex cs(&e->docLock);
        fz_try(ctx) {
            int wt = pdf_widget_type(ctx, a);
            int flags = pdf_annot_field_flags(ctx, a);
            bool readOnly = (flags & PDF_FIELD_IS_READ_ONLY) != 0;
            if (wt == PDF_WIDGET_TYPE_RADIOBUTTON && !readOnly) {
                // pdf_toggle_widget mishandles radio groups whose buttons have
                // distinct on-state names: it sets every sibling's /AS to the
                // selected state, leaving them all "on". Instead set the group's
                // value via pdf_set_field_value, which routes through
                // update_checkbox_selector and sets each kid's /AS correctly.
                pdf_obj* kid = pdf_annot_obj(ctx, a);
                pdf_obj* grp = kid;
                for (pdf_obj* p = pdf_dict_get(ctx, grp, PDF_NAME(Parent)); p;
                     p = pdf_dict_get(ctx, grp, PDF_NAME(Parent))) {
                    grp = p;
                }
                pdf_obj* curAS = pdf_dict_get(ctx, kid, PDF_NAME(AS));
                bool isOn = curAS && !pdf_name_eq(ctx, curAS, PDF_NAME(Off));
                bool noToggleOff = (flags & PDF_BTN_FIELD_IS_NO_TOGGLE_TO_OFF) != 0;
                Str onName = Str(pdf_to_name(ctx, pdf_button_field_on_state(ctx, kid)));
                char* val = CStrTemp((isOn && !noToggleOff) ? StrL("Off") : onName);
                // unlike pdf_toggle_widget, pdf_set_field_value doesn't open a
                // journal operation itself, and the journalled doc throws on
                // a write outside one
                pdf_begin_operation(ctx, e->pdfdoc, "Toggle radio button");
                fz_try(ctx) {
                    pdf_set_field_value(ctx, e->pdfdoc, grp, val, 0);
                    pdf_end_operation(ctx, e->pdfdoc);
                }
                fz_catch(ctx) {
                    pdf_abandon_operation(ctx, e->pdfdoc);
                    fz_rethrow(ctx);
                }
                pdf_update_annot(ctx, a);
                UpdateFormFieldPage(ctx, a); // refresh all radio-group siblings
                changed = true;
            } else if (wt == PDF_WIDGET_TYPE_CHECKBOX && !readOnly) {
                pdf_toggle_widget(ctx, a);
                pdf_update_annot(ctx, a);
                UpdateFormFieldPage(ctx, a);
                changed = true;
            }
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            logf("ToggleFormButton(): mupdf calls failed\n");
        }
    }
    if (changed) {
        // must be called outside docLock (it takes pagesLock then docLock/renderLock)
        MarkNotificationAsModified(e, annot);
    }
    return changed;
}

// pdf_annot_field_flags (PDF_FIELD_IS_*, PDF_TX_FIELD_IS_* bits), or 0.
int GetWidgetFieldFlags(Annotation* annot) {
    if (!annot || annot->type != AnnotationType::Widget) {
        return 0;
    }
    return ReadAnnot(annot, 0, pdf_annot_field_flags);
}

// current text value of a form field (owned temp copy), or "" .
Str GetWidgetValue(Annotation* annot) {
    if (!annot || annot->type != AnnotationType::Widget) {
        return {};
    }
    return ReadAnnot(annot, Str{},
                     [](fz_context* ctx, pdf_annot* a) { return MupdfCStrTemp(pdf_annot_field_value(ctx, a)); });
}

// font size from the field's /DA (in PDF points), or 0 for auto-size.
float GetWidgetFontSize(Annotation* annot) {
    if (!annot || annot->type != AnnotationType::Widget) {
        return 0;
    }
    return ReadAnnot(annot, 0.f, [](fz_context* ctx, pdf_annot* a) {
        const char* fontZ = nullptr;
        float size = 0;
        int nColor = 0;
        float color[4] = {0};
        pdf_annot_default_appearance(ctx, a, &fontZ, &size, &nColor, color);
        return size;
    });
}

// max length of a text field (chars), or 0 for unlimited.
int GetWidgetMaxLen(Annotation* annot) {
    if (!annot || annot->type != AnnotationType::Widget) {
        return 0;
    }
    return ReadAnnot(annot, 0, pdf_text_widget_max_len);
}

template <typename SetValue>
static bool SetWidgetValue(Annotation* annot, Str value, Str operation, SetValue setValue) {
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Widget) {
        return false;
    }
    EngineMupdf* e = annot->engine;
    auto* a = annot->pdfannot;
    bool ok = false;
    TempStr valueZ = str::DupTemp(value);
    {
        // BaseCtx(), not a Ctx() clone: regenerating the appearance runs the
        // field's format/calculate JS, which mupdf executes (and rethrows
        // errors) on _ctx -- the fz_try must be on that same context.
        auto* ctx = e->BaseCtx();
        ScopedRecursiveMutex cs(&e->docLock);
        fz_try(ctx) {
            ok = setValue(ctx, a, len(valueZ) == 0 ? "" : valueZ.s);
            pdf_update_annot(ctx, a);
            UpdateFormFieldPage(ctx, a); // refresh JS-calculated fields
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            logf("%s(): mupdf calls failed\n", operation);
        }
    }
    if (ok) {
        MarkNotificationAsModified(e, annot);
    }
    return ok;
}

// set a text field's value (runs validation); returns true if accepted.
bool SetWidgetTextValue(Annotation* annot, Str value) {
    auto setValue = [](fz_context* ctx, pdf_annot* a, const char* s) {
        return pdf_set_text_field_value(ctx, a, s) != 0;
    };
    return SetWidgetValue(annot, value, StrL("SetWidgetTextValue"), setValue);
}

// options of a combobox/listbox field (display strings), appended to `out`.
void GetWidgetChoiceOptions(Annotation* annot, StrVec& out) {
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Widget) {
        return;
    }
    AnnotAccess access(annot);
    fz_try(access.ctx) {
        int n = pdf_choice_widget_options(access.ctx, access.annot, 0, nullptr);
        if (n > 0) {
            const char** opts = (const char**)fz_malloc(access.ctx, n * sizeof(char*));
            pdf_choice_widget_options(access.ctx, access.annot, 0, opts);
            for (int i = 0; i < n; i++) {
                out.Append(Str(opts[i] ? opts[i] : ""));
            }
            fz_free(access.ctx, (void*)opts);
        }
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
    }
}

// set a choice field's value to one of its options; returns true if applied.
bool SetWidgetChoiceValue(Annotation* annot, Str value) {
    auto setValue = [](fz_context* ctx, pdf_annot* a, const char* s) {
        pdf_set_choice_field_value(ctx, a, s);
        return true;
    };
    return SetWidgetValue(annot, value, StrL("SetWidgetChoiceValue"), setValue);
}

static pdf_obj* WidgetField(fz_context* ctx, pdf_annot* a) {
    pdf_obj* obj = pdf_annot_obj(ctx, a);
    pdf_obj* parent = pdf_dict_get(ctx, obj, PDF_NAME(Parent));
    return parent && !pdf_dict_get(ctx, obj, PDF_NAME(T)) ? parent : obj;
}

bool GetFormFieldProperties(Annotation* annot, FormFieldProperties& props) {
    if (!AnnotationIsLive(annot) || Type(annot) != AnnotationType::Widget) {
        return false;
    }
    AnnotAccess access(annot);
    bool ok = false;
    fz_var(ok);
    str::Builder options;
    fz_try(access.ctx) {
        auto* ctx = access.ctx;
        pdf_annot* a = access.annot;
        switch (pdf_widget_type(ctx, a)) {
            case PDF_WIDGET_TYPE_TEXT:
                props.kind = FormFieldKind::Text;
                break;
            case PDF_WIDGET_TYPE_CHECKBOX:
                props.kind = FormFieldKind::Checkbox;
                break;
            case PDF_WIDGET_TYPE_RADIOBUTTON:
                props.kind = FormFieldKind::Radio;
                break;
            case PDF_WIDGET_TYPE_COMBOBOX:
                props.kind = FormFieldKind::Combo;
                break;
            case PDF_WIDGET_TYPE_LISTBOX:
                props.kind = FormFieldKind::ListBox;
                break;
            case PDF_WIDGET_TYPE_BUTTON:
                props.kind = FormFieldKind::PushButton;
                break;
            case PDF_WIDGET_TYPE_SIGNATURE:
                if (pdf_widget_is_signed(ctx, a)) fz_throw(ctx, FZ_ERROR_ARGUMENT, "signed field");
                props.kind = FormFieldKind::Signature;
                break;
            default:
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "unsupported field");
        }
        pdf_obj* field = WidgetField(ctx, a);
        // Editing a nested field name needs a hierarchy editor.
        if (pdf_dict_get(ctx, field, PDF_NAME(Parent))) fz_throw(ctx, FZ_ERROR_ARGUMENT, "nested field");
        if (pdf_dict_gets(ctx, field, "SwiftLeafImage")) props.kind = FormFieldKind::Image;
        props.format = MupdfCStrDupTemp(pdf_to_text_string(ctx, pdf_dict_gets(ctx, field, "SwiftLeafFormat")));
        if (props.kind == FormFieldKind::Text && str::StartsWith(props.format, StrL("date:")))
            props.kind = FormFieldKind::Date;
        props.tooltip = MupdfCStrDupTemp(pdf_dict_get_text_string(ctx, field, PDF_NAME(TU)));
        props.maxLength = pdf_dict_get_int(ctx, field, PDF_NAME(MaxLen));
        props.multiline = (pdf_field_flags(ctx, field) & PDF_TX_FIELD_IS_MULTILINE) != 0;
        props.comb = (pdf_field_flags(ctx, field) & PDF_TX_FIELD_IS_COMB) != 0;
        props.borderWidth = pdf_annot_border_width(ctx, a);
        pdf_obj* action = pdf_dict_get(ctx, field, PDF_NAME(A));
        Str actionType = Str(pdf_dict_get_name(ctx, action, PDF_NAME(S)));
        props.action = str::Eq(actionType, StrL("ResetForm"))    ? StrL("reset")
                       : str::Eq(actionType, StrL("SubmitForm")) ? StrL("submit")
                       : str::Eq(actionType, StrL("URI"))        ? StrL("link")
                                                                 : Str{};
        props.target = MupdfCStrDupTemp(
            pdf_dict_get_text_string(ctx, action, str::Eq(props.action, StrL("link")) ? PDF_NAME(URI) : PDF_NAME(F)));
        props.name = MupdfCStrDupTemp(pdf_dict_get_text_string(ctx, field, PDF_NAME(T)));
        pdf_obj* def = pdf_dict_get(ctx, field, PDF_NAME(DV));
        props.defaultValue =
            MupdfCStrDupTemp(pdf_is_name(ctx, def) ? pdf_to_name(ctx, def) : pdf_to_text_string(ctx, def));
        int flags = pdf_field_flags(ctx, field);
        props.required = (flags & PDF_FIELD_IS_REQUIRED) != 0;
        props.readOnly = (flags & PDF_FIELD_IS_READ_ONLY) != 0;
        const char* font = nullptr;
        int n = 0;
        float color[4]{};
        pdf_annot_default_appearance(ctx, a, &font, &props.fontSize, &n, color);
        props.font = MupdfCStrDupTemp(font);
        for (int i = 0; i < 3; i++) props.textColor[i] = n == 1 ? color[0] : n == 3 ? color[i] : 0;
        pdf_obj* mk = pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(MK));
        for (int i = 0; i < 3; i++) {
            pdf_obj* bc = pdf_dict_get(ctx, mk, PDF_NAME(BC));
            pdf_obj* bg = pdf_dict_get(ctx, mk, PDF_NAME(BG));
            props.borderColor[i] = pdf_to_real(ctx, pdf_array_get(ctx, bc, pdf_array_len(ctx, bc) == 1 ? 0 : i));
            props.backgroundColor[i] =
                bg ? pdf_to_real(ctx, pdf_array_get(ctx, bg, pdf_array_len(ctx, bg) == 1 ? 0 : i)) : 1;
        }
        pdf_obj* kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        int count =
            props.kind == FormFieldKind::Radio ? pdf_array_len(ctx, kids) : pdf_choice_field_option_count(ctx, field);
        for (int i = 0; i < count; i++) {
            if (i) options.Append(StrL("\r\n"));
            const char* s = props.kind == FormFieldKind::Radio
                                ? pdf_to_name(ctx, pdf_button_field_on_state(ctx, pdf_array_get(ctx, kids, i)))
                                : pdf_choice_field_option(ctx, field, 0, i);
            options.Append(Str(s));
        }
        props.options = ToStrTemp(options);
        ok = true;
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
    }
    return ok;
}

static bool ValidateFieldProps(const FormFieldProperties& p, StrVec& options, Str& error) {
    if (str::IsEmptyOrWhiteSpace(p.name) || str::Contains(p.name, StrL("."))) {
        error = Tr("Enter a field name without dots.");
        return false;
    }
    if (!isfinite(p.fontSize) || p.fontSize < 0 || p.fontSize > 300) {
        error = Tr("Font size must be between 0 (automatic) and 300.");
        return false;
    }
    if (p.maxLength < 0 || p.maxLength > 1000000 || !isfinite(p.borderWidth) || p.borderWidth < 0 ||
        p.borderWidth > 20 || (p.comb && (p.maxLength == 0 || p.multiline))) {
        error = Tr("Check maximum length, comb, multiline and border width.");
        return false;
    }
    if (!str::Eq(p.font, StrL("Helv")) && !str::Eq(p.font, StrL("Cour")) && !str::Eq(p.font, StrL("TiRo"))) {
        error = Tr("Font must be Helv, Cour or TiRo.");
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(p.textColor[i]) || p.textColor[i] < 0 || p.textColor[i] > 1 || !isfinite(p.borderColor[i]) ||
            p.borderColor[i] < 0 || p.borderColor[i] > 1 || !isfinite(p.backgroundColor[i]) ||
            p.backgroundColor[i] < 0 || p.backgroundColor[i] > 1) {
            error = Tr("Colors must contain three numbers between 0 and 1.");
            return false;
        }
    }
    if (p.kind == FormFieldKind::PushButton && !str::Eq(p.action, StrL("reset")) &&
        !str::Eq(p.action, StrL("submit")) && !str::Eq(p.action, StrL("link"))) {
        error = Tr("Button action must be reset, submit or link.");
        return false;
    }
    if (p.kind == FormFieldKind::PushButton && !str::Eq(p.action, StrL("reset")) &&
        !str::StartsWith(p.target, StrL("https://")) && !str::StartsWith(p.target, StrL("http://"))) {
        error = Tr("Enter an HTTP or HTTPS URL.");
        return false;
    }
    if (p.kind == FormFieldKind::Signature && len(p.defaultValue)) {
        error = Tr("An unsigned signature field cannot have a default value.");
        return false;
    }
    if (p.kind == FormFieldKind::Checkbox && len(p.defaultValue) && !str::Eq(p.defaultValue, StrL("Yes")) &&
        !str::Eq(p.defaultValue, StrL("Off"))) {
        error = Tr("Use Yes or Off as the checkbox default.");
        return false;
    }
    if (p.kind != FormFieldKind::Combo && p.kind != FormFieldKind::ListBox && p.kind != FormFieldKind::Radio)
        return true;
    StrVec lines;
    Split(&lines, p.options, StrL("\n"), true);
    for (Str line : lines) {
        str::TrimWs(line);
        if (!len(line)) continue;
        if (options.Contains(line) || (p.kind == FormFieldKind::Radio && str::Eq(line, StrL("Off")))) {
            error = Tr("Options must be unique; Off is reserved for radio buttons.");
            return false;
        }
        options.Append(line);
    }
    int min = p.kind == FormFieldKind::Radio ? 2 : 1;
    if (len(options) < min || len(options) > 64) {
        error = Tr("Enter 1 to 64 options, or 2 to 64 for a radio group, one per line.");
        return false;
    }
    if (len(p.defaultValue) && !options.Contains(p.defaultValue) &&
        !(p.kind == FormFieldKind::Radio && str::Eq(p.defaultValue, StrL("Off")))) {
        error = Tr("The default value must be one of the options.");
        return false;
    }
    return true;
}

static void EnsureFieldFont(fz_context* ctx, pdf_document* doc, const FormFieldProperties& p) {
    pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
    pdf_obj* acro = pdf_dict_get(ctx, root, PDF_NAME(AcroForm));
    if (!acro) acro = pdf_dict_put_dict(ctx, root, PDF_NAME(AcroForm), 3);
    pdf_obj* dr = pdf_dict_get(ctx, acro, PDF_NAME(DR));
    if (!dr) dr = pdf_dict_put_dict(ctx, acro, PDF_NAME(DR), 1);
    pdf_obj* fonts = pdf_dict_get(ctx, dr, PDF_NAME(Font));
    if (!fonts) fonts = pdf_dict_put_dict(ctx, dr, PDF_NAME(Font), 1);
    if (pdf_dict_gets(ctx, fonts, CStrTemp(p.font))) return;
    pdf_obj* font = pdf_add_new_dict(ctx, doc, 3);
    pdf_dict_puts_drop(ctx, fonts, CStrTemp(p.font), font);
    pdf_dict_put(ctx, font, PDF_NAME(Type), PDF_NAME(Font));
    pdf_dict_put(ctx, font, PDF_NAME(Subtype), PDF_NAME(Type1));
    pdf_dict_put_name(ctx, font, PDF_NAME(BaseFont),
                      str::Eq(p.font, StrL("Cour"))   ? "Courier"
                      : str::Eq(p.font, StrL("TiRo")) ? "Times-Roman"
                                                      : "Helvetica");
}

static void WriteFieldProps(fz_context* ctx, pdf_document* doc, pdf_obj* field, const FormFieldProperties& p,
                            const StrVec& options) {
    EnsureFieldFont(ctx, doc, p);
    pdf_dict_put_text_string(ctx, field, PDF_NAME(T), CStrTemp(p.name));
    int flags = pdf_field_flags(ctx, field) & ~(PDF_FIELD_IS_READ_ONLY | PDF_FIELD_IS_REQUIRED);
    flags |= p.readOnly ? PDF_FIELD_IS_READ_ONLY : 0;
    flags |= p.required ? PDF_FIELD_IS_REQUIRED : 0;
    bool text = p.kind == FormFieldKind::Text || p.kind == FormFieldKind::Date;
    if (text) {
        flags &= ~(PDF_TX_FIELD_IS_MULTILINE | PDF_TX_FIELD_IS_COMB);
        flags |= p.multiline ? PDF_TX_FIELD_IS_MULTILINE : 0;
        flags |= p.comb ? PDF_TX_FIELD_IS_COMB : 0;
        if (p.maxLength)
            pdf_dict_put_int(ctx, field, PDF_NAME(MaxLen), p.maxLength);
        else
            pdf_dict_del(ctx, field, PDF_NAME(MaxLen));
    }
    pdf_dict_put_text_string(ctx, field, PDF_NAME(TU), CStrTemp(p.tooltip));
    pdf_dict_puts_drop(ctx, field, "SwiftLeafFormat", pdf_new_text_string(ctx, CStrTemp(p.format)));
    if (p.kind == FormFieldKind::PushButton || p.kind == FormFieldKind::Image) {
        if (p.kind == FormFieldKind::Image)
            pdf_dict_puts(ctx, field, "SwiftLeafImage", PDF_TRUE);
        else {
            pdf_obj* action = pdf_dict_put_dict(ctx, field, PDF_NAME(A), 3);
            pdf_dict_put_name(ctx, action, PDF_NAME(S),
                              str::Eq(p.action, StrL("reset"))    ? "ResetForm"
                              : str::Eq(p.action, StrL("submit")) ? "SubmitForm"
                                                                  : "URI");
            if (!str::Eq(p.action, StrL("reset")))
                pdf_dict_put_text_string(ctx, action, str::Eq(p.action, StrL("link")) ? PDF_NAME(URI) : PDF_NAME(F),
                                         CStrTemp(p.target));
        }
    }
    pdf_dict_put_int(ctx, field, PDF_NAME(Ff), flags);
    Str value = p.defaultValue;
    bool button = p.kind == FormFieldKind::Checkbox || p.kind == FormFieldKind::Radio;
    if (button && !len(value)) value = StrL("Off");
    if (p.kind == FormFieldKind::Signature) {
        pdf_dict_del(ctx, field, PDF_NAME(DV));
        return;
    }
    if (p.kind == FormFieldKind::Combo || p.kind == FormFieldKind::ListBox) {
        pdf_obj* opt = pdf_dict_put_array(ctx, field, PDF_NAME(Opt), len(options));
        for (Str s : options) pdf_array_push_text_string(ctx, opt, CStrTemp(s));
        pdf_dict_del(ctx, field, PDF_NAME(I));
    }
    const char* valueZ = len(value) ? CStrTemp(value) : "";
    if (button)
        pdf_dict_put_name(ctx, field, PDF_NAME(DV), valueZ);
    else
        pdf_dict_put_text_string(ctx, field, PDF_NAME(DV), valueZ);
    if (!pdf_set_field_value(ctx, doc, field, valueZ, 1)) {
        fz_throw(ctx, FZ_ERROR_ARGUMENT, "field value rejected");
    }
}

static void StyleFieldWidget(fz_context* ctx, pdf_annot* a, const FormFieldProperties& p) {
    pdf_set_annot_default_appearance(ctx, a, CStrTemp(p.font), p.fontSize, 3, p.textColor);
    pdf_set_annot_border_width(ctx, a, p.borderWidth);
    pdf_obj* obj = pdf_annot_obj(ctx, a);
    pdf_obj* mk = pdf_dict_get(ctx, obj, PDF_NAME(MK));
    if (!mk) mk = pdf_dict_put_dict(ctx, obj, PDF_NAME(MK), 3);
    pdf_obj* bc = pdf_dict_put_array(ctx, mk, PDF_NAME(BC), 3);
    pdf_obj* bg = pdf_dict_put_array(ctx, mk, PDF_NAME(BG), 3);
    for (int i = 0; i < 3; i++) {
        pdf_array_push_real(ctx, bc, p.borderColor[i]);
        pdf_array_push_real(ctx, bg, p.backgroundColor[i]);
    }
    if (p.kind == FormFieldKind::PushButton) pdf_dict_put_text_string(ctx, mk, PDF_NAME(CA), CStrTemp(p.defaultValue));
    if (p.kind != FormFieldKind::Image || !pdf_dict_get(ctx, obj, PDF_NAME(AP))) {
        pdf_dirty_annot(ctx, a);
        pdf_update_annot(ctx, a);
    }
    if (p.kind == FormFieldKind::Image && len(p.target)) {
        fz_image* image = nullptr;
        pdf_obj* resources = nullptr;
        fz_buffer* contents = nullptr;
        fz_var(image);
        fz_var(resources);
        fz_var(contents);
        fz_try(ctx) {
            image = fz_new_image_from_file(ctx, CStrTemp(p.target));
            pdf_document* doc = pdf_annot_page(ctx, a)->doc;
            resources = pdf_new_dict(ctx, doc, 1);
            pdf_obj* images = pdf_dict_put_dict(ctx, resources, PDF_NAME(XObject), 1);
            pdf_dict_puts_drop(ctx, images, "Image", pdf_add_image(ctx, doc, image));
            fz_rect rect = pdf_annot_rect(ctx, a);
            float w = rect.x1 - rect.x0, h = rect.y1 - rect.y0;
            float scale = fz_min(w / image->w, h / image->h);
            Str stream = fmt("q %g 0 0 %g %g %g cm /Image Do Q", image->w * scale, image->h * scale,
                             (w - image->w * scale) / 2, (h - image->h * scale) / 2);
            contents = fz_new_buffer_from_copied_data(ctx, (const u8*)stream.s, len(stream));
            pdf_set_annot_appearance(ctx, a, "N", nullptr, fz_identity, fz_make_rect(0, 0, w, h), resources, contents);
            pdf_obj* imageMk = pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(MK));
            pdf_obj* ap = pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(AP));
            pdf_dict_put(ctx, imageMk, PDF_NAME(I), pdf_dict_get(ctx, ap, PDF_NAME(N)));
            pdf_dict_puts_drop(ctx, imageMk, "TP", pdf_new_int(ctx, 1));
        }
        fz_always(ctx) {
            fz_drop_image(ctx, image);
            pdf_drop_obj(ctx, resources);
            fz_drop_buffer(ctx, contents);
        }
        fz_catch(ctx) {
            fz_rethrow(ctx);
        }
    }
}

bool SetFormFieldProperties(Annotation* annot, const FormFieldProperties& p, Str& error) {
    FormFieldProperties old;
    StrVec options;
    if (!GetFormFieldProperties(annot, old) || old.kind != p.kind || !ValidateFieldProps(p, options, error))
        return false;
    auto* e = annot->engine;
    auto* ctx = e->BaseCtx();
    bool ok = false;
    fz_var(ok);
    {
        ScopedRecursiveMutex lock(&e->docLock);
        pdf_begin_operation(ctx, e->pdfdoc, "Edit form field");
        fz_try(ctx) {
            pdf_obj* field = WidgetField(ctx, annot->pdfannot);
            pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, e->pdfdoc), "Root/AcroForm/Fields");
            pdf_obj* duplicate = pdf_lookup_field(ctx, fields, CStrTemp(p.name));
            if (duplicate && pdf_objcmp(ctx, duplicate, field))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "field name already exists");
            pdf_obj* kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
            int n = pdf_array_len(ctx, kids);
            if (p.kind == FormFieldKind::Radio) {
                if (n != len(options)) fz_throw(ctx, FZ_ERROR_ARGUMENT, "keep the number of radio options unchanged");
                pdf_page* page = pdf_annot_page(ctx, annot->pdfannot);
                for (int i = 0; i < n; i++) {
                    pdf_obj* kid = pdf_array_get(ctx, kids, i);
                    for (pdf_annot* a = pdf_first_widget(ctx, page); a; a = pdf_next_widget(ctx, a)) {
                        if (pdf_objcmp(ctx, kid, pdf_annot_obj(ctx, a))) continue;
                        pdf_dict_put_name(ctx, kid, PDF_NAME(AS), CStrTemp(options[i]));
                        StyleFieldWidget(ctx, a, p);
                    }
                }
            }
            WriteFieldProps(ctx, e->pdfdoc, field, p, options);
            StyleFieldWidget(ctx, annot->pdfannot, p);
            UpdateFormFieldPage(ctx, annot->pdfannot);
            pdf_end_operation(ctx, e->pdfdoc);
            ok = true;
        }
        fz_catch(ctx) {
            error = str::DupTemp(Str(fz_caught_message(ctx)));
            pdf_abandon_operation(ctx, e->pdfdoc);
        }
    }
    if (ok) MarkNotificationAsModified(e, annot);
    return ok;
}

Annotation* CreateFormField(EngineBase* engine, int pageNo, RectF rect, const FormFieldProperties& p, Str& error) {
    EngineMupdf* e = AsEngineMupdf(engine);
    StrVec options;
    if (!e || !e->pdfdoc || !EngineSupportsAnnotations(engine) || rect.IsEmpty() ||
        !ValidateFieldProps(p, options, error))
        return nullptr;
    auto* pi = e->GetFzPageInfo(pageNo, true);
    if (!pi || !pi->page) return nullptr;
    auto* ctx = e->BaseCtx();
    Vec<pdf_annot*> created;
    VecReserve(created, p.kind == FormFieldKind::Radio ? len(options) : 1);
    bool ok = false;
    fz_var(ok);
    {
        ScopedRecursiveMutex lock(&e->docLock);
        pdf_begin_operation(ctx, e->pdfdoc, "Create form field");
        fz_try(ctx) {
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, e->pdfdoc), PDF_NAME(Root));
            pdf_obj* acro = pdf_dict_get(ctx, root, PDF_NAME(AcroForm));
            if (!acro) acro = pdf_dict_put_dict(ctx, root, PDF_NAME(AcroForm), 3);
            pdf_obj* fields = pdf_dict_get(ctx, acro, PDF_NAME(Fields));
            if (!fields) fields = pdf_dict_put_array(ctx, acro, PDF_NAME(Fields), 1);
            if (pdf_lookup_field(ctx, fields, CStrTemp(p.name)))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "field name already exists");
            EnsureFieldFont(ctx, e->pdfdoc, p);
            pdf_page* page = pdf_page_from_fz_page(ctx, pi->page);
            pdf_obj* field = nullptr;
            pdf_obj* kids = nullptr;
            if (p.kind == FormFieldKind::Radio) {
                field = pdf_add_new_dict(ctx, e->pdfdoc, 5);
                pdf_array_push_drop(ctx, fields, field);
                pdf_dict_put(ctx, field, PDF_NAME(FT), PDF_NAME(Btn));
                pdf_dict_put_int(ctx, field, PDF_NAME(Ff), PDF_BTN_FIELD_IS_RADIO);
                kids = pdf_dict_put_array(ctx, field, PDF_NAME(Kids), len(options));
            }
            int n = p.kind == FormFieldKind::Radio ? len(options) : 1;
            for (int i = 0; i < n; i++) {
                pdf_annot* a = pdf_create_annot(ctx, page, PDF_ANNOT_WIDGET);
                VecAppend(created, a);
                pdf_obj* obj = pdf_annot_obj(ctx, a);
                RectF r = rect;
                if (kids) {
                    float step = rect.dy / n;
                    r = {rect.x, rect.y + i * step, std::min(rect.dx, step * 0.8f), step * 0.8f};
                    pdf_dict_put(ctx, obj, PDF_NAME(Parent), field);
                    pdf_array_push(ctx, kids, obj);
                    pdf_dict_put_name(ctx, obj, PDF_NAME(AS), CStrTemp(options[i]));
                } else {
                    field = obj;
                    pdf_array_push(ctx, fields, field);
                    pdf_obj* ft = (p.kind == FormFieldKind::Text || p.kind == FormFieldKind::Date)       ? PDF_NAME(Tx)
                                  : (p.kind == FormFieldKind::Combo || p.kind == FormFieldKind::ListBox) ? PDF_NAME(Ch)
                                  : p.kind == FormFieldKind::Signature ? PDF_NAME(Sig)
                                                                       : PDF_NAME(Btn);
                    pdf_dict_put(ctx, obj, PDF_NAME(FT), ft);
                    pdf_dict_put_int(ctx, obj, PDF_NAME(Ff),
                                     p.kind == FormFieldKind::Combo ? PDF_CH_FIELD_IS_COMBO
                                     : (p.kind == FormFieldKind::PushButton || p.kind == FormFieldKind::Image)
                                         ? PDF_BTN_FIELD_IS_PUSHBUTTON
                                         : 0);
                }
                pdf_set_annot_rect(ctx, a, ToFzRect(r));
                pdf_set_annot_border_width(ctx, a, 1);
                pdf_obj* mk = pdf_dict_put_dict(ctx, obj, PDF_NAME(MK), 2);
                pdf_obj* bc = pdf_dict_put_array(ctx, mk, PDF_NAME(BC), 1);
                pdf_array_push_real(ctx, bc, 0);
                StyleFieldWidget(ctx, a, p);
            }
            WriteFieldProps(ctx, e->pdfdoc, field, p, options);
            for (pdf_annot* a : created) StyleFieldWidget(ctx, a, p);
            pdf_end_operation(ctx, e->pdfdoc);
            ok = true;
        }
        fz_catch(ctx) {
            error = str::DupTemp(Str(fz_caught_message(ctx)));
            for (pdf_annot* a : created) pdf_delete_annot(ctx, pdf_annot_page(ctx, a), a);
            pdf_abandon_operation(ctx, e->pdfdoc);
        }
    }
    Annotation* first = nullptr;
    for (pdf_annot* a : created) {
        if (ok) {
            Annotation* wrapper = MakeAnnotationWrapper(e, a, pageNo);
            MarkNotificationAsModified(e, wrapper, AnnotationChange::Add);
            if (!first) first = wrapper;
        }
        pdf_drop_annot(ctx, a);
    }
    return first;
}

Str Contents(Annotation* annot) {
    return ReadAnnot(annot, Str{},
                     [](fz_context* ctx, pdf_annot* a) { return MupdfCStrDupTemp(pdf_annot_contents(ctx, a)); });
}

bool SetContents(Annotation* annot, Str sv) {
    ReportIf(!annot);
    Str currValue = Contents(annot);
    if (str::Eq(sv, currValue)) {
        return false;
    }
    TempStr valueZ = str::DupTemp(sv);
    return UpdateAnnot(annot, [valueZ](fz_context* ctx, pdf_annot* a) {
        pdf_set_annot_contents(ctx, a, len(valueZ) == 0 ? "" : valueZ.s);
    });
}

void DeleteAnnotation(Annotation* annot) {
    ReportIf(!annot);
    if (!annot) {
        return;
    }
    // Caller must have DetachAnnotationFromUI (or have no UI holders).
    EngineMupdf* e = annot->engine;
    if (!e) {
        delete annot;
        return;
    }
    auto* a = annot->pdfannot;
    if (!a) {
        // Already stripped from mupdf; drop from engine list if still present.
        if (IsAnnotationInEngine(e, annot)) {
            MarkNotificationAsModified(e, annot, AnnotationChange::Remove);
        }
        delete annot;
        return;
    }
    if (!IsAnnotationInEngine(e, annot)) {
        logf("DeleteAnnotation: annotation not found in engine, skipping\n");
        // Still free the wrapper so callers do not leak a detached Annotation*.
        annot->pdfannot = nullptr;
        delete annot;
        return;
    }
    bool failed = false;
    ScopedEngineOperation operation(e, "Delete annotation");
    {
        auto* ctx = e->Ctx();
        ScopedRecursiveMutex cs(&e->docLock);
        pdf_page* page = nullptr;
        fz_try(ctx) {
            page = pdf_annot_page(ctx, a);
            pdf_obj* parent = annot->type == AnnotationType::Widget
                                  ? pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(Parent))
                                  : nullptr;
            pdf_delete_annot(ctx, page, a);
            if (parent && !pdf_dict_get(ctx, parent, PDF_NAME(Parent)) &&
                pdf_array_len(ctx, pdf_dict_get(ctx, parent, PDF_NAME(Kids))) == 0) {
                pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, e->pdfdoc), "Root/AcroForm/Fields");
                int index = pdf_array_find(ctx, fields, parent);
                if (index >= 0) pdf_array_delete(ctx, fields, index);
            }
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            failed = true;
        }
    }
    if (failed) {
        logf("failed to delete annotation on page %d\n", annot->pageNo);
        return;
    }
    annot->pdfannot = nullptr;
    // Remove from pageInfo->annotations (must happen while annot is still valid).
    MarkNotificationAsModified(e, annot, AnnotationChange::Remove);
    delete annot;
}

int AnnotationId(Annotation* annot) {
    return ReadAnnot(annot, 0, [](fz_context* ctx, pdf_annot* a) { return pdf_to_num(ctx, pdf_annot_obj(ctx, a)); });
}

int AnnotationReplyTo(Annotation* annot) {
    return ReadAnnot(annot, 0, [](fz_context* ctx, pdf_annot* a) {
        return pdf_to_num(ctx, pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(IRT)));
    });
}

// -1 if not exist
int PopupId(Annotation* annot) {
    return ReadAnnot(annot, -1, [](fz_context* ctx, pdf_annot* a) {
        pdf_obj* obj = pdf_dict_get(ctx, pdf_annot_obj(ctx, a), PDF_NAME(Popup));
        return obj ? pdf_to_num(ctx, obj) : -1;
    });
}

time_t ModificationDate(Annotation* annot) {
    return ReadAnnot(annot, (time_t)0,
                     [](fz_context* ctx, pdf_annot* a) { return (time_t)pdf_annot_modification_date(ctx, a); });
}

// mupdf never touches /M on its own, so whoever changes an annotation has to
// stamp it. Used by paste, which is a brand new annotation whatever date the
// one it was copied from carried.
void SetModificationDateToNow(Annotation* annot) {
    if (!AnnotationIsLive(annot)) {
        return;
    }
    AnnotAccess access(annot);
    fz_try(access.ctx) {
        pdf_set_annot_modification_date(access.ctx, access.annot, time(nullptr));
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
    }
}

// return empty if no icon
Str IconName(Annotation* annot) {
    return ReadAnnot(annot, Str{}, [](fz_context* ctx, pdf_annot* a) {
        if (pdf_annot_has_icon_name(ctx, a)) {
            return MupdfCStrDupTemp(pdf_annot_icon_name(ctx, a));
        }
        return Str{};
    });
}

void SetIconName(Annotation* annot, Str iconName) {
    Str curr = IconName(annot);
    if (str::Eq(curr, iconName)) {
        return;
    }
    TempStr nameZ = str::DupTemp(iconName);
    UpdateAnnot(annot, [nameZ](fz_context* ctx, pdf_annot* a) {
        pdf_set_annot_icon_name(ctx, a, len(nameZ) == 0 ? "" : nameZ.s);
    });
}

static i64 FileTimeToUnixSeconds(FILETIME ft) {
    u64 value = FileTimeToU64(ft);
    if (value < kFileTimeUnixEpoch) {
        return -1;
    }
    return (i64)((value - kFileTimeUnixEpoch) / kFileTimeTicksPerSec);
}

static pdf_obj* FilespecDict(fz_context* ctx, pdf_annot* a) {
    pdf_obj* obj = pdf_annot_obj(ctx, a);
    if (!obj) {
        return nullptr;
    }
    pdf_obj* fs = pdf_dict_get(ctx, obj, PDF_NAME(FS));
    if (!fs || !pdf_is_dict(ctx, fs)) {
        return nullptr;
    }
    return fs;
}

bool HasEmbeddedFile(Annotation* annot) {
    if (!AnnotationIsLive(annot)) {
        return false;
    }
    AnnotAccess access(annot);
    bool ok = false;
    fz_try(access.ctx) {
        pdf_obj* fs = FilespecDict(access.ctx, access.annot);
        ok = fs && pdf_is_embedded_file(access.ctx, fs);
    }
    fz_catch(access.ctx) {
        fz_ignore_error(access.ctx);
    }
    return ok;
}

Str EmbeddedFileNameTemp(Annotation* annot) {
    if (!AnnotationIsLive(annot)) {
        return {};
    }
    AnnotAccess access(annot);
    Str name;
    fz_try(access.ctx) {
        pdf_obj* fs = FilespecDict(access.ctx, access.annot);
        if (fs) {
            pdf_filespec_params params{};
            pdf_get_filespec_params(access.ctx, fs, &params);
            if (params.filename) {
                name = str::DupTemp(Str(params.filename));
            }
        }
    }
    fz_catch(access.ctx) {
        fz_ignore_error(access.ctx);
        name = {};
    }
    return name;
}

// caller owns the result (binary; may not be NUL-terminated)
Str LoadEmbeddedFile(Annotation* annot) {
    if (!AnnotationIsLive(annot)) {
        return {};
    }
    AnnotAccess access(annot);
    Str res;
    fz_try(access.ctx) {
        pdf_obj* fs = FilespecDict(access.ctx, access.annot);
        if (fs && pdf_is_embedded_file(access.ctx, fs)) {
            fz_buffer* buf = pdf_load_embedded_file_contents(access.ctx, fs);
            if (buf) {
                res = str::Dup(Str((char*)buf->data, (int)buf->len));
                fz_drop_buffer(access.ctx, buf);
            }
        }
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
        logf("LoadEmbeddedFile() failed\n");
    }
    return res;
}

bool SetEmbeddedFileFromPath(Annotation* annot, Str path) {
    if (!AnnotationIsLive(annot) || len(path) == 0 || !file::Exists(path)) {
        return false;
    }
    EngineMupdf* e = annot->engine;
    Str data = file::ReadFile(path);
    if (!data.s) {
        return false;
    }
    TempStr name = path::GetBaseNameTemp(path);
    TempStr mime = MimeTypeFromExtTemp(path::GetExtTemp(path));
    i64 modified = FileTimeToUnixSeconds(file::GetModificationTime(path));
    bool ok = false;
    {
        ScopedEngineOperation op(e, "Embed file attachment");
        auto* ctx = e->Ctx();
        ScopedRecursiveMutex cs(&e->docLock);
        pdf_obj* fs = nullptr;
        fz_buffer* buf = nullptr;
        fz_var(fs);
        fz_var(buf);
        fz_try(ctx) {
            buf = fz_new_buffer_from_copied_data(ctx, (const u8*)data.s, (size_t)data.len);
            fs = pdf_add_embedded_file(ctx, e->pdfdoc, CStrTemp(name), mime ? CStrTemp(mime) : nullptr, buf, modified,
                                       modified, 0);
            pdf_set_annot_filespec(ctx, annot->pdfannot, fs);
            pdf_update_annot(ctx, annot->pdfannot);
            ok = true;
        }
        fz_always(ctx) {
            fz_drop_buffer(ctx, buf);
            pdf_drop_obj(ctx, fs);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            logf("SetEmbeddedFileFromPath() failed\n");
            ok = false;
        }
    }
    str::Free(data);
    if (ok) {
        MarkNotificationAsModified(e, annot);
    }
    return ok;
}

void SetLineEndStyles(Annotation* annot, int end) {
    UpdateAnnot(annot,
                [end](fz_context* ctx, pdf_annot* a) { pdf_set_annot_line_end_style(ctx, a, (pdf_line_ending)end); });
}

void SetLineStartStyles(Annotation* annot, int start) {
    UpdateAnnot(annot, [start](fz_context* ctx, pdf_annot* a) {
        pdf_set_annot_line_start_style(ctx, a, (pdf_line_ending)start);
    });
}

static void PdfColorToFloat(PdfColor c, float rgb[3]) {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    UnpackPdfColor(c, r, g, b, a);
    rgb[0] = (float)r / 255.0f;
    rgb[1] = (float)g / 255.0f;
    rgb[2] = (float)b / 255.0f;
}

static float GetOpacityFloat(PdfColor c) {
    u8 alpha = GetAlpha(c);
    return (float)alpha / 255.0f;
}

static PdfColor MkPdfColorFromFloat(float rf, float gf, float bf) {
    u8 r = (u8)(rf * 255.0f);
    u8 g = (u8)(gf * 255.0f);
    u8 b = (u8)(bf * 255.0f);
    return MkPdfColor(r, g, b, 0xff);
}

// n = 1 (grey), 3 (rgb) or 4 (cmyk).
static PdfColor PdfColorFromFloat(fz_context* ctx, int n, float color[4]) {
    if (n == 0) {
        return 0; // transparent
    }
    if (n == 1) {
        return MkPdfColorFromFloat(color[0], color[0], color[0]);
    }
    if (n == 3) {
        return MkPdfColorFromFloat(color[0], color[1], color[2]);
    }
    if (n == 4) {
        float rgb[4]{};
        fz_try(ctx) {
            fz_convert_color(ctx, fz_device_cmyk(ctx), color, fz_device_rgb(ctx), rgb, nullptr,
                             fz_default_color_params);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
        return MkPdfColorFromFloat(rgb[0], rgb[1], rgb[2]);
    }
    ReportIf(true);
    return 0;
}

using AnnotColorReader = void (*)(fz_context*, pdf_annot*, int*, float[4]);

static PdfColor GetAnnotColor(Annotation* annot, AnnotColorReader readColor) {
    return ReadAnnot(annot, (PdfColor)0, [readColor](fz_context* ctx, pdf_annot* a) {
        float color[4]{};
        int n = 0;
        readColor(ctx, a, &n, color);
        return PdfColorFromFloat(ctx, n, color);
    });
}

static bool AnnotColorDiffers(const float* color, int n, const float* newColor, int newN) {
    if (n != newN) {
        return true;
    }
    for (int i = 0; i < n; i++) {
        if (color[i] != newColor[i]) {
            return true;
        }
    }
    return false;
}

PdfColor GetColor(Annotation* annot) {
    return GetAnnotColor(annot, pdf_annot_color);
}

// Highlight, Underline, StrikeOut and Squiggly: /C is the only thing drawn
bool AnnotationIsTextMarkup(AnnotationType tp) {
    switch (tp) {
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::StrikeOut:
        case AnnotationType::Squiggly:
            return true;
        default:
            return false;
    }
}

// return true if color changed
bool SetColor(Annotation* annot, PdfColor c) {
    return UpdateAnnotIf(annot, [annot, c](fz_context* ctx, pdf_annot* a) {
        float color[4]{};
        int n = 0;
        pdf_annot_color(ctx, a, &n, color);
        float oldOpacity = pdf_annot_opacity(ctx, a);

        float newColor[3];
        PdfColorToFloat(c, newColor);
        float opacity = GetOpacityFloat(c);
        bool changed = AnnotColorDiffers(color, n, newColor, 3) || opacity != oldOpacity;
        if (!changed) {
            return false;
        }

        if (c == 0) {
            pdf_set_annot_color(ctx, a, 0, newColor);
            // For text markup /C is the only ink, so an empty one doesn't
            // make the annotation invisible: mupdf synthesizes Acrobat's
            // default yellow for Highlight and a black line for the rest
            // (issue #1994). Opacity 0 is what "transparent" has to mean.
            // Other types keep their opacity: a Square with a transparent
            // stroke still shows /IC, and a FreeText with a transparent
            // background still shows its text.
            if (AnnotationIsTextMarkup(Type(annot))) {
                pdf_set_annot_opacity(ctx, a, 0.f);
            }
        } else {
            pdf_set_annot_color(ctx, a, 3, newColor);
            if (oldOpacity != opacity) {
                pdf_set_annot_opacity(ctx, a, opacity);
            }
        }
        return true;
    });
}

PdfColor InteriorColor(Annotation* annot) {
    return GetAnnotColor(annot, pdf_annot_interior_color);
}

bool SetInteriorColor(Annotation* annot, PdfColor c) {
    return UpdateAnnotIf(annot, [c](fz_context* ctx, pdf_annot* a) {
        float color[4]{};
        int n = 0;
        pdf_annot_interior_color(ctx, a, &n, color);

        float newColor[3]{};
        PdfColorToFloat(c, newColor);
        int newN = (c == 0) ? 0 : 3;
        if (!AnnotColorDiffers(color, n, newColor, newN)) {
            return false;
        }
        pdf_set_annot_interior_color(ctx, a, newN, newColor);
        return true;
    });
}

// clang-format off
SeqStrings gBase14FontFamilies = "Courier\0Helvetica\0Times\0";
// the /DA font of each of gBase14FontFamilies
static SeqStrings gBase14DaFonts = "Cour\0Helv\0TiRo\0";
static SeqStrings gCssTextAligns = "left\0center\0right\0";
// clang-format on

bool IsBase14FontFamily(Str family) {
    return SeqStrIndexIS(gBase14FontFamilies, family) >= 0;
}

static bool IsCustomFreeTextFont(Str family, int style) {
    return style != 0 || !IsBase14FontFamily(family);
}

static Str TrimWS(Str s) {
    int start = 0;
    int end = len(s);
    while (start < end && str::IsWs(s.s[start])) {
        start++;
    }
    while (end > start && str::IsWs(s.s[end - 1])) {
        end--;
    }
    return Str(s.s + start, end - start);
}

// the first family of a CSS list: Georgia for "'Georgia', serif"
static TempStr FirstCssFontFamilyTemp(Str families) {
    int end = 0;
    while (end < len(families) && families.s[end] != ',') {
        end++;
    }
    TempStr family = str::DupTemp(TrimWS(Str(families.s, end)));
    family.len -= str::RemoveCharsInPlace(family, StrL("'\""));
    return family;
}

static bool IsCssBold(Str v) {
    return str::EqI(v, StrL("bold")) || str::EqI(v, StrL("bolder")) || atoi(CStrTemp(v)) >= 600;
}

static bool IsCssItalic(Str v) {
    return str::EqI(v, StrL("italic")) || str::EqI(v, StrL("oblique"));
}

// "bold 12pt Georgia" or, the way Acrobat writes it, "Helvetica,sans-serif 12.0pt"
static void ParseCssFontShorthand(Str v, Str& family, int& style) {
    str::Builder names;
    int i = 0;
    while (i < len(v)) {
        while (i < len(v) && str::IsWs(v.s[i])) {
            i++;
        }
        int start = i;
        while (i < len(v) && !str::IsWs(v.s[i])) {
            i++;
        }
        Str token(v.s + start, i - start);
        if (len(token) == 0) {
            break;
        }
        if (IsCssBold(token)) {
            style |= kFreeTextBold;
            continue;
        }
        if (IsCssItalic(token)) {
            style |= kFreeTextItalic;
            continue;
        }
        // a size, a number weight or a keyword like "normal"
        bool isSize = isdigit((u8)token.s[0]) || token.s[0] == '.';
        if (isSize || str::EqI(token, StrL("normal")) || str::EqI(token, StrL("small-caps"))) {
            continue;
        }
        if (names.len > 0) {
            names.AppendChar(' ');
        }
        names.Append(token);
    }
    if (names.len > 0) {
        family = FirstCssFontFamilyTemp(ToStrTemp(names));
    }
}

// The font of a free text: its /DS style if it has one, else its /DA base-14 font.
static void ReadFreeTextFontLocked(fz_context* ctx, pdf_annot* a, Str& family, int& style) {
    const char* daFont = nullptr;
    float size = 0;
    int n = 0;
    float color[4]{};
    pdf_annot_default_appearance(ctx, a, &daFont, &size, &n, color);
    int idx = daFont ? SeqStrIndexIS(gBase14DaFonts, Str(daFont)) : -1;
    family = SeqStrByIndex(gBase14FontFamilies, idx >= 0 ? idx : 1);
    style = 0;

    CssPullParser parser(Str(pdf_annot_rich_defaults(ctx, a)));
    for (const CssProperty* prop = parser.NextProperty(); prop; prop = parser.NextProperty()) {
        switch (prop->type) {
            case Css_Font_Family:
                family = FirstCssFontFamilyTemp(prop->s);
                break;
            case Css_Font_Weight:
                style |= IsCssBold(TrimWS(prop->s)) ? kFreeTextBold : 0;
                break;
            case Css_Font_Style:
                style |= IsCssItalic(TrimWS(prop->s)) ? kFreeTextItalic : 0;
                break;
            case Css_Text_Decoration:
                style |= str::ContainsI(prop->s, StrL("underline")) ? kFreeTextUnderline : 0;
                break;
            case Css_Font:
                ParseCssFontShorthand(prop->s, family, style);
                break;
            default:
                break;
        }
    }
}

// Setting /DA drops /DS, so write both together: /DA names a base-14 font
// (Helvetica for any other family) for readers that ignore /DS, and /DS, which
// MuPDF lays the text out with, is only there for a font /DA can't describe.
static void WriteFreeTextFontLocked(fz_context* ctx, pdf_annot* a, Str family, int style) {
    const char* daFont = nullptr;
    float size = 0;
    int n = 0;
    float color[4]{};
    pdf_annot_default_appearance(ctx, a, &daFont, &size, &n, color);
    int idx = SeqStrIndexIS(gBase14FontFamilies, family);
    Str newDaFont = SeqStrByIndex(gBase14DaFonts, idx >= 0 ? idx : 1);
    pdf_set_annot_default_appearance(ctx, a, newDaFont.s, size, n, color);
    if (!IsCustomFreeTextFont(family, style)) {
        return;
    }

    TempStr cssFamily = str::DupTemp(family);
    cssFamily.len -= str::RemoveCharsInPlace(cssFamily, StrL("'\";{}"));
    u8 r;
    u8 g;
    u8 b;
    u8 alpha;
    UnpackPdfColor(PdfColorFromFloat(ctx, n, color), r, g, b, alpha);
    Str align = SeqStrByIndex(gCssTextAligns, std::clamp(pdf_annot_quadding(ctx, a), 0, 2));
    str::Builder ds;
    ds.Append(fmt("font-family:'%s';font-size:%gpt;color:#%02x%02x%02x;text-align:%s", cssFamily, size, (int)r, (int)g,
                  (int)b, align));
    if (style & kFreeTextBold) {
        ds.Append(StrL(";font-weight:bold"));
    }
    if (style & kFreeTextItalic) {
        ds.Append(StrL(";font-style:italic"));
    }
    if (style & kFreeTextUnderline) {
        ds.Append(StrL(";text-decoration:underline"));
    }
    pdf_set_annot_rich_defaults(ctx, a, CStrTemp(ToStrTemp(ds)));
}

struct FreeTextFont {
    Str family;
    int style = 0;
};

static FreeTextFont ReadFreeTextFont(Annotation* annot) {
    if (!annot || Type(annot) != AnnotationType::FreeText) {
        return {};
    }
    return ReadAnnot(annot, FreeTextFont{}, [](fz_context* ctx, pdf_annot* a) {
        FreeTextFont font;
        ReadFreeTextFontLocked(ctx, a, font.family, font.style);
        return font;
    });
}

Str FreeTextFontFamily(Annotation* annot) {
    return ReadFreeTextFont(annot).family;
}

int FreeTextFontStyle(Annotation* annot) {
    return ReadFreeTextFont(annot).style;
}

void SetFreeTextFont(Annotation* annot, Str family, int style) {
    if (Type(annot) != AnnotationType::FreeText || len(family) == 0) {
        return;
    }
    UpdateAnnot(annot,
                [family, style](fz_context* ctx, pdf_annot* a) { WriteFreeTextFontLocked(ctx, a, family, style); });
}

struct DefaultAppearance {
    int textSize = 0;
    PdfColor textColor = 0;
};

static DefaultAppearance ReadDefaultAppearance(Annotation* annot) {
    return ReadAnnot(annot, DefaultAppearance{}, [](fz_context* ctx, pdf_annot* a) {
        const char* fontNameZ = nullptr;
        float size = 0;
        int n = 0;
        float color[4]{};
        pdf_annot_default_appearance(ctx, a, &fontNameZ, &size, &n, color);
        return DefaultAppearance{(int)size, PdfColorFromFloat(ctx, n, color)};
    });
}

static void SetDefaultAppearance(Annotation* annot, const int* textSize, const PdfColor* textColor) {
    UpdateAnnot(annot, [textSize, textColor](fz_context* ctx, pdf_annot* a) {
        const char* fontNameZ = nullptr;
        float size = 0;
        int n = 0;
        float color[4]{};
        Str family;
        int style = 0;
        ReadFreeTextFontLocked(ctx, a, family, style);
        pdf_annot_default_appearance(ctx, a, &fontNameZ, &size, &n, color);
        if (textSize) {
            size = (float)*textSize;
        }
        if (textColor) {
            PdfColorToFloat(*textColor, color);
            n = 3;
        }
        pdf_set_annot_default_appearance(ctx, a, fontNameZ, size, n, color);
        if (IsCustomFreeTextFont(family, style)) {
            WriteFreeTextFontLocked(ctx, a, family, style);
        }
    });
}

int DefaultAppearanceTextSize(Annotation* annot) {
    return ReadDefaultAppearance(annot).textSize;
}

void SetDefaultAppearanceTextSize(Annotation* annot, int textSize) {
    SetDefaultAppearance(annot, &textSize, nullptr);
}

PdfColor DefaultAppearanceTextColor(Annotation* annot) {
    return ReadDefaultAppearance(annot).textColor;
}

void SetDefaultAppearanceTextColor(Annotation* annot, PdfColor color) {
    SetDefaultAppearance(annot, nullptr, &color);
}

void GetLineEndingStyles(Annotation* annot, int* start, int* end) {
    if (start) {
        *start = 0;
    }
    if (end) {
        *end = 0;
    }
    if (!AnnotationIsLive(annot)) {
        return;
    }
    AnnotAccess access(annot);
    pdf_line_ending leStart = PDF_ANNOT_LE_NONE;
    pdf_line_ending leEnd = PDF_ANNOT_LE_NONE;
    fz_try(access.ctx) {
        pdf_annot_line_ending_styles(access.ctx, access.annot, &leStart, &leEnd);
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
        logf("GetLineEndingStyles: pdf_annot_line_ending_styles() failed\n");
    }
    if (start) {
        *start = (int)leStart;
    }
    if (end) {
        *end = (int)leEnd;
    }
}

// The /L endpoints of a Line annotation, in page coordinates.
bool GetLinePoints(Annotation* annot, PointF& start, PointF& end) {
    start = {};
    end = {};
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Line) {
        return false;
    }
    AnnotAccess access(annot);
    fz_point aPt{};
    fz_point bPt{};
    bool ok = false;
    fz_try(access.ctx) {
        pdf_annot_line(access.ctx, access.annot, &aPt, &bPt);
        ok = true;
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
        logf("GetLinePoints: pdf_annot_line() failed\n");
    }
    if (!ok) {
        return false;
    }
    start = {aPt.x, aPt.y};
    end = {bPt.x, bPt.y};
    return true;
}

// The two endpoints of a Line annotation (/L). Bounds follow from that.
void SetLinePoints(Annotation* annot, PointF start, PointF end) {
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Line) {
        return;
    }
    EngineMupdf* e = annot->engine;
    bool failed = false;
    {
        AnnotAccess access(annot);
        fz_try(access.ctx) {
            pdf_set_annot_line(access.ctx, access.annot, fz_point{start.x, start.y}, fz_point{end.x, end.y});
            pdf_update_annot(access.ctx, access.annot);
        }
        fz_catch(access.ctx) {
            fz_report_error(access.ctx);
            failed = true;
            logf("SetLinePoints: pdf_set_annot_line() failed\n");
        }
    }
    if (failed) {
        return;
    }
    annot->bounds = GetBounds(annot);
    MarkNotificationAsModified(e, annot);
}

// /Vertices of a PolyLine or Polygon, in page coordinates.
Vec<PointF> GetVertices(Annotation* annot) {
    Vec<PointF> res;
    if (!AnnotationIsLive(annot)) {
        return res;
    }
    if (annot->type != AnnotationType::PolyLine && annot->type != AnnotationType::Polygon) {
        return res;
    }
    AnnotAccess access(annot);
    fz_try(access.ctx) {
        int n = pdf_annot_vertex_count(access.ctx, access.annot);
        for (int i = 0; i < n; i++) {
            fz_point p = pdf_annot_vertex(access.ctx, access.annot, i);
            VecAppend(res, {p.x, p.y});
        }
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
        logf("GetVertices: pdf_annot_vertex() failed\n");
        VecReset(res);
    }
    return res;
}

// Replace /Vertices. Bounds follow from that.
void SetVertices(Annotation* annot, const Vec<PointF>& points) {
    if (!AnnotationIsLive(annot)) {
        return;
    }
    if (annot->type != AnnotationType::PolyLine && annot->type != AnnotationType::Polygon) {
        return;
    }
    if (len(points) < 2) {
        return;
    }
    EngineMupdf* e = annot->engine;
    Vec<fz_point> pts;
    for (int i = 0; i < len(points); i++) {
        VecAppend(pts, {points[i].x, points[i].y});
    }
    bool failed = false;
    {
        AnnotAccess access(annot);
        fz_try(access.ctx) {
            pdf_set_annot_vertices(access.ctx, access.annot, len(pts), pts.els);
            pdf_update_annot(access.ctx, access.annot);
        }
        fz_catch(access.ctx) {
            fz_report_error(access.ctx);
            failed = true;
            logf("SetVertices: pdf_set_annot_vertices() failed\n");
        }
    }
    if (failed) {
        return;
    }
    annot->bounds = GetBounds(annot);
    MarkNotificationAsModified(e, annot);
}

void GetInkList(Annotation* annot, Vec<int>& strokeCounts, Vec<PointF>& points) {
    VecReset(strokeCounts);
    VecReset(points);
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Ink) {
        return;
    }
    AnnotAccess access(annot);
    fz_try(access.ctx) {
        int nStrokes = pdf_annot_ink_list_count(access.ctx, access.annot);
        for (int i = 0; i < nStrokes; i++) {
            int nv = pdf_annot_ink_list_stroke_count(access.ctx, access.annot, i);
            VecAppend(strokeCounts, nv);
            for (int k = 0; k < nv; k++) {
                fz_point p = pdf_annot_ink_list_stroke_vertex(access.ctx, access.annot, i, k);
                VecAppend(points, {p.x, p.y});
            }
        }
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
        logf("GetInkList: pdf_annot_ink_list() failed\n");
        VecReset(strokeCounts);
        VecReset(points);
    }
}

static float PointSegmentDistSq(PointF p, PointF a, PointF b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float lengthSq = (dx * dx) + (dy * dy);
    float t = 0.f;
    if (lengthSq > 0.f) {
        t = (((p.x - a.x) * dx) + ((p.y - a.y) * dy)) / lengthSq;
        t = ClampF(t, 0.f, 1.f);
    }
    float px = a.x + (t * dx);
    float py = a.y + (t * dy);
    dx = p.x - px;
    dy = p.y - py;
    return (dx * dx) + (dy * dy);
}

static bool InkStrokeHit(const Vec<PointF>& points, int start, int count, PointF pt, float radiusSq) {
    if (count == 1) {
        return PointSegmentDistSq(pt, points[start], points[start]) <= radiusSq;
    }
    for (int i = start + 1; i < start + count; i++) {
        if (PointSegmentDistSq(pt, points[i - 1], points[i]) <= radiusSq) {
            return true;
        }
    }
    return false;
}

bool EraseInkStrokes(Vec<int>& strokeCounts, Vec<PointF>& points, PointF pt, float radius) {
    int pointCount = 0;
    for (int count : strokeCounts) {
        if (count < 0) {
            return false;
        }
        pointCount += count;
    }
    if (pointCount != len(points)) {
        ReportIf(true);
        return false;
    }

    bool erased = false;
    float radiusSq = radius * radius;
    int end = len(points);
    for (int i = len(strokeCounts) - 1; i >= 0; i--) {
        int count = strokeCounts[i];
        int start = end - count;
        if (count > 0 && InkStrokeHit(points, start, count, pt, radiusSq)) {
            VecRemoveAtN(points, start, count);
            VecRemoveAt(strokeCounts, i);
            erased = true;
        }
        end = start;
    }
    return erased;
}

InkEraseResult EraseAnnotationInk(Annotation* annot, PointF pt, float radius) {
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Ink) {
        return InkEraseResult::None;
    }

    Vec<int> strokeCounts;
    Vec<PointF> points;
    GetInkList(annot, strokeCounts, points);
    radius += (float)BorderWidth(annot) / 2.f;
    if (!EraseInkStrokes(strokeCounts, points, pt, radius)) {
        return InkEraseResult::None;
    }
    if (len(strokeCounts) == 0) {
        return InkEraseResult::Empty;
    }

    Vec<fz_point> pts;
    VecGrow(pts, len(points));
    for (PointF p : points) {
        VecAppend(pts, {p.x, p.y});
    }
    EngineMupdf* e = annot->engine;
    bool failed = false;
    {
        AnnotAccess access(annot);
        fz_try(access.ctx) {
            pdf_set_annot_ink_list(access.ctx, access.annot, len(strokeCounts), strokeCounts.els, pts.els);
            pdf_update_annot(access.ctx, access.annot);
        }
        fz_catch(access.ctx) {
            fz_report_error(access.ctx);
            failed = true;
            logf("EraseAnnotationInk: pdf_set_annot_ink_list() failed\n");
        }
    }
    if (failed) {
        return InkEraseResult::None;
    }
    annot->bounds = GetBounds(annot);
    MarkNotificationAsModified(e, annot);
    return InkEraseResult::Changed;
}

int BorderWidth(Annotation* annot) {
    return ReadAnnot(annot, 0, [](fz_context* ctx, pdf_annot* a) { return (int)pdf_annot_border(ctx, a); });
}

void SetBorderWidth(Annotation* annot, int newWidth) {
    ReportIf(!annot);
    UpdateAnnot(annot,
                [newWidth](fz_context* ctx, pdf_annot* a) { pdf_set_annot_border_width(ctx, a, (float)newWidth); });
}

int Opacity(Annotation* annot) {
    return ReadAnnot(annot, 0, [](fz_context* ctx, pdf_annot* a) { return (int)(pdf_annot_opacity(ctx, a) * 255.f); });
}

void SetOpacity(Annotation* annot, int newOpacity) {
    ReportIf(newOpacity < 0);
    ReportIf(newOpacity > 255);
    float opacity = (float)setMinMax(newOpacity, 0, 255) / 255.f;
    UpdateAnnot(annot, [opacity](fz_context* ctx, pdf_annot* a) { pdf_set_annot_opacity(ctx, a, opacity); });
}

static Str GetUserTemp() {
    Str u = Str(getenv("USER"));
    if (len(u) == 0) {
        u = Str(getenv("USERNAME"));
    }
    if (len(u) == 0) {
        return StrL("user");
    }
    return u;
}

static TempStr GetAnnotationTextIconTemp() {
    TempStr s = str::DupTemp(gSettings->annotations.textIconType);
    // this way user can use "new paragraph" and we'll match "NewParagraph"
    str::RemoveCharsInPlace(s, StrL(" "));
    int idx = SeqStrIndexIS(gAnnotationTextIcons, s);
    if (idx < 0) {
        return StrL("Note");
    }
    return SeqStrByIndex(gAnnotationTextIcons, idx);
}

enum AnnotationCapability : u8 {
    AnnotMove = 1 << 0,
    AnnotCopy = 1 << 1,
    AnnotResize = 1 << 2,
    AnnotInteriorColor = 1 << 3,
    AnnotBorder = 1 << 4,
    AnnotColor = 1 << 5,
    AnnotOpacity = 1 << 6,
};

static const u8 kAnnotationCapabilities[] = {
    AnnotMove | AnnotCopy | AnnotColor | AnnotOpacity,                                                  // Text
    0,                                                                                                  // Link
    AnnotMove | AnnotCopy | AnnotResize | AnnotBorder | AnnotColor | AnnotOpacity,                      // FreeText
    AnnotMove | AnnotCopy | AnnotResize | AnnotInteriorColor | AnnotBorder | AnnotColor | AnnotOpacity, // Line
    AnnotMove | AnnotCopy | AnnotResize | AnnotInteriorColor | AnnotBorder | AnnotColor | AnnotOpacity, // Square
    AnnotMove | AnnotCopy | AnnotResize | AnnotInteriorColor | AnnotBorder | AnnotColor | AnnotOpacity, // Circle
    AnnotMove | AnnotCopy | AnnotResize | AnnotInteriorColor | AnnotBorder | AnnotColor | AnnotOpacity, // Polygon
    AnnotMove | AnnotCopy | AnnotResize | AnnotInteriorColor | AnnotBorder | AnnotColor | AnnotOpacity, // PolyLine
    AnnotColor | AnnotOpacity,                                                                          // Highlight
    AnnotColor | AnnotOpacity,                                                                          // Underline
    AnnotColor | AnnotOpacity,                                                                          // Squiggly
    AnnotColor | AnnotOpacity,                                                                          // StrikeOut
    AnnotMove | AnnotCopy | AnnotResize | AnnotOpacity,                                                 // Redact
    AnnotMove | AnnotCopy | AnnotResize | AnnotColor | AnnotOpacity,                                    // Stamp
    AnnotMove | AnnotCopy | AnnotColor | AnnotOpacity,                                                  // Caret
    AnnotMove | AnnotCopy | AnnotBorder | AnnotColor | AnnotOpacity,                                    // Ink
    AnnotMove | AnnotResize,                                                                            // Popup
    AnnotMove | AnnotColor | AnnotOpacity, // FileAttachment
    AnnotMove | AnnotColor,                // Sound
    AnnotMove | AnnotResize,               // Movie
    AnnotMove | AnnotResize,               // RichMedia
    AnnotMove | AnnotResize,               // Widget
    0,                                     // Screen
    0,                                     // PrinterMark
    0,                                     // TrapNet
    0,                                     // Watermark
    AnnotMove | AnnotResize,               // ThreeD
    0,                                     // Projection
};
static_assert(dimof(kAnnotationCapabilities) == (int)AnnotationType::Last + 1);

static bool HasAnnotationCapability(AnnotationType tp, AnnotationCapability capability) {
    int idx = (int)tp;
    return idx >= 0 && idx < dimof(kAnnotationCapabilities) && (kAnnotationCapabilities[idx] & capability) != 0;
}

bool AnnotationCanBeMoved(AnnotationType tp) {
    return HasAnnotationCapability(tp, AnnotMove);
}

// Paste puts the copy wherever the mouse is, so copying only makes sense for
// annotations that can be moved. That rules out text markup (highlight,
// underline, squiggly, strike-out): those are anchored to the text they cover
// and a copy dropped elsewhere would mark unrelated text.
// Of the moveable types we also skip Popup, Sound, Movie and Widget, which we
// can't recreate from a snapshot, and FileAttachment, whose embedded file
// stream we don't copy (the paste would be a paperclip with no file).
bool AnnotationCanBeCopied(AnnotationType tp) {
    return HasAnnotationCapability(tp, AnnotCopy);
}

bool AnnotationCanBeResized(AnnotationType tp) {
    return HasAnnotationCapability(tp, AnnotResize);
}

bool AnnotationSupportsInteriorColor(AnnotationType tp) {
    return HasAnnotationCapability(tp, AnnotInteriorColor);
}

bool AnnotationSupportsBorder(AnnotationType tp) {
    return HasAnnotationCapability(tp, AnnotBorder);
}

bool AnnotationSupportsColor(AnnotationType tp) {
    return HasAnnotationCapability(tp, AnnotColor);
}

bool AnnotationSupportsOpacity(AnnotationType tp) {
    return HasAnnotationCapability(tp, AnnotOpacity);
}

Annotation* EngineMupdfCreateAnnotation(EngineBase* engine, int pageNo, PointF pos, AnnotCreateArgs* args) {
    static const float black[3] = {0, 0, 0};

    EngineMupdf* epdf = AsEngineMupdf(engine);
    fz_context* ctx = epdf->Ctx();
    // creating an annotation is a create plus a handful of property changes;
    // Undo should take all of it back in one go
    ScopedEngineOperation op(engine, "Add annotation");

    auto* pageInfo = epdf->GetFzPageInfo(pageNo, true);
    if (!pageInfo || !pageInfo->page) {
        return nullptr;
    }
    pdf_annot* annot = nullptr;
    pdf_page* page = nullptr;
    auto typ = args->annotType;
    auto col = args->col;
    auto bgCol = args->bgCol;
    auto interiorCol = args->interiorCol;
    // Keep the Vec outside fz_try: a MuPDF longjmp would skip its destructor.
    Vec<fz_point> polyLinePoints;
    if (args->polyLinePoints) {
        for (PointF point : *args->polyLinePoints) {
            VecAppend(polyLinePoints, {point.x, point.y});
        }
    }
    Vec<int> inkStrokeCounts;
    Vec<fz_point> inkPoints;
    bool hasInkList = args->inkStrokeCounts && args->inkPoints && len(*args->inkStrokeCounts) > 0;
    int nInkPoints = 0;
    if (hasInkList) {
        for (int count : *args->inkStrokeCounts) {
            if (count <= 0) {
                hasInkList = false;
                break;
            }
            VecAppend(inkStrokeCounts, count);
            nInkPoints += count;
        }
        hasInkList = hasInkList && nInkPoints == len(*args->inkPoints);
        if (hasInkList) {
            for (PointF point : *args->inkPoints) {
                VecAppend(inkPoints, {point.x, point.y});
            }
        }
    }
    {
        ScopedRecursiveMutex cs(&epdf->docLock);

        // pdf_create_annot returns a kept ref; the page list holds another.
        // On failure we must drop our keep and not fall through to the success
        // pdf_drop_annot (that would free the annot while still linked on the
        // page → UAF on the next render; crash reports show stamp create then
        // ACCESS_VIOLATION with float 30.0f as a pointer — stamp "DRAFT" uses h=30).
        fz_try(ctx) {
            page = pdf_page_from_fz_page(ctx, pageInfo->page);
            enum pdf_annot_type atyp = (enum pdf_annot_type)typ;

            annot = pdf_create_annot(ctx, page, atyp);

            pdf_set_annot_modification_date(ctx, annot, time(nullptr));
            if (pdf_annot_has_author(ctx, annot)) {
                Str defAuthor = gSettings->annotations.defaultAuthor;
                // if "(none)" we don't set it
                if (!str::Eq(defAuthor, StrL("(none)"))) {
                    Str author = GetUserTemp();
                    if (!str::IsEmptyOrWhiteSpace(defAuthor)) {
                        author = defAuthor;
                    }
                    pdf_set_annot_author(ctx, annot, CStrTemp(author));
                }
            }

            switch (typ) {
                case AnnotationType::Link:
                case AnnotationType::Popup:
                case AnnotationType::Unknown:
                case AnnotationType::Sound:
                case AnnotationType::Movie:
                case AnnotationType::RichMedia:
                case AnnotationType::Widget:
                case AnnotationType::Screen:
                case AnnotationType::PrinterMark:
                case AnnotationType::Watermark:
                case AnnotationType::TrapNet:
                case AnnotationType::ThreeD:
                case AnnotationType::Projection:
                    // do nothing
                    break;

                case AnnotationType::Highlight:
                case AnnotationType::Underline:
                case AnnotationType::Squiggly:
                case AnnotationType::StrikeOut: {
                    const char* content = CStrTemp(args->content);
                    if (!str::IsEmptyOrWhiteSpace(Str(content))) {
                        pdf_set_annot_contents(ctx, annot, content);
                    }
                } break;
                case AnnotationType::Text:
                case AnnotationType::FreeText:
                case AnnotationType::Stamp:
                case AnnotationType::FileAttachment:
                case AnnotationType::Square:
                case AnnotationType::Circle: {
                    if (args->hasRect) {
                        // free text placed from the toolbar: the rect is the
                        // preview box the user just positioned
                        pdf_set_annot_rect(ctx, annot, ToFzRect(args->rect));
                        break;
                    }
                    fz_rect trect = pdf_annot_rect(ctx, annot);
                    float dx = trect.x1 - trect.x0;
                    trect.x0 = pos.x;
                    trect.x1 = trect.x0 + dx;
                    float dy = trect.y1 - trect.y0;
                    trect.y0 = pos.y;
                    trect.y1 = trect.y0 + dy;
                    pdf_set_annot_rect(ctx, annot, trect);
                } break;
                case AnnotationType::Caret: {
                    // MuPDF draws the caret glyph centered in the rect, so anchor
                    // middle-left at the click point instead of top-left.
                    fz_rect trect = pdf_annot_rect(ctx, annot);
                    float dx = trect.x1 - trect.x0;
                    float dy = trect.y1 - trect.y0;
                    trect.x0 = pos.x;
                    trect.x1 = trect.x0 + dx;
                    trect.y0 = pos.y - (dy / 2);
                    trect.y1 = trect.y0 + dy;
                    pdf_set_annot_rect(ctx, annot, trect);
                } break;
                case AnnotationType::Line: {
                    fz_point a{pos.x, pos.y};
                    fz_point b = args->hasLineEnd ? fz_point{args->lineEnd.x, args->lineEnd.y}
                                                  : fz_point{pos.x + 100, pos.y + 50};
                    pdf_set_annot_line(ctx, annot, a, b);
                } break;
                case AnnotationType::Polygon:
                case AnnotationType::PolyLine: {
                    if (len(polyLinePoints) >= 2) {
                        pdf_set_annot_vertices(ctx, annot, len(polyLinePoints), polyLinePoints.els);
                    } else if (typ == AnnotationType::Polygon) {
                        fz_point points[] = {
                            {pos.x + 50, pos.y},
                            {pos.x + 100, pos.y + 50},
                            {pos.x + 50, pos.y + 90},
                            {pos.x, pos.y + 50},
                        };
                        pdf_set_annot_vertices(ctx, annot, dimof(points), points);
                    } else {
                        fz_point points[] = {
                            {pos.x, pos.y + 70},
                            {pos.x + 30, pos.y},
                            {pos.x + 65, pos.y + 55},
                            {pos.x + 100, pos.y + 10},
                        };
                        pdf_set_annot_vertices(ctx, annot, dimof(points), points);
                    }
                } break;
                case AnnotationType::Ink: {
                    if (hasInkList) {
                        pdf_set_annot_ink_list(ctx, annot, len(inkStrokeCounts), inkStrokeCounts.els, inkPoints.els);
                    } else {
                        fz_point points[] = {
                            {pos.x, pos.y + 30},      {pos.x + 15, pos.y + 5},  {pos.x + 30, pos.y + 45},
                            {pos.x + 48, pos.y + 10}, {pos.x + 65, pos.y + 40}, {pos.x + 85, pos.y + 15},
                        };
                        int count = dimof(points);
                        pdf_set_annot_ink_list(ctx, annot, 1, &count, points);
                    }
                } break;
                case AnnotationType::Redact: {
                    if (args->hasRect) {
                        pdf_set_annot_rect(ctx, annot, ToFzRect(args->rect));
                    } else {
                        fz_rect rect{pos.x, pos.y, pos.x + 100, pos.y + 50};
                        pdf_set_annot_rect(ctx, annot, rect);
                    }
                } break;
            }
            if (typ == AnnotationType::Stamp && args->stampImage) {
                // image stamp (e.g. pasted from the clipboard or Insert Image):
                // embed the image and size the rect to the image's natural size,
                // anchored at pos. PDF has no DeviceBGR, so convert BGR/BGRA to RGB.
                Pixmap* stamp = args->stampImage;
                fz_image* img = nullptr;
                fz_pixmap* pix = nullptr;
                fz_pixmap* rgbPix = nullptr;
                fz_var(img);
                fz_var(pix);
                fz_var(rgbPix);
                fz_try(ctx) {
                    bool isRgb = stamp->format == PixmapFormat::RGBA8;
                    int alpha = stamp->format == PixmapFormat::BGR8 ? 0 : 1;
                    fz_colorspace* srcCs = isRgb ? fz_device_rgb(ctx) : fz_device_bgr(ctx);
                    pix = fz_new_pixmap_with_data(ctx, srcCs, stamp->width, stamp->height, nullptr, alpha,
                                                  stamp->stride, stamp->data);
                    pix->xres = (int)stamp->xres;
                    pix->yres = (int)stamp->yres;
                    if (!isRgb) {
                        rgbPix = fz_convert_pixmap(ctx, pix, fz_device_rgb(ctx), nullptr, nullptr,
                                                   fz_default_color_params, 1);
                        fz_drop_pixmap(ctx, pix);
                        pix = rgbPix;
                        rgbPix = nullptr;
                    }
                    img = fz_new_image_from_pixmap(ctx, pix, nullptr);
                    pdf_set_annot_stamp_image(ctx, annot, img);
                    int xres = img->xres > 0 ? img->xres : 96;
                    int yres = img->yres > 0 ? img->yres : 96;
                    float wPt = (float)img->w * 72.0f / (float)xres;
                    float hPt = (float)img->h * 72.0f / (float)yres;
                    fz_rect r = {pos.x, pos.y, pos.x + wPt, pos.y + hPt};
                    pdf_set_annot_rect(ctx, annot, r);
                }
                fz_always(ctx) {
                    fz_drop_image(ctx, img);
                    fz_drop_pixmap(ctx, rgbPix);
                    fz_drop_pixmap(ctx, pix);
                }
                fz_catch(ctx) {
                    fz_rethrow(ctx);
                }
            }
            // e.g. [CmdCreateAnnotPolyLine borderwidth=2] (#6208)
            if (args->borderWidth >= 0 && AnnotationSupportsBorder(typ)) {
                pdf_set_annot_border_width(ctx, annot, (float)args->borderWidth);
            }
            if (typ == AnnotationType::FreeText) {
                // left is MuPDF's default; leave /Q out of the file for it
                if (args->quadding > kQuaddingLeft) {
                    pdf_set_annot_quadding(ctx, annot, args->quadding);
                }
                const char* content = CStrTemp(args->content);
                pdf_set_annot_contents(ctx, annot, content);
                int fontSize = args->textSize;
                if (fontSize <= 0) {
                    fontSize = 12;
                }
                int nCol = 3;
                const float* fcol = black;
                float textColor[3]{};

                if (col.parsedOk) {
                    PdfColorToFloat(col.pdfCol, textColor);
                    fcol = textColor;
                }
                pdf_set_annot_default_appearance(ctx, annot, "Helv", (float)fontSize, nCol, fcol);
                if (bgCol.parsedOk) {
                    float bgColor[3]{};
                    PdfColorToFloat(bgCol.pdfCol, bgColor);
                    // PdfColor 0 is "no color"; asking for 3 components would
                    // give the annotation a black background instead
                    int nBgCol = (bgCol.pdfCol == 0) ? 0 : 3;
                    pdf_set_annot_color(ctx, annot, nBgCol, bgColor);
                } else {
                    pdf_set_annot_color(ctx, annot, 0, black);
                }
                // 100 is fuly opaque, the default
                if (args->opacity < 100) {
                    float fop = (float)args->opacity / 100.0f;
                    pdf_set_annot_opacity(ctx, annot, fop);
                }
            }

            if (typ == AnnotationType::Ink) {
                // the highlighter brush is an ink stroke as wide and as
                // translucent as a marker
                if (args->opacity < 100) {
                    pdf_set_annot_opacity(ctx, annot, (float)args->opacity / 100.0f);
                }
            }

            if (interiorCol.parsedOk && AnnotationSupportsInteriorColor(typ)) {
                float interiorColor[3]{};
                PdfColorToFloat(interiorCol.pdfCol, interiorColor);
                // PdfColor 0 is "no fill"; 3 components would fill it black
                int nInteriorCol = (interiorCol.pdfCol == 0) ? 0 : 3;
                pdf_set_annot_interior_color(ctx, annot, nInteriorCol, interiorColor);
            }
            pdf_update_annot(ctx, annot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            if (annot) {
                // Unlink + drop page ref, then drop our keep from create.
                // Must not fall through to the success-path pdf_drop_annot.
                if (page) {
                    pdf_delete_annot(ctx, page, annot);
                }
                pdf_drop_annot(ctx, annot);
                annot = nullptr;
            }
        }
        if (!annot) {
            return nullptr;
        }
    }

    auto* res = MakeAnnotationWrapper(epdf, annot, pageNo);
    MarkNotificationAsModified(epdf, res, AnnotationChange::Add);

    if (typ == AnnotationType::Text) {
        TempStr iconName = GetAnnotationTextIconTemp();
        if (!str::EqI(iconName, StrL("Note"))) {
            SetIconName(res, iconName);
        }
    }
    if (col.parsedOk) {
        // FreeText: text color via pdf_set_annot_default_appearance; SetColor is bg
        if (typ != AnnotationType::FreeText) {
            SetColor(res, col.pdfCol);
        }
    }
    // SetColor copies the color's alpha (opaque for #rrggbb like HighlightColor)
    // and would wipe args->opacity set during create (highlighter is 40%).
    if (args->opacity < 100) {
        SetOpacity(res, (args->opacity * 255) / 100);
    }
    pdf_drop_annot(ctx, annot);
    return res;
}

struct AnnotationClipboard {
    bool valid = false;
    AnnotationType type = AnnotationType::Unknown;
    // the annotation's /Rect, not GetBounds(): the bound is expanded by the
    // border width, so a copy -> paste -> copy round trip would grow it
    RectF rect{};
    Str contents;
    Str iconName;
    Str fontFamily;
    int fontStyle = 0;
    PdfColor color = 0;
    bool hasColor = false;
    PdfColor interiorColor = 0;
    bool hasInteriorColor = false;
    PdfColor textColor = 0;
    bool hasTextColor = false;
    int opacity = 255;
    int borderWidth = -1;
    int quadding = -1;
    int textSize = -1;
    int lineStartStyle = 0;
    int lineEndStyle = 0;
    bool hasLine = false;
    PointF lineStart{};
    PointF lineEnd{};
    Vec<PointF> vertices;
    Vec<RectF> quads;
    Vec<int> inkStrokeCounts;
    Vec<PointF> inkPoints;
    Pixmap* stampImage = nullptr;
};

static AnnotationClipboard gAnnotClipboard;

static void ClearAnnotationClipboard() {
    gPendingCutAnnotation = nullptr;
    str::FreePtr(&gAnnotClipboard.contents);
    str::FreePtr(&gAnnotClipboard.iconName);
    str::FreePtr(&gAnnotClipboard.fontFamily);
    gAnnotClipboard.fontStyle = 0;
    FreePixmap(gAnnotClipboard.stampImage);
    gAnnotClipboard.stampImage = nullptr;
    gAnnotClipboard.valid = false;
    gAnnotClipboard.type = AnnotationType::Unknown;
    gAnnotClipboard.rect = {};
    gAnnotClipboard.color = 0;
    gAnnotClipboard.hasColor = false;
    gAnnotClipboard.interiorColor = 0;
    gAnnotClipboard.hasInteriorColor = false;
    gAnnotClipboard.textColor = 0;
    gAnnotClipboard.hasTextColor = false;
    gAnnotClipboard.opacity = 255;
    gAnnotClipboard.borderWidth = -1;
    gAnnotClipboard.quadding = -1;
    gAnnotClipboard.textSize = -1;
    gAnnotClipboard.lineStartStyle = 0;
    gAnnotClipboard.lineEndStyle = 0;
    gAnnotClipboard.hasLine = false;
    gAnnotClipboard.lineStart = {};
    gAnnotClipboard.lineEnd = {};
    VecReset(gAnnotClipboard.vertices);
    VecReset(gAnnotClipboard.quads);
    VecReset(gAnnotClipboard.inkStrokeCounts);
    VecReset(gAnnotClipboard.inkPoints);
}

static Pixmap* PixmapFromRgbFzPixmap(fz_context* ctx, fz_pixmap* src) {
    if (!src || src->w <= 0 || src->h <= 0 || !src->samples) {
        return nullptr;
    }
    fz_pixmap* rgb = nullptr;
    fz_try(ctx) {
        rgb = fz_convert_pixmap(ctx, src, fz_device_rgb(ctx), nullptr, nullptr, fz_default_color_params, 1);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        rgb = nullptr;
    }
    fz_pixmap* use = rgb ? rgb : src;
    int n = use->n;
    if (n < 3 || !use->samples) {
        fz_drop_pixmap(ctx, rgb);
        return nullptr;
    }
    Pixmap* p = AllocPixmap(use->w, use->h, PixmapFormat::RGBA8);
    if (!p) {
        fz_drop_pixmap(ctx, rgb);
        return nullptr;
    }
    p->xres = (float)use->xres;
    p->yres = (float)use->yres;
    p->hasAlpha = true;
    int alphaOff = use->alpha ? n - 1 : -1;
    for (int y = 0; y < use->h; y++) {
        const u8* s = use->samples + (y * use->stride);
        u8* d = p->data + ((ptrdiff_t)y * p->stride);
        for (int x = 0; x < use->w; x++) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = alphaOff >= 0 ? s[alphaOff] : 255;
            s += n;
            d += 4;
        }
    }
    fz_drop_pixmap(ctx, rgb);
    return p;
}

static Pixmap* GetStampImage(Annotation* annot) {
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Stamp) {
        return nullptr;
    }
    EngineMupdf* e = annot->engine;
    if (!e || !e->pdfdoc) {
        return nullptr;
    }
    auto* ctx = e->Ctx();
    ScopedRecursiveMutex cs(&e->docLock);
    fz_image* img = nullptr;
    fz_pixmap* pix = nullptr;
    fz_pixmap* maskPix = nullptr;
    fz_var(img);
    fz_var(pix);
    fz_var(maskPix);
    fz_try(ctx) {
        pdf_obj* obj = pdf_annot_stamp_image_obj(ctx, annot->pdfannot);
        if (obj) {
            img = pdf_load_image(ctx, e->pdfdoc, obj);
            if (img) {
                pix = fz_get_pixmap_from_image(ctx, img, nullptr, nullptr, nullptr, nullptr);
                // Swiftleaf: a transparent image (a signature) keeps its alpha
                // in a separate soft mask
                if (img->mask) {
                    maskPix = fz_get_pixmap_from_image(ctx, img->mask, nullptr, nullptr, nullptr, nullptr);
                }
            }
        }
    }
    fz_always(ctx) {
        fz_drop_image(ctx, img);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        fz_drop_pixmap(ctx, pix);
        fz_drop_pixmap(ctx, maskPix);
        pix = nullptr;
        maskPix = nullptr;
    }
    if (!pix) {
        return nullptr;
    }
    Pixmap* res = PixmapFromRgbFzPixmap(ctx, pix);
    fz_drop_pixmap(ctx, pix);
    bool maskFits = res && maskPix && maskPix->w == res->width && maskPix->h == res->height && maskPix->n >= 1;
    if (maskFits) {
        for (int y = 0; y < res->height; y++) {
            const u8* s = maskPix->samples + ((ptrdiff_t)y * maskPix->stride);
            u8* d = res->data + ((ptrdiff_t)y * res->stride);
            for (int x = 0; x < res->width; x++) {
                d[(x * 4) + 3] = s[x * maskPix->n];
            }
        }
    }
    fz_drop_pixmap(ctx, maskPix);
    return res;
}

bool HasCopiedAnnotation() {
    return gAnnotClipboard.valid;
}

// gAnnotClipboard owns strings, Vecs and a stamp bitmap; free them at exit
void FreeAnnotationClipboard() {
    ClearAnnotationClipboard();
}

// The annotation's /Rect. GetBounds() is pdf_bound_annot(), which is expanded
// by the border width and would grow the annotation on every copy -> paste.
static RectF GetAnnotRect(Annotation* annot) {
    if (!AnnotationIsLive(annot)) {
        return annot ? annot->bounds : RectF{};
    }
    AnnotAccess access(annot);
    fz_rect rc = {};
    bool ok = true;
    fz_try(access.ctx) {
        rc = pdf_annot_rect(access.ctx, access.annot);
    }
    fz_catch(access.ctx) {
        fz_report_error(access.ctx);
        ok = false;
    }
    if (!ok) {
        return GetBounds(annot);
    }
    return ToRectF(rc);
}

// Snapshot the annotation and remember it as the one to delete on paste. A cut
// is a copy plus a delete of the original once the copy lands, so nothing is
// lost if the paste fails.
bool CutAnnotation(Annotation* annot) {
    if (!CopyAnnotation(annot)) {
        return false;
    }
    gPendingCutAnnotation = annot;
    return true;
}

// The annotation a cut is waiting to delete, if it is still around. Clears it:
// only the first paste of a cut removes the original, later ones are copies.
Annotation* TakeCutAnnotation() {
    Annotation* res = gPendingCutAnnotation;
    gPendingCutAnnotation = nullptr;
    return res;
}

// True if annot belongs to engine. Used to find the tab showing the document a
// cut annotation lives in, which can be a different one than we paste into.
bool EngineOwnsAnnotation(EngineBase* engine, Annotation* annot) {
    if (!engine || !annot) {
        return false;
    }
    return AsEngineMupdf(engine) == annot->engine;
}

// Snapshot a live annotation so PasteCopiedAnnotation can recreate it.
bool CopyAnnotation(Annotation* annot) {
    if (!AnnotationIsLive(annot) || !AnnotationCanBeCopied(annot->type)) {
        return false;
    }
    ClearAnnotationClipboard();
    gAnnotClipboard.valid = true;
    gAnnotClipboard.type = annot->type;
    gAnnotClipboard.rect = GetAnnotRect(annot);
    gAnnotClipboard.contents = str::Dup(Contents(annot));
    gAnnotClipboard.iconName = str::Dup(IconName(annot));
    gAnnotClipboard.opacity = Opacity(annot);
    if (AnnotationSupportsBorder(annot->type)) {
        gAnnotClipboard.borderWidth = BorderWidth(annot);
    }
    if (annot->type == AnnotationType::FreeText) {
        gAnnotClipboard.quadding = Quadding(annot);
        gAnnotClipboard.textSize = DefaultAppearanceTextSize(annot);
        gAnnotClipboard.fontFamily = str::Dup(FreeTextFontFamily(annot));
        gAnnotClipboard.fontStyle = FreeTextFontStyle(annot);
        gAnnotClipboard.textColor = DefaultAppearanceTextColor(annot);
        gAnnotClipboard.hasTextColor = true;
        gAnnotClipboard.color = GetColor(annot);
        gAnnotClipboard.hasColor = true;
    } else if (AnnotationSupportsColor(annot->type)) {
        gAnnotClipboard.color = GetColor(annot);
        gAnnotClipboard.hasColor = true;
    }
    if (AnnotationSupportsInteriorColor(annot->type)) {
        gAnnotClipboard.interiorColor = InteriorColor(annot);
        gAnnotClipboard.hasInteriorColor = true;
    }
    GetLineEndingStyles(annot, &gAnnotClipboard.lineStartStyle, &gAnnotClipboard.lineEndStyle);
    gAnnotClipboard.hasLine = GetLinePoints(annot, gAnnotClipboard.lineStart, gAnnotClipboard.lineEnd);
    gAnnotClipboard.vertices = GetVertices(annot);
    gAnnotClipboard.quads = GetQuadPointsAsRect(annot);
    GetInkList(annot, gAnnotClipboard.inkStrokeCounts, gAnnotClipboard.inkPoints);
    gAnnotClipboard.stampImage = GetStampImage(annot);
    return true;
}

static void OffsetPoints(Vec<PointF>& pts, float dx, float dy) {
    for (int i = 0; i < len(pts); i++) {
        pts[i].x += dx;
        pts[i].y += dy;
    }
}

static void OffsetRects(Vec<RectF>& rects, float dx, float dy) {
    for (int i = 0; i < len(rects); i++) {
        rects[i].x += dx;
        rects[i].y += dy;
    }
}

// Recreate the copied annotation so its rect's top-left sits at topLeft.
Annotation* PasteCopiedAnnotation(EngineBase* engine, int pageNo, PointF topLeft) {
    if (!gAnnotClipboard.valid || !engine) {
        return nullptr;
    }
    ScopedEngineOperation op(engine, "Paste annotation");
    AnnotationClipboard& clip = gAnnotClipboard;
    float dx = topLeft.x - clip.rect.x;
    float dy = topLeft.y - clip.rect.y;
    RectF newRect{topLeft.x, topLeft.y, clip.rect.dx, clip.rect.dy};

    Vec<PointF> vertices = clip.vertices;
    OffsetPoints(vertices, dx, dy);
    Vec<PointF> inkPoints = clip.inkPoints;
    OffsetPoints(inkPoints, dx, dy);
    Vec<RectF> quads = clip.quads;
    OffsetRects(quads, dx, dy);

    AnnotCreateArgs args{clip.type};
    args.content = clip.contents;
    if (clip.type == AnnotationType::FreeText) {
        if (clip.hasTextColor) {
            args.col.parsedOk = true;
            args.col.pdfCol = clip.textColor;
        }
        if (clip.hasColor) {
            args.bgCol.parsedOk = true;
            args.bgCol.pdfCol = clip.color;
        }
        args.textSize = clip.textSize;
        args.borderWidth = clip.borderWidth;
        args.quadding = clip.quadding;
    } else if (clip.hasColor) {
        args.col.parsedOk = true;
        args.col.pdfCol = clip.color;
    }
    if (clip.hasInteriorColor) {
        args.interiorCol.parsedOk = true;
        args.interiorCol.pdfCol = clip.interiorColor;
    }
    args.stampImage = clip.stampImage;
    if (clip.hasLine) {
        args.hasLineEnd = true;
        args.lineEnd = {clip.lineEnd.x + dx, clip.lineEnd.y + dy};
    }
    if (len(vertices) >= 2) {
        args.polyLinePoints = &vertices;
    }
    if (len(clip.inkStrokeCounts) > 0) {
        args.inkStrokeCounts = &clip.inkStrokeCounts;
        args.inkPoints = &inkPoints;
    }
    bool useRect = clip.type == AnnotationType::Square || clip.type == AnnotationType::Circle ||
                   (clip.type == AnnotationType::Redact && len(quads) == 0);
    if (useRect) {
        args.hasRect = true;
        args.rect = newRect;
    }

    PointF pos = topLeft;
    if (clip.hasLine) {
        pos = {clip.lineStart.x + dx, clip.lineStart.y + dy};
    }
    Annotation* annot = EngineMupdfCreateAnnotation(engine, pageNo, pos, &args);
    if (!annot) {
        return nullptr;
    }
    if (clip.iconName) {
        SetIconName(annot, clip.iconName);
    }
    if (clip.fontFamily) {
        SetFreeTextFont(annot, clip.fontFamily, clip.fontStyle);
    }
    if (clip.hasLine || clip.type == AnnotationType::PolyLine || clip.type == AnnotationType::Line) {
        SetLineStartStyles(annot, clip.lineStartStyle);
        SetLineEndStyles(annot, clip.lineEndStyle);
    }
    if (clip.type == AnnotationType::FreeText) {
        SetContents(annot, clip.contents);
        if (clip.hasTextColor) {
            SetDefaultAppearanceTextColor(annot, clip.textColor);
        }
        if (clip.textSize > 0) {
            SetDefaultAppearanceTextSize(annot, clip.textSize);
        }
        if (clip.quadding >= 0) {
            SetQuadding(annot, clip.quadding);
        }
    } else if (clip.contents) {
        SetContents(annot, clip.contents);
    }
    if (clip.opacity < 255) {
        SetOpacity(annot, clip.opacity);
    }
    if (AnnotationSupportsBorder(clip.type) && clip.borderWidth >= 0) {
        SetBorderWidth(annot, clip.borderWidth);
    }
    if (len(quads) > 0) {
        SetQuadPointsAsRect(annot, quads);
    }
    switch (clip.type) {
        case AnnotationType::Text:
        case AnnotationType::FreeText:
        case AnnotationType::Stamp:
        case AnnotationType::Caret:
        case AnnotationType::FileAttachment:
            SetRect(annot, newRect);
            break;
        default:
            break;
    }
    // a pasted annotation is new: it is dated now, not when the one it was
    // copied from was last touched. Last, so it also covers the writes above.
    SetModificationDateToNow(annot);
    return annot;
}

AnnotationType CmdIdToAnnotationType(int cmdId) {
    // clang-format off
    switch (cmdId) {
        case CmdCreateAnnotText:           return AnnotationType::Text;
        case CmdCreateAnnotLink:           return AnnotationType::Link;
        case CmdCreateAnnotFreeText:       return AnnotationType::FreeText;
        case CmdCreateAnnotLine:           return AnnotationType::Line;
        case CmdCreateAnnotSquare:         return AnnotationType::Square;
        case CmdCreateAnnotCircle:         return AnnotationType::Circle;
        case CmdCreateAnnotPolygon:        return AnnotationType::Polygon;
        case CmdCreateAnnotPolyLine:       return AnnotationType::PolyLine;
        case CmdCreateAnnotHighlight:      return AnnotationType::Highlight;
        case CmdCreateAnnotUnderline:      return AnnotationType::Underline;
        case CmdCreateAnnotSquiggly:       return AnnotationType::Squiggly;
        case CmdCreateAnnotStrikeOut:      return AnnotationType::StrikeOut;
        case CmdCreateAnnotRedact:         return AnnotationType::Redact;
        case CmdCreateAnnotStamp:          return AnnotationType::Stamp;
        case CmdCreateAnnotCaret:          return AnnotationType::Caret;
        case CmdCreateAnnotInk:            return AnnotationType::Ink;
        case CmdAnnotationHighlightBrush:  return AnnotationType::Highlight;
        case CmdCreateAnnotPopup:          return AnnotationType::Popup;
        case CmdCreateAnnotFileAttachment: return AnnotationType::FileAttachment;
    }
    // clang-format on
    return AnnotationType::Unknown;
}

// Swiftleaf: a stamp that shows a picture (a signature, an inserted image)
// rather than one of the standard stamps (Approved, Draft, ...)
bool IsImageStamp(Annotation* annot) {
    if (!AnnotationIsLive(annot) || annot->type != AnnotationType::Stamp) {
        return false;
    }
    EngineMupdf* e = annot->engine;
    if (!e || !e->pdfdoc) {
        return false;
    }
    auto* ctx = e->Ctx();
    ScopedRecursiveMutex cs(&e->docLock);
    bool res = false;
    fz_try(ctx) {
        res = pdf_annot_stamp_image_obj(ctx, annot->pdfannot) != nullptr;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return res;
}

// the picture of an image stamp, in the BGRA gdi+ draws; the caller owns it
Pixmap* StampImagePixmap(Annotation* annot) {
    Pixmap* p = GetStampImage(annot);
    if (!p) {
        return nullptr;
    }
    if (p->format == PixmapFormat::RGBA8) {
        for (int y = 0; y < p->height; y++) {
            u8* d = p->data + ((ptrdiff_t)y * p->stride);
            for (int x = 0; x < p->width; x++, d += 4) {
                std::swap(d[0], d[2]);
            }
        }
        p->format = PixmapFormat::BGRA8;
    }
    p->hasAlpha = true;
    return p;
}

// hidden annotations aren't drawn; resizing an image stamp hides it while a
// preview stands in for it
void SetAnnotationHidden(Annotation* annot, bool hidden) {
    if (!AnnotationIsLive(annot)) {
        return;
    }
    EngineMupdf* e = annot->engine;
    auto* ctx = e->Ctx();
    {
        ScopedRecursiveMutex cs(&e->docLock);
        fz_try(ctx) {
            int flags = pdf_annot_flags(ctx, annot->pdfannot);
            flags = hidden ? (flags | PDF_ANNOT_IS_HIDDEN) : (flags & ~PDF_ANNOT_IS_HIDDEN);
            pdf_set_annot_flags(ctx, annot->pdfannot, flags);
            pdf_update_annot(ctx, annot->pdfannot);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
    }
    MarkNotificationAsModified(e, annot);
}
