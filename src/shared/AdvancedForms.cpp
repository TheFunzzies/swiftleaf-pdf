/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/JsonParser.h"
#include "base/File.h"
#include "base/GuessFileType.h"
#include <mupdf/pdf.h>
#include "gui/UIModels.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "EngineMupdf.h"
#include "Annotation.h"
#include "AdvancedForms.h"

static constexpr int kMaxCandidates = 2048;

static void AddCandidate(Vec<FormCandidate>& out, int pageNo, RectF r, FormFieldKind kind) {
    if (r.IsEmpty() || len(out) >= kMaxCandidates) return;
    for (const FormCandidate& c : out) {
        if (c.pageNo != pageNo) continue;
        RectF overlap = c.rect.Intersect(r);
        if (overlap.dx * overlap.dy > 0.3f * fz_min(c.rect.dx * c.rect.dy, r.dx * r.dy)) return;
    }
    VecAppend(out, FormCandidate{pageNo, r, kind});
}

struct DetectDevice {
    fz_device super;
    Vec<FormCandidate>* out;
    int pageNo;
};

struct PathShape {
    int segments = 0;
    bool curved = false;
    bool closed = false;
};
static void DetectMove(fz_context*, void*, float, float) {}
static void DetectLine(fz_context*, void* arg, float, float) {
    ((PathShape*)arg)->segments++;
}
static void DetectCurve(fz_context*, void* arg, float, float, float, float, float, float) {
    ((PathShape*)arg)->curved = true;
}
static void DetectClose(fz_context*, void* arg) {
    ((PathShape*)arg)->closed = true;
}

static void DetectPath(fz_context* ctx, fz_device* dev, const fz_path* path, fz_matrix ctm) {
    auto* d = (DetectDevice*)dev;
    PathShape shape;
    const fz_path_walker walker = {DetectMove, DetectLine, DetectCurve, DetectClose};
    fz_walk_path(ctx, path, &walker, &shape);
    if (shape.curved) return;
    RectF r = ToRectF(fz_bound_path(ctx, path, nullptr, ctm));
    if (shape.segments == 1 && r.dx >= 24 && r.dx <= 500 && r.dy < 2) {
        r.y -= 16;
        r.dy = 18;
        AddCandidate(*d->out, d->pageNo, r, FormFieldKind::Text);
    } else if (shape.closed && shape.segments >= 3 && shape.segments <= 4 && r.dx >= 8 && r.dx <= 500 && r.dy >= 8 &&
               r.dy <= 60) {
        bool check = r.dx <= 24 && r.dy <= 24 && fabsf(r.dx - r.dy) < 4;
        AddCandidate(*d->out, d->pageNo, r, check ? FormFieldKind::Checkbox : FormFieldKind::Text);
    }
}
static void DetectStroke(fz_context* ctx, fz_device* d, const fz_path* p, const fz_stroke_state*, fz_matrix m,
                         fz_colorspace*, const float*, float, fz_color_params) {
    DetectPath(ctx, d, p, m);
}
static void DetectFill(fz_context* ctx, fz_device* d, const fz_path* p, int, fz_matrix m, fz_colorspace*, const float*,
                       float, fz_color_params) {
    DetectPath(ctx, d, p, m);
}

bool DetectFormFields(EngineBase* engine, Vec<FormCandidate>& out, Str& error) {
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc || !EngineSupportsAnnotations(engine)) return false;
    VecClear(out);
    for (int pageNo = 1; pageNo <= engine->PageCount() && len(out) < kMaxCandidates; pageNo++) {
        auto* pi = e->GetFzPageInfo(pageNo, true);
        if (!pi || !pi->page) continue;
        auto* ctx = e->BaseCtx();
        ScopedRecursiveMutex lock(&e->docLock);
        fz_stext_page* text = nullptr;
        DetectDevice* dev = nullptr;
        fz_var(text);
        fz_var(dev);
        bool ok = true;
        fz_var(ok);
        fz_try(ctx) {
            fz_stext_options opts{};
            text = fz_new_stext_page_from_page(ctx, pi->page, &opts);
            for (fz_stext_block* b = text->first_block; b; b = b->next) {
                if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
                for (fz_stext_line* line = b->u.t.first_line; line; line = line->next) {
                    fz_stext_char* run = nullptr;
                    int underscores = 0;
                    for (fz_stext_char* c = line->first_char; c; c = c->next) {
                        if (c->c == '_') {
                            if (!run) run = c;
                            underscores++;
                        }
                        if (run && (c->c != '_' || !c->next)) {
                            if (underscores >= 3) {
                                fz_rect r = fz_union_rect(fz_rect_from_quad(run->quad), fz_rect_from_quad(c->quad));
                                AddCandidate(out, pageNo, ToRectF(r), FormFieldKind::Text);
                            }
                            run = nullptr;
                            underscores = 0;
                        }
                        if (c->c == ':' && c != line->first_char) {
                            fz_rect r = fz_rect_from_quad(c->quad);
                            fz_stext_char* next = c->next;
                            while (next && next->c == ' ') next = next->next;
                            float right = next ? fz_rect_from_quad(next->quad).x0 - 4
                                               : fz_min(text->mediabox.x1 - 24, r.x1 + 160);
                            if (right - r.x1 >= 40)
                                AddCandidate(out, pageNo, ToRectF(fz_make_rect(r.x1 + 4, r.y0, right, r.y1 + 3)),
                                             FormFieldKind::Text);
                        }
                    }
                }
            }
            dev = fz_new_derived_device(ctx, DetectDevice);
            dev->out = &out;
            dev->pageNo = pageNo;
            dev->super.stroke_path = DetectStroke;
            dev->super.fill_path = DetectFill;
            fz_run_page_contents(ctx, pi->page, &dev->super, fz_identity, nullptr);
            fz_close_device(ctx, &dev->super);
            RectF box = ToRectF(text->mediabox);
            for (int i = len(out) - 1; i >= 0; i--) {
                if (out[i].pageNo != pageNo) continue;
                out[i].rect = out[i].rect.Intersect(box);
                bool overlap = out[i].rect.IsEmpty();
                for (pdf_annot* a = pdf_first_widget(ctx, pdf_page_from_fz_page(ctx, pi->page)); a;
                     a = pdf_next_widget(ctx, a)) {
                    if (!out[i].rect.Intersect(ToRectF(pdf_annot_rect(ctx, a))).IsEmpty()) overlap = true;
                }
                if (overlap) VecRemoveAt(out, i);
            }
        }
        fz_always(ctx) {
            fz_drop_stext_page(ctx, text);
            fz_drop_device(ctx, (fz_device*)dev);
        }
        fz_catch(ctx) {
            error = str::DupTemp(Str(fz_caught_message(ctx)));
            ok = false;
        }
        if (!ok) {
            VecClear(out);
            return false;
        }
    }
    return true;
}

template <typename Fn>
static bool ChangeForm(EngineBase* engine, const char* label, Str& error, Fn fn) {
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc || !EngineSupportsAnnotations(engine)) return false;
    bool ok = false;
    fz_var(ok);
    auto* ctx = e->BaseCtx();
    {
        ScopedRecursiveMutex lock(&e->docLock);
        pdf_begin_operation(ctx, e->pdfdoc, label);
        fz_try(ctx) {
            fn(ctx, e);
            pdf_end_operation(ctx, e->pdfdoc);
            ok = true;
        }
        fz_catch(ctx) {
            error = str::DupTemp(Str(fz_caught_message(ctx)));
            pdf_abandon_operation(ctx, e->pdfdoc);
        }
    }
    if (ok) {
        EngineMupdfRefreshModifiedState(e);
        for (int p = 1; p <= engine->PageCount(); p++) {
            Vec<Annotation*> widgets;
            EngineMupdfGetPageWidgets(engine, p, widgets);
            if (!len(widgets)) continue;
            {
                ScopedRecursiveMutex lock(&e->docLock);
                fz_try(ctx) {
                    pdf_update_page(ctx, pdf_annot_page(ctx, widgets[0]->pdfannot));
                }
                fz_catch(ctx) {
                    fz_report_error(ctx);
                }
            }
            MarkNotificationAsModified(e, widgets[0]);
        }
    }
    return ok;
}

static pdf_obj* FormRoots(fz_context* ctx, EngineMupdf* e) {
    return pdf_dict_getp(ctx, pdf_trailer(ctx, e->pdfdoc), "Root/AcroForm/Fields");
}
static void CollectFields(fz_context* ctx, pdf_obj* fields, Vec<pdf_obj*>& out, int depth = 0) {
    if (depth > 32) fz_throw(ctx, FZ_ERROR_ARGUMENT, "form hierarchy too deep");
    for (int i = 0; i < pdf_array_len(ctx, fields); i++) {
        pdf_obj* field = pdf_array_get(ctx, fields, i);
        pdf_obj* kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        bool namedKids = false;
        for (int j = 0; j < pdf_array_len(ctx, kids); j++)
            if (pdf_dict_get(ctx, pdf_array_get(ctx, kids, j), PDF_NAME(T))) namedKids = true;
        pdf_obj* ft = pdf_dict_get_inheritable(ctx, field, PDF_NAME(FT));
        if (namedKids || !ft) {
            CollectFields(ctx, kids, out, depth + 1);
            continue;
        }
        if (pdf_name_eq(ctx, ft, PDF_NAME(Sig)) ||
            (pdf_name_eq(ctx, ft, PDF_NAME(Btn)) && (pdf_field_flags(ctx, field) & PDF_BTN_FIELD_IS_PUSHBUTTON)))
            continue;
        VecAppend(out, field);
    }
}

TempStr GetFormTabNames(EngineBase* engine) {
    str::Builder names;
    StrVec seen;
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) return {};
    for (int p = 1; p <= engine->PageCount(); p++) {
        Vec<Annotation*> widgets;
        EngineMupdfGetPageWidgets(engine, p, widgets);
        auto* ctx = e->BaseCtx();
        ScopedRecursiveMutex lock(&e->docLock);
        fz_try(ctx) {
            for (Annotation* w : widgets) {
                char* raw = pdf_load_field_name(ctx, pdf_annot_obj(ctx, w->pdfannot));
                Str name = str::DupTemp(Str(raw));
                fz_free(ctx, raw);
                if (seen.Contains(name)) continue;
                seen.Append(name);
                if (len(names)) names.Append(StrL("\r\n"));
                names.Append(name);
            }
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            return {};
        }
    }
    return ToStrTemp(names);
}

bool SetFormTabOrder(EngineBase* engine, FormTabOrder order, Str manualNames, Str& error) {
    StrVec names;
    Split(&names, manualNames, StrL("\n"), true);
    for (int i = 0; i < len(names); i++) {
        Str s = names[i];
        str::TrimWs(s);
        names.SetAt(i, s);
    }
    if (order == FormTabOrder::Manual) {
        StrVec existing, seen;
        Split(&existing, GetFormTabNames(engine), StrL("\r\n"), true);
        for (Str name : names) {
            if (!len(name) || seen.Contains(name) || !existing.Contains(name)) {
                error = StrL("Manual order must list each field name once.");
                return false;
            }
            seen.Append(name);
        }
        if (len(names) != len(existing)) {
            error = StrL("Manual order must list every field name.");
            return false;
        }
    }
    // Load widgets before acquiring docLock (page loading takes pagesLock first).
    Vec<FzPageInfo*> pages;
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) return false;
    for (int p = 1; p <= engine->PageCount(); p++) {
        Vec<Annotation*> widgets;
        EngineMupdfGetPageWidgets(engine, p, widgets);
        VecAppend(pages, e->GetFzPageInfo(p, true));
    }
    bool ok = ChangeForm(engine, "Set form tab order", error, [&](fz_context* ctx, EngineMupdf* e) {
        if (order == FormTabOrder::Structure && !pdf_dict_getp(ctx, pdf_trailer(ctx, e->pdfdoc), "Root/StructTreeRoot"))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "This PDF has no structure tree. Choose rows, columns or manual order.");
        for (FzPageInfo* pi : pages) {
            if (!pi || !pi->page) continue;
            auto* page = pdf_page_from_fz_page(ctx, pi->page);
            Vec<Annotation*> sorted;
            for (Annotation* w : pi->widgets) VecAppend(sorted, w);
            auto rank = [&](Annotation* w) {
                pdf_obj* field = pdf_annot_obj(ctx, w->pdfannot);
                char* name = pdf_load_field_name(ctx, field);
                int n = names.Find(Str(name));
                fz_free(ctx, name);
                if (n < 0) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Manual order must list every field name.");
                return n;
            };
            for (int i = 1; i < len(sorted); i++) {
                int j = i;
                while (j > 0) {
                    Annotation* a = sorted[j - 1];
                    Annotation* b = sorted[j];
                    bool swap = false;
                    if (order == FormTabOrder::Manual) swap = rank(a) > rank(b);
                    if (order == FormTabOrder::Row)
                        swap = a->bounds.y != b->bounds.y ? a->bounds.y > b->bounds.y : a->bounds.x > b->bounds.x;
                    if (order == FormTabOrder::Column)
                        swap = a->bounds.x != b->bounds.x ? a->bounds.x > b->bounds.x : a->bounds.y > b->bounds.y;
                    if (!swap) break;
                    sorted[j - 1] = b;
                    sorted[j] = a;
                    j--;
                }
            }
            if (order == FormTabOrder::Manual)
                for (Annotation* w : sorted) rank(w);
            pdf_obj* annots = pdf_dict_get(ctx, page->obj, PDF_NAME(Annots));
            pdf_keep_obj(ctx, annots);
            pdf_obj* replacement = pdf_new_array(ctx, e->pdfdoc, pdf_array_len(ctx, annots));
            pdf_dict_put_drop(ctx, page->obj, PDF_NAME(Annots), replacement);
            for (int i = 0; i < pdf_array_len(ctx, annots); i++) {
                pdf_obj* a = pdf_array_get(ctx, annots, i);
                if (!pdf_name_eq(ctx, pdf_dict_get(ctx, a, PDF_NAME(Subtype)), PDF_NAME(Widget)))
                    pdf_array_push(ctx, replacement, a);
            }
            for (Annotation* w : sorted) pdf_array_push(ctx, replacement, pdf_annot_obj(ctx, w->pdfannot));
            pdf_drop_obj(ctx, annots);
            pdf_dict_puts_drop(ctx, page->obj, "Tabs", pdf_new_name(ctx, order == FormTabOrder::Structure ? "S" : "A"));
        }
    });
    if (ok) {
        for (FzPageInfo* pi : pages) {
            if (!pi || !pi->page) continue;
            auto* ctx = e->BaseCtx();
            ScopedRecursiveMutex lock(&e->docLock);
            pdf_obj* annots = pdf_dict_get(ctx, pdf_page_from_fz_page(ctx, pi->page)->obj, PDF_NAME(Annots));
            Vec<Annotation*> sorted;
            for (int i = 0; i < pdf_array_len(ctx, annots); i++)
                for (Annotation* w : pi->widgets)
                    if (!pdf_objcmp(ctx, pdf_array_get(ctx, annots, i), pdf_annot_obj(ctx, w->pdfannot)))
                        VecAppend(sorted, w);
            pi->widgets = sorted;
        }
    }
    return ok;
}

static TempStr LoadFieldJs(fz_context* ctx, pdf_obj* field, const char* event) {
    pdf_obj* aa = pdf_dict_get(ctx, field, PDF_NAME(AA));
    pdf_obj* action = pdf_dict_gets(ctx, aa, event);
    pdf_obj* js = pdf_dict_get(ctx, action, PDF_NAME(JS));
    if (!js || !pdf_name_eq(ctx, pdf_dict_get(ctx, action, PDF_NAME(S)), PDF_NAME(JavaScript))) return {};
    char* code = pdf_load_stream_or_string_as_utf8(ctx, js);
    TempStr result = str::DupTemp(Str(code));
    fz_free(ctx, code);
    return result;
}

static bool CalcOpSupported(Str op) {
    return str::Eq(op, StrL("SUM")) || str::Eq(op, StrL("PRD")) || str::Eq(op, StrL("AVG")) ||
           str::Eq(op, StrL("MIN")) || str::Eq(op, StrL("MAX"));
}

static void SplitCalcSources(Str text, StrVec& names) {
    StrVec lines;
    Split(&lines, text, StrL("\n"), true);
    for (Str name : lines) {
        str::TrimWs(name);
        if (len(name)) names.Append(name);
    }
}

struct CalcSourcesJson {
    StrVec* names;
    bool valid = true;
    void OnValue(json::Value* v) {
        if (v->type != json::Type::String) {
            valid = false;
            return;
        }
        names->Append(v->value);
    }
};

static void ReadCalcSources(fz_context* ctx, pdf_obj* field, Str& op, StrVec& names) {
    TempStr code = LoadFieldJs(ctx, field, "C");
    const char prefix[] = "AFSimple_Calculate(\"";
    if (!str::StartsWith(code, Str(prefix))) return;
    const char* z = CStrTemp(code);
    const char* list = strstr(z + sizeof(prefix) - 1, "\", [");
    if (!list || !str::EndsWith(code, StrL("]);"))) return;
    op = Str(z + sizeof(prefix) - 1, (int)(list - z - sizeof(prefix) + 1));
    if (!CalcOpSupported(op)) {
        op = {};
        return;
    }
    list += 2;
    Str json = Str(list, (int)(z + len(code) - 1 - list));
    CalcSourcesJson state{&names};
    if (!json::Parse(json, MkMethod1<CalcSourcesJson, json::Value*, &CalcSourcesJson::OnValue>(&state)) ||
        !state.valid || !len(names)) {
        op = {};
        names.Reset();
    }
}

static TempStr JoinCalcSources(const StrVec& names) {
    str::Builder out;
    for (Str name : names) {
        if (len(out)) out.Append(StrL("\r\n"));
        out.Append(name);
    }
    return ToStrTemp(out);
}

static bool ParseDecimals(Str format, Str prefix, int& decimals) {
    if (!str::StartsWith(format, prefix)) return false;
    Str value = format;
    value.s += len(prefix);
    value.len -= len(prefix);
    char extra;
    return sscanf_s(CStrTemp(value), "%d %c", &decimals, &extra, 1u) == 1 && decimals >= 0 && decimals <= 10;
}

static TempStr FormatAction(fz_context* ctx, Str format, bool keystroke) {
    if (!len(format)) return {};
    int decimals = 0;
    if (ParseDecimals(format, StrL("number:"), decimals))
        return fmt(
            keystroke ? "AFNumber_Keystroke(%d, 0, 0, 0, \"\", true);" : "AFNumber_Format(%d, 0, 0, 0, \"\", true);",
            decimals);
    if (ParseDecimals(format, StrL("percent:"), decimals))
        return fmt(keystroke ? "AFPercent_Keystroke(%d, 0);" : "AFPercent_Format(%d, 0);", decimals);
    if (str::Eq(format, StrL("zip"))) return StrL(keystroke ? "AFSpecial_Keystroke(0);" : "AFSpecial_Format(0);");
    if (str::Eq(format, StrL("phone"))) return StrL(keystroke ? "AFSpecial_Keystroke(2);" : "AFSpecial_Format(2);");
    if (str::StartsWith(format, StrL("date:"))) {
        Str pattern = format;
        pattern.s += 5;
        pattern.len -= 5;
        if (!len(pattern) || len(pattern) > 128)
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Enter a date format of 1 to 128 characters");
        for (int i = 0; i < len(pattern); i++) {
            u8 c = pattern.s[i];
            if (c >= 0x80 || (!isalnum(c) && !strchr(" /-.,:", c)))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Date formats use ASCII letters, digits, spaces and /-.,:");
        }
        return fmt(keystroke ? "AFDate_KeystrokeEx(\"%s\");" : "AFDate_FormatEx(\"%s\");", pattern);
    }
    fz_throw(ctx, FZ_ERROR_ARGUMENT, "Format must be number:0-10, percent:0-10, date:PATTERN, zip or phone");
}

static void PutFieldAction(fz_context* ctx, pdf_obj* field, const char* event, Str code) {
    pdf_obj* aa = pdf_dict_get(ctx, field, PDF_NAME(AA));
    if (!len(code)) {
        if (aa) pdf_dict_dels(ctx, aa, event);
        if (aa && pdf_dict_len(ctx, aa) == 0) pdf_dict_del(ctx, field, PDF_NAME(AA));
        return;
    }
    if (!aa) aa = pdf_dict_put_dict(ctx, field, PDF_NAME(AA), 1);
    pdf_obj* action = pdf_new_dict(ctx, pdf_get_bound_document(ctx, field), 2);
    pdf_dict_puts_drop(ctx, aa, event, action);
    pdf_dict_put(ctx, action, PDF_NAME(S), PDF_NAME(JavaScript));
    pdf_dict_put_text_string(ctx, action, PDF_NAME(JS), CStrTemp(code));
}

static double ReadRangeLimit(fz_context* ctx, Str text, bool& present) {
    str::TrimWs(text);
    present = len(text) != 0;
    if (!present) return 0;
    char* end = nullptr;
    double value = strtod(CStrTemp(text), &end);
    if (!end || *end || !isfinite(value)) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Validation limits must be finite numbers");
    return value;
}

static bool HasCalculation(fz_context* ctx, pdf_obj* field) {
    return pdf_dict_getp_inheritable(ctx, field, "AA/C/JS") != nullptr;
}

static void CollectCalcOrder(fz_context* ctx, EngineMupdf* e, Vec<pdf_obj*>& out) {
    pdf_obj* acro = pdf_dict_getp(ctx, pdf_trailer(ctx, e->pdfdoc), "Root/AcroForm");
    pdf_obj* co = pdf_dict_get(ctx, acro, PDF_NAME(CO));
    for (int i = 0; i < pdf_array_len(ctx, co); i++) {
        pdf_obj* field = pdf_array_get(ctx, co, i);
        if (HasCalculation(ctx, field)) VecAppend(out, field);
    }
    Vec<pdf_obj*> fields;
    CollectFields(ctx, FormRoots(ctx, e), fields);
    for (pdf_obj* field : fields) {
        if (!HasCalculation(ctx, field)) continue;
        bool found = false;
        for (pdf_obj* existing : out)
            if (!pdf_objcmp(ctx, existing, field)) found = true;
        if (!found) VecAppend(out, field);
    }
}

static void ValidateCalcOrder(fz_context* ctx, EngineMupdf* e, const Vec<pdf_obj*>& order) {
    for (int i = 0; i < len(order); i++) {
        Str op;
        StrVec sources;
        ReadCalcSources(ctx, order[i], op, sources);
        for (Str name : sources) {
            pdf_obj* source = pdf_lookup_field(ctx, FormRoots(ctx, e), CStrTemp(name));
            if (!source) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Unknown calculation source: %s", CStrTemp(name));
            if (!HasCalculation(ctx, source)) continue;
            int sourceIndex = -1;
            for (int j = 0; j < len(order); j++)
                if (!pdf_objcmp(ctx, source, order[j])) sourceIndex = j;
            if (sourceIndex < 0 || sourceIndex >= i)
                fz_throw(ctx, FZ_ERROR_ARGUMENT,
                         "Calculation sources must precede dependent fields in calculation order");
        }
    }
}

static void SyncCalcOrder(fz_context* ctx, EngineMupdf* e) {
    Vec<pdf_obj*> order;
    CollectCalcOrder(ctx, e, order);
    ValidateCalcOrder(ctx, e, order);
    pdf_obj* acro = pdf_dict_getp(ctx, pdf_trailer(ctx, e->pdfdoc), "Root/AcroForm");
    if (!len(order)) {
        pdf_dict_del(ctx, acro, PDF_NAME(CO));
        return;
    }
    pdf_obj* co = pdf_dict_put_array(ctx, acro, PDF_NAME(CO), len(order));
    for (pdf_obj* field : order) pdf_array_push(ctx, co, field);
}

void ReadFormActions(fz_context* ctx, pdf_obj* field, FormFieldProperties& props) {
    props.format = str::DupTemp(Str(pdf_to_text_string(ctx, pdf_dict_gets(ctx, field, "SwiftLeafFormat"))));
    TempStr format = LoadFieldJs(ctx, field, "F");
    int decimals = 0;
    char extra;
    const char* z = CStrTemp(format);
    if (sscanf_s(z, "AFNumber_Format(%d, 0, 0, 0, \"\", true); %c", &decimals, &extra, 1u) == 1)
        props.format = fmt("number:%d", decimals);
    else if (sscanf_s(z, "AFPercent_Format(%d, 0); %c", &decimals, &extra, 1u) == 1)
        props.format = fmt("percent:%d", decimals);
    else if (str::Eq(format, StrL("AFSpecial_Format(0);")))
        props.format = StrL("zip");
    else if (str::Eq(format, StrL("AFSpecial_Format(2);")))
        props.format = StrL("phone");
    else if (str::StartsWith(format, StrL("AFDate_FormatEx(\"")) && str::EndsWith(format, StrL("\");"))) {
        const char prefix[] = "AFDate_FormatEx(\"";
        props.format = fmt("date:%s", Str(z + sizeof(prefix) - 1, len(format) - sizeof(prefix) - 2));
    }

    TempStr validate = LoadFieldJs(ctx, field, "V");
    int lowerCheck = 0, upperCheck = 0;
    double lower = 0, upper = 0;
    if (sscanf_s(CStrTemp(validate), "AFRange_Validate(%d, %lf, %d, %lf); %c", &lowerCheck, &lower, &upperCheck, &upper,
                 &extra, 1u) == 4) {
        if (lowerCheck) props.validationMin = fmt("%g", lower);
        if (upperCheck) props.validationMax = fmt("%g", upper);
    }
    StrVec sources;
    ReadCalcSources(ctx, field, props.calculation, sources);
    props.calculationFields = JoinCalcSources(sources);
}

void WriteFormActions(fz_context* ctx, pdf_document* doc, pdf_obj* field, const FormFieldProperties& props) {
    pdf_dict_dels(ctx, field, "SwiftLeafFormat");
    bool text = props.kind == FormFieldKind::Text || props.kind == FormFieldKind::Date;
    if (!text && (len(props.format) || len(props.calculation) || len(props.calculationFields) ||
                  len(props.validationMin) || len(props.validationMax)))
        fz_throw(ctx, FZ_ERROR_ARGUMENT, "Calculations, validation and formats require a text field");
    if (props.kind == FormFieldKind::Date && !str::StartsWith(props.format, StrL("date:")))
        fz_throw(ctx, FZ_ERROR_ARGUMENT, "Date fields require date:PATTERN format");
    if (props.kind == FormFieldKind::Date && (len(props.calculation) || len(props.calculationFields) ||
                                              len(props.validationMin) || len(props.validationMax)))
        fz_throw(ctx, FZ_ERROR_ARGUMENT, "Numeric calculations and ranges require a non-date text field");
    PutFieldAction(ctx, field, "F", FormatAction(ctx, props.format, false));
    PutFieldAction(ctx, field, "K", FormatAction(ctx, props.format, true));

    bool hasMin = false, hasMax = false;
    double min = ReadRangeLimit(ctx, props.validationMin, hasMin);
    double max = ReadRangeLimit(ctx, props.validationMax, hasMax);
    if (hasMin && hasMax && min > max) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Minimum validation limit exceeds maximum");
    TempStr range;
    if (hasMin || hasMax)
        range = fmt("AFRange_Validate(%s, %.17g, %s, %.17g);", Str(hasMin ? "true" : "false"), min,
                    Str(hasMax ? "true" : "false"), max);
    PutFieldAction(ctx, field, "V", range);

    StrVec sources;
    SplitCalcSources(props.calculationFields, sources);
    if (!len(props.calculation) && !len(sources)) {
        PutFieldAction(ctx, field, "C", {});
    } else {
        if (!CalcOpSupported(props.calculation) || !len(sources) || len(sources) > 64)
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Choose a calculation and enter 1 to 64 source fields");
        char* targetName = pdf_load_field_name(ctx, field);
        Str target = str::DupTemp(Str(targetName));
        fz_free(ctx, targetName);
        StrVec seen;
        str::Builder script;
        script.Append(fmt("AFSimple_Calculate(\"%s\", [", Str(props.calculation)));
        for (Str source : sources) {
            if (seen.Contains(source) || str::Eq(source, target))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Calculation sources must be unique and exclude the target");
            seen.Append(source);
            if (!pdf_lookup_field(ctx, pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/AcroForm/Fields"),
                                  CStrTemp(source)))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Unknown calculation source: %s", CStrTemp(source));
            if (len(seen) > 1) script.Append(StrL(", "));
            script.Append(fmt("\"%s\"", json::EscapeStrTemp(source)));
        }
        script.Append(StrL("]);"));
        PutFieldAction(ctx, field, "C", ToStrTemp(script));
    }
}

void SyncFormCalculationOrder(fz_context* ctx, EngineMupdf* e) {
    SyncCalcOrder(ctx, e);
}

TempStr GetFormCalculationNames(EngineBase* engine) {
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) return {};
    str::Builder names;
    auto* ctx = e->BaseCtx();
    ScopedRecursiveMutex lock(&e->docLock);
    fz_try(ctx) {
        Vec<pdf_obj*> fields;
        CollectCalcOrder(ctx, e, fields);
        for (pdf_obj* field : fields) {
            char* raw = pdf_load_field_name(ctx, field);
            if (len(names)) names.Append(StrL("\r\n"));
            names.Append(Str(raw));
            fz_free(ctx, raw);
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        return {};
    }
    return ToStrTemp(names);
}

bool SetFormCalculationOrder(EngineBase* engine, Str manualNames, Str& error) {
    StrVec names;
    SplitCalcSources(manualNames, names);
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) return false;
    return ChangeForm(engine, "Set form calculation order", error, [&](fz_context* ctx, EngineMupdf* e) {
        Vec<pdf_obj*> existing;
        CollectCalcOrder(ctx, e, existing);
        if (len(names) != len(existing)) fz_throw(ctx, FZ_ERROR_ARGUMENT, "List every calculated field name once");
        StrVec seen;
        Vec<pdf_obj*> order;
        for (Str name : names) {
            if (seen.Contains(name)) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Calculation field names must be unique");
            seen.Append(name);
            pdf_obj* field = pdf_lookup_field(ctx, FormRoots(ctx, e), CStrTemp(name));
            if (!field || !HasCalculation(ctx, field))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Unknown calculated field: %s", CStrTemp(name));
            VecAppend(order, field);
        }
        ValidateCalcOrder(ctx, e, order);
        pdf_obj* acro = pdf_dict_getp(ctx, pdf_trailer(ctx, e->pdfdoc), "Root/AcroForm");
        if (!len(order)) {
            pdf_dict_del(ctx, acro, PDF_NAME(CO));
            return;
        }
        pdf_obj* co = pdf_dict_put_array(ctx, acro, PDF_NAME(CO), len(order));
        for (pdf_obj* field : order) pdf_array_push(ctx, co, field);
    });
}

static void AppendXml(str::Builder& out, Str value) {
    for (int i = 0; i < len(value); i++) {
        switch (value.s[i]) {
            case '&':
                out.Append(StrL("&amp;"));
                break;
            case '<':
                out.Append(StrL("&lt;"));
                break;
            case '>':
                out.Append(StrL("&gt;"));
                break;
            case '"':
                out.Append(StrL("&quot;"));
                break;
            case '\r':
                out.Append(StrL("&#13;"));
                break;
            case '\n':
                out.Append(StrL("&#10;"));
                break;
            default:
                out.AppendChar(value.s[i]);
                break;
        }
    }
}
static void AppendCsv(str::Builder& out, Str value) {
    out.AppendChar('"');
    for (int i = 0; i < len(value); i++) {
        if (value.s[i] == '"') out.AppendChar('"');
        out.AppendChar(value.s[i]);
    }
    out.AppendChar('"');
}
static void AppendPdfString(str::Builder& out, Str value) {
    WStr w = ToWStrTemp(value);
    out.Append(StrL("<FEFF"));
    for (int i = 0; i < len(w); i++) out.Append(fmt("%04X", (unsigned)w.s[i]));
    out.AppendChar('>');
}

TempStr ExportFormData(EngineBase* engine, FormDataFormat format, Str& error) {
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) return {};
    str::Builder out;
    Vec<pdf_obj*> fields;
    auto* ctx = e->BaseCtx();
    ScopedRecursiveMutex lock(&e->docLock);
    bool ok = true;
    fz_var(ok);
    fz_try(ctx) {
        CollectFields(ctx, FormRoots(ctx, e), fields);
        if (format == FormDataFormat::Fdf) out.Append(StrL("%FDF-1.2\n1 0 obj\n<< /FDF << /Fields [\n"));
        if (format == FormDataFormat::Xfdf)
            out.Append(StrL(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><fields>\n"));
        if (format == FormDataFormat::Csv) out.Append(StrL("name,value\r\n"));
        if (format == FormDataFormat::Json) out.AppendChar('{');
        for (int i = 0; i < len(fields); i++) {
            char* raw = pdf_load_field_name(ctx, fields[i]);
            Str name = str::DupTemp(Str(raw));
            fz_free(ctx, raw);
            Str value = Str(pdf_field_value(ctx, fields[i]));
            if (pdf_is_array(ctx, pdf_dict_get(ctx, fields[i], PDF_NAME(V))))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Multi-select values are not supported by this data format.");
            if (format == FormDataFormat::Json) {
                if (i) out.AppendChar(',');
                out.Append(fmt("\n\"%s\":\"%s\"", json::EscapeStrTemp(name), json::EscapeStrTemp(value)));
            } else if (format == FormDataFormat::Csv) {
                AppendCsv(out, name);
                out.AppendChar(',');
                AppendCsv(out, value);
                out.Append(StrL("\r\n"));
            } else if (format == FormDataFormat::Xfdf) {
                out.Append(StrL("<field name=\""));
                AppendXml(out, name);
                out.Append(StrL("\"><value>"));
                AppendXml(out, value);
                out.Append(StrL("</value></field>\n"));
            } else {
                out.Append(StrL("<< /T "));
                AppendPdfString(out, name);
                out.Append(StrL(" /V "));
                AppendPdfString(out, value);
                out.Append(StrL(" >>\n"));
            }
        }
        if (format == FormDataFormat::Fdf) out.Append(StrL("] >> >>\nendobj\ntrailer\n<< /Root 1 0 R >>\n%%EOF\n"));
        if (format == FormDataFormat::Xfdf) out.Append(StrL("</fields></xfdf>\n"));
        if (format == FormDataFormat::Json) out.Append(StrL("\n}\n"));
    }
    fz_catch(ctx) {
        error = str::DupTemp(Str(fz_caught_message(ctx)));
        ok = false;
    }
    return ok ? ToStrTemp(out) : TempStr{};
}

static bool ParseCsv(Str data, StrVec& cells) {
    str::Builder cell;
    bool quoted = false, closed = false;
    int column = 0;
    for (int i = 0; i < len(data); i++) {
        char c = data.s[i];
        if (quoted) {
            if (c != '"') {
                cell.AppendChar(c);
                continue;
            }
            if (i + 1 < len(data) && data.s[i + 1] == '"') {
                cell.AppendChar('"');
                i++;
                continue;
            }
            quoted = false;
            closed = true;
            continue;
        }
        if (c == '"' && !len(cell) && !closed) {
            quoted = true;
            continue;
        }
        if (c == ',' || c == '\n' || c == '\r') {
            cells.Append(ToStrTemp(cell));
            cell.Reset();
            closed = false;
            column++;
            if (c == ',') {
                if (column != 1) return false;
                continue;
            }
            if (column != 2) return false;
            column = 0;
            if (c == '\r' && i + 1 < len(data) && data.s[i + 1] == '\n') i++;
            continue;
        }
        if (closed || c == '"') return false;
        cell.AppendChar(c);
    }
    if (quoted) return false;
    if (len(cell) || closed || column) {
        cells.Append(ToStrTemp(cell));
        column++;
    }
    return (column == 0 || column == 2) && len(cells) >= 2 && len(cells) % 2 == 0;
}

static void ReadXmlFields(fz_context* ctx, fz_xml* node, Str parent, StrVec& names, StrVec& values, int depth) {
    if (depth > 32) fz_throw(ctx, FZ_ERROR_ARGUMENT, "XFDF hierarchy too deep");
    for (; node; node = fz_xml_next(node)) {
        if (!fz_xml_is_tag(node, "field")) continue;
        Str name = Str(fz_xml_att(node, "name"));
        if (!len(name)) fz_throw(ctx, FZ_ERROR_ARGUMENT, "XFDF field needs a name");
        if (len(parent))
            name = fmt("%s.%s", parent, name);
        else
            name = str::DupTemp(name);
        fz_xml* child = fz_xml_down(node);
        int count = 0;
        for (fz_xml* v = child; v; v = fz_xml_next(v)) {
            if (!fz_xml_is_tag(v, "value")) continue;
            if (++count > 1) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Multi-select XFDF is not supported");
            names.Append(name);
            const char* val = fz_xml_text(fz_xml_down(v));
            values.Append(str::DupTemp(Str(val)));
        }
        ReadXmlFields(ctx, child, name, names, values, depth + 1);
    }
}
static void ReadFdfFields(fz_context* ctx, pdf_obj* fields, Str parent, StrVec& names, StrVec& values, int depth) {
    if (depth > 32) fz_throw(ctx, FZ_ERROR_ARGUMENT, "FDF hierarchy too deep");
    for (int i = 0; i < pdf_array_len(ctx, fields); i++) {
        pdf_obj* field = pdf_array_get(ctx, fields, i);
        Str name = Str(pdf_dict_get_text_string(ctx, field, PDF_NAME(T)));
        if (len(parent))
            name = fmt("%s.%s", parent, name);
        else
            name = str::DupTemp(name);
        pdf_obj* v = pdf_dict_get(ctx, field, PDF_NAME(V));
        if (v) {
            if (!pdf_is_name(ctx, v) && !pdf_is_string(ctx, v))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "FDF value must be a string or name");
            names.Append(name);
            const char* val = pdf_is_name(ctx, v) ? pdf_to_name(ctx, v) : pdf_to_text_string(ctx, v);
            values.Append(str::DupTemp(Str(val)));
        }
        ReadFdfFields(ctx, pdf_dict_get(ctx, field, PDF_NAME(Kids)), name, names, values, depth + 1);
    }
}

bool ImportFormData(EngineBase* engine, FormDataFormat format, Str data, Str& error) {
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc || !len(data) || len(data) > 16 * 1024 * 1024) {
        error = StrL("Empty or oversized form data (limit: 16 MB).");
        return false;
    }
    StrVec names, values;
    bool valid = true;
    if (format == FormDataFormat::Json) {
        struct JsonFields {
            StrVec* names;
            StrVec* values;
            bool valid = true;
            void OnValue(json::Value* v) {
                if (!v->path || v->path->next || v->path->s.s[0] != json::kSegKey || v->type != json::Type::String) {
                    valid = false;
                    return;
                }
                names->Append(json::PathSegKey(v->path));
                values->Append(v->value);
            }
        } state{&names, &values};
        valid = json::Parse(data, MkMethod1<JsonFields, json::Value*, &JsonFields::OnValue>(&state)) && state.valid;
    } else if (format == FormDataFormat::Csv) {
        StrVec cells;
        valid = ParseCsv(data, cells) && str::Eq(cells[0], StrL("name")) && str::Eq(cells[1], StrL("value"));
        if (valid)
            for (int i = 2; i < len(cells); i += 2) {
                names.Append(cells[i]);
                values.Append(cells[i + 1]);
            }
    } else {
        auto* ctx = e->BaseCtx();
        ScopedRecursiveMutex lock(&e->docLock);
        fz_buffer* buffer = nullptr;
        fz_stream* stream = nullptr;
        pdf_document* fdf = nullptr;
        fz_xml* xml = nullptr;
        fz_var(buffer);
        fz_var(stream);
        fz_var(fdf);
        fz_var(xml);
        fz_var(valid);
        fz_try(ctx) {
            buffer = fz_new_buffer_from_copied_data(ctx, (const u8*)data.s, len(data));
            if (format == FormDataFormat::Xfdf) {
                xml = fz_parse_xml(ctx, buffer, 1);
                fz_xml* root = fz_xml_root(xml);
                if (!fz_xml_is_tag(root, "xfdf")) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Expected an XFDF document");
                ReadXmlFields(ctx, fz_xml_down(fz_xml_find_down(root, "fields")), {}, names, values, 0);
            } else {
                stream = fz_open_buffer(ctx, buffer);
                fdf = pdf_open_document_with_stream(ctx, stream);
                pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, fdf), "Root/FDF/Fields");
                if (!fields) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Expected an FDF document");
                ReadFdfFields(ctx, fields, {}, names, values, 0);
            }
        }
        fz_always(ctx) {
            fz_drop_xml(ctx, xml);
            pdf_drop_document(ctx, fdf);
            fz_drop_stream(ctx, stream);
            fz_drop_buffer(ctx, buffer);
        }
        fz_catch(ctx) {
            error = str::DupTemp(Str(fz_caught_message(ctx)));
            valid = false;
        }
    }
    if (!valid || !len(names)) {
        if (!len(error)) error = StrL("Expected named string values in FDF, XFDF, name/value CSV or a JSON object.");
        return false;
    }
    StrVec seen;
    Vec<pdf_obj*> fields;
    return ChangeForm(engine, "Import form data", error, [&](fz_context* ctx, EngineMupdf* e) {
        for (int i = 0; i < len(names); i++) {
            if (!len(names[i])) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Field name cannot be empty");
            if (memchr(names[i].s, 0, len(names[i])) || (len(values[i]) && memchr(values[i].s, 0, len(values[i]))))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Field names and values cannot contain NUL characters");
            if (seen.Contains(names[i])) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Duplicate field name");
            seen.Append(names[i]);
            pdf_obj* field = pdf_lookup_field(ctx, FormRoots(ctx, e), CStrTemp(names[i]));
            if (!field) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Unknown field: %s", CStrTemp(names[i]));
            int flags = pdf_field_flags(ctx, field);
            pdf_obj* ft = pdf_dict_get_inheritable(ctx, field, PDF_NAME(FT));
            if ((flags & PDF_FIELD_IS_READ_ONLY) || pdf_name_eq(ctx, ft, PDF_NAME(Sig)) ||
                (pdf_name_eq(ctx, ft, PDF_NAME(Btn)) && (flags & PDF_BTN_FIELD_IS_PUSHBUTTON)))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Read-only, signature or push-button field cannot be imported");
            int maxLength = pdf_dict_get_int(ctx, field, PDF_NAME(MaxLen));
            const char* valZ = len(values[i]) ? CStrTemp(values[i]) : "";
            if (maxLength > 0 && fz_utflen(valZ) > maxLength)
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Value exceeds maximum length");
            if (pdf_name_eq(ctx, ft, PDF_NAME(Ch))) {
                bool found = !len(values[i]);
                for (int j = 0; j < pdf_choice_field_option_count(ctx, field); j++)
                    if (str::Eq(values[i], Str(pdf_choice_field_option(ctx, field, 1, j)))) found = true;
                if (!found) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Value is not a choice option");
            }
            if (pdf_name_eq(ctx, ft, PDF_NAME(Btn)) && !str::Eq(values[i], StrL("Off"))) {
                bool found = false;
                pdf_obj* kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
                int n = pdf_array_len(ctx, kids);
                for (int j = 0; j < (n ? n : 1); j++) {
                    pdf_obj* widget = n ? pdf_array_get(ctx, kids, j) : field;
                    if (str::Eq(values[i], Str(pdf_to_name(ctx, pdf_button_field_on_state(ctx, widget))))) found = true;
                }
                if (!found) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Invalid button state");
            }
            VecAppend(fields, field);
        }
        for (int i = 0; i < len(fields); i++) {
            const char* valZ = len(values[i]) ? CStrTemp(values[i]) : "";
            if (!pdf_set_field_value(ctx, e->pdfdoc, fields[i], valZ, 0))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Field rejected imported value");
        }
    });
}

bool ClearFormData(EngineBase* engine, Str& error) {
    return ChangeForm(engine, "Clear form", error, [](fz_context* ctx, EngineMupdf* e) {
        Vec<pdf_obj*> fields;
        CollectFields(ctx, FormRoots(ctx, e), fields);
        for (pdf_obj* field : fields) {
            pdf_obj* ft = pdf_dict_get_inheritable(ctx, field, PDF_NAME(FT));
            pdf_set_field_value(ctx, e->pdfdoc, field, pdf_name_eq(ctx, ft, PDF_NAME(Btn)) ? "Off" : "", 1);
        }
    });
}

bool ResetFormData(EngineBase* engine, Str& error) {
    return ChangeForm(engine, "Reset form", error,
                      [](fz_context* ctx, EngineMupdf* e) { pdf_reset_form(ctx, e->pdfdoc, nullptr, 0); });
}

bool SaveFlattenedForm(EngineBase* engine, Str path, Str& error) {
    // Work on a saved snapshot so flattening leaves the open editable form intact.
    auto* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc || !EngineSupportsAnnotations(engine)) return false;
    TempStr snapshot = GetTempFilePathTemp(StrL("flatten-form"));
    if (!EngineMupdfSaveCopy(engine, snapshot)) {
        file::Delete(snapshot);
        return false;
    }
    EngineBase* copy = CreateEngineMupdfFromFile(snapshot, FileType::PDF, 96);
    if (!copy) {
        file::Delete(snapshot);
        return false;
    }
    bool ok = ChangeForm(copy, "Flatten form", error,
                         [](fz_context* ctx, EngineMupdf* e) { pdf_bake_document(ctx, e->pdfdoc, 0, 1); });
    if (ok) ok = EngineMupdfSaveUpdated(copy, path, {});
    if (!ok && !len(error)) error = StrL("Could not save the flattened form.");
    SafeEngineRelease(&copy);
    file::Delete(snapshot);
    return ok;
}
