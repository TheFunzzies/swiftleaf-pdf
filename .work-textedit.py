from pathlib import Path
p=Path('src/shared/EngineMupdf.cpp')
s=p.read_text()
a=s.index('// UTF-8 to a PDF string literal')
b=s.index('static pdf_obj* AddContentStream',a)
s=s[:a]+'''// Validate Unicode scalars before touching the document. Complex shaping is
// deliberately refused until the editor can preserve clusters and bidi order.
static int NextEditRune(Str text, int& pos) {
    int c = (unsigned char)text.s[pos++];
    if (c < 0x80) return c;
    int extra = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : -1;
    if (extra < 0 || pos + extra > len(text)) return -1;
    int minimum = extra == 1 ? 0x80 : extra == 2 ? 0x800 : 0x10000;
    c &= (1 << (6 - extra)) - 1;
    for (int n = 0; n < extra; n++) {
        int next = (unsigned char)text.s[pos++];
        if ((next & 0xc0) != 0x80) return -1;
        c = (c << 6) | (next & 0x3f);
    }
    return c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) ? -1 : c;
}

bool EngineMupdfCanEditText(Str text) {
    for (int pos = 0; pos < len(text);) {
        int c = NextEditRune(text, pos);
        if (c == '\\n' || (c == '\\r' && pos < len(text) && text.s[pos] == '\\n')) continue;
        // These ranges have independent, left-to-right glyphs. Decomposed
        // marks, bidi controls and joining scripts must not be drawn unshaped.
        bool simple = (c >= 0x20 && c <= 0x2ff) || (c >= 0x370 && c <= 0x52f) ||
                      (c >= 0x1e00 && c <= 0x1fff) || (c >= 0x2000 && c <= 0x206f) ||
                      (c >= 0x2100 && c <= 0x2bff) || (c >= 0x2e80 && c <= 0xa4cf) ||
                      (c >= 0xac00 && c <= 0xd7af) || (c >= 0xf900 && c <= 0xfaff) ||
                      (c >= 0xff01 && c <= 0xffef) || (c >= 0x1f300 && c <= 0x1faff) ||
                      (c >= 0x20000 && c <= 0x323af);
        if (!simple || (c >= 0x7f && c <= 0x9f) || c == 0xad ||
            (c >= 0x200b && c <= 0x200f) || (c >= 0x2028 && c <= 0x202e) ||
            (c >= 0x2060 && c <= 0x206f) || (c >= 0x302a && c <= 0x302f) ||
            c == 0x3099 || c == 0x309a) return false;
    }
    return true;
}

struct EditGlyph {
    fz_font* font;
    int rune;
    int gid;
    float advance;
};

static float EditAdvance(fz_context* ctx, fz_font* font, int rune, fz_font** actual) {
    int gid = fz_encode_character_with_fallback(ctx, font, rune, 0, FZ_LANG_UNSET, actual);
    if (gid <= 0 || !*actual || !pdf_font_writing_supported(ctx, *actual)) {
        fz_throw(ctx, FZ_ERROR_UNSUPPORTED, "No embeddable font contains U+%04X. Original text was kept.", rune);
    }
    return fz_advance_glyph(ctx, *actual, gid, 0);
}

''' + s[b:]
a=s.index('// Replace the text of `run`')
b=s.index('//--- Undo / redo', a)
old=s[a:b]
tail=old[old.index('    // the redaction may have taken'):]
tail=tail.replace('    e->modifiedAnnotations = true;\n    e->editGeneration++;', '    if (ok) {\n        e->modifiedAnnotations = true;\n        e->editGeneration++;\n    }')
s=s[:a]+'''// Prepare all glyphs, fonts and streams before removing any source text.
bool EngineMupdfReplaceText(EngineBase* engine, int pageNo, const PdfTextRun& run, Str newText,
                            Vec<Annotation*>& deletedOut, Str* errorOut) {
    VecReset(deletedOut);
    if (errorOut) *errorOut = {};
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc || pageNo < 1 || pageNo > e->pageCount || !EngineMupdfCanEditText(newText)) {
        if (errorOut) *errorOut = StrL("This text needs unsupported shaping or contains invalid characters. Original text was kept.");
        return false;
    }
    auto* ctx = e->Ctx();
    if (!run.supported || run.fontSize <= 0 || run.bbox.dx <= 0 || run.lineGap <= 0) {
        if (errorOut) *errorOut = StrL("This block has mixed styles, rotation, transparency or unsupported text rendering. Original text was kept.");
        return false;
    }
    pdf_document* doc = e->pdfdoc;
    ScopedEngineOperation op(engine, "Edit text");
    ScopedRecursiveMutex pagesScope(&e->pagesLock);
    ScopedMutex renderScope(&e->renderLock);
    FzPageInfo* pi = GetFzPageInfoLocked(e, e->LocationFromPageNo(pageNo), true, nullptr);
    if (!pi || !pi->page) return false;

    Vec<Annotation*> before = pi->annotations;
    bool ok = false;
    {
        ScopedRecursiveMutex docScope(&e->docLock);
        pdf_annot* redact = nullptr;
        fz_font* font = nullptr;
        pdf_obj* resources = nullptr;
        pdf_obj* form = nullptr;
        pdf_obj* pageRes = nullptr;
        pdf_obj* contents = nullptr;
        pdf_obj* fontRef = nullptr;
        pdf_obj* fontDict = nullptr;
        fz_buffer* buffer = nullptr;
        bool started = false;
        bool mutated = false;
        Vec<EditGlyph> glyphs;
        Vec<fz_font*> fonts;
        str::Builder content, cmap, unicode;
        fz_var(redact); fz_var(font); fz_var(resources); fz_var(form);
        fz_var(pageRes); fz_var(contents); fz_var(fontRef); fz_var(fontDict);
        fz_var(buffer); fz_var(started); fz_var(mutated); fz_var(ok);
        fz_try(ctx) {
            pdf_page* page = pdf_page_from_fz_page(ctx, pi->page);
            if (!page) fz_throw(ctx, FZ_ERROR_ARGUMENT, "Not a PDF page.");
            if (run.fontName[0]) {
                const char* name = run.fontName;
                if (strlen(name) > 7 && name[6] == '+') name += 7;
                font = fz_load_system_font(ctx, name, run.bold, run.italic, 0);
            }
            if (!font) font = fz_new_base14_font(ctx, Base14ForRun(run));
            for (int pos = 0; pos < len(newText);) {
                int rune = NextEditRune(newText, pos);
                if (rune == '\\r') continue;
                if (rune == '\\n') {
                    VecAppend(glyphs, EditGlyph{nullptr, rune, 0, 0});
                    continue;
                }
                fz_font* actual = nullptr;
                float advance = EditAdvance(ctx, font, rune, &actual) * run.fontSize;
                int gid = fz_encode_character(ctx, actual, rune);
                if (advance > run.bbox.dx && run.reflow)
                    fz_throw(ctx, FZ_ERROR_ARGUMENT, "A character is wider than this block. Original text was kept.");
                VecAppend(glyphs, EditGlyph{actual, rune, gid, advance});
                if (VecFind(fonts, actual) < 0) VecAppend(fonts, actual);
            }
            if (len(glyphs) > 65534)
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "This block is too long to edit (maximum 65534 characters).");

            // A nested journal operation makes failures after preflight reversible.
            pdf_begin_operation(ctx, doc, "Replace text block");
            started = true;
            resources = pdf_new_dict(ctx, doc, 1);
            pdf_obj* fontRes = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), len(fonts));
            for (int f = 0; f < len(fonts); f++) {
                fontRef = pdf_add_cid_font(ctx, doc, fonts[f]);
                fontDict = pdf_copy_dict(ctx, fontRef);
                cmap.Reset(); unicode.Reset();
                const char* header = "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\\n"
                    "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def\\n";
                cmap.Append(Str(header)); unicode.Append(Str(header));
                cmap.Append(StrL("/CMapName /SwiftLeafEncoding def /CMapType 1 def /WMode 0 def\\n"));
                unicode.Append(StrL("/CMapName /SwiftLeafUnicode def /CMapType 2 def\\n"));
                Str codespace = StrL("1 begincodespacerange <0000> <FFFF> endcodespacerange\\n");
                cmap.Append(codespace); unicode.Append(codespace);
                for (int g = 0; g < len(glyphs); g++) {
                    const EditGlyph& glyph = glyphs[g];
                    if (glyph.font != fonts[f]) continue;
                    cmap.Append(fmt("1 begincidchar <%04X> %d endcidchar\\n", g + 1, glyph.gid));
                    if (glyph.rune <= 0xffff) {
                        unicode.Append(fmt("1 beginbfchar <%04X> <%04X> endbfchar\\n", g + 1, glyph.rune));
                    } else {
                        int c = glyph.rune - 0x10000;
                        unicode.Append(fmt("1 beginbfchar <%04X> <%04X%04X> endbfchar\\n", g + 1,
                                           0xd800 + (c >> 10), 0xdc00 + (c & 1023)));
                    }
                }
                Str footer = StrL("endcmap CMapName currentdict /CMap defineresource pop end end\\n");
                cmap.Append(footer); unicode.Append(footer);
                pdf_dict_put_drop(ctx, fontDict, PDF_NAME(Encoding), AddContentStream(ctx, doc, ToStrTemp(cmap)));
                pdf_dict_put_drop(ctx, fontDict, PDF_NAME(ToUnicode), AddContentStream(ctx, doc, ToStrTemp(unicode)));
                pdf_dict_puts_drop(ctx, fontRes, CStrTemp(fmt("F%d", f)), pdf_add_object(ctx, doc, fontDict));
                pdf_drop_obj(ctx, fontDict); fontDict = nullptr;
                pdf_drop_obj(ctx, fontRef); fontRef = nullptr;
            }
            fz_rect media;
            fz_matrix ctm;
            pdf_page_transform(ctx, page, &media, &ctm);
            u8 cr, cg, cb;
            UnpackColor(run.color, cr, cg, cb);
            content.Append(fmt("q BT %.6g %.6g %.6g rg\\n", cr / 255.f, cg / 255.f, cb / 255.f));
            float x = 0, y = 0;
            for (int g = 0; g < len(glyphs); g++) {
                const EditGlyph& glyph = glyphs[g];
                if (glyph.rune == '\\n') { x = 0; y += run.lineGap; continue; }
                // Prefer a word boundary; long words and CJK can wrap between scalars.
                float wordWidth = glyph.advance;
                if (run.reflow && (g == 0 || glyphs[g - 1].rune == ' ')) {
                    for (int n = g + 1; n < len(glyphs) && glyphs[n].rune != ' ' && glyphs[n].rune != '\\n'; n++)
                        wordWidth += glyphs[n].advance;
                }
                if (run.reflow && x > 0 && (x + glyph.advance > run.bbox.dx + 0.01f ||
                    (wordWidth <= run.bbox.dx && x + wordWidth > run.bbox.dx + 0.01f))) {
                    x = 0; y += run.lineGap;
                }
                fz_matrix upright{1, 0, 0, -1, run.origin.x + x, run.origin.y + y};
                fz_matrix tm = fz_concat(upright, fz_invert_matrix(ctm));
                content.Append(fmt("/F%d %.6g Tf %.8g %.8g %.8g %.8g %.8g %.8g Tm <%04X> Tj\\n",
                                   VecFind(fonts, glyph.font), run.fontSize, tm.a, tm.b, tm.c, tm.d, tm.e, tm.f, g + 1));
                x += glyph.advance;
            }
            content.Append(StrL("ET Q\\n"));
            buffer = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)content.s, len(content));
            form = pdf_new_xobject(ctx, doc, media, fz_identity, resources, buffer);
            // Keep annotations intact: redaction deletes intersecting annotations.
            for (pdf_annot* a = pdf_first_annot(ctx, page); a; a = pdf_next_annot(ctx, a)) {
                fz_rect bounds = pdf_bound_annot(ctx, a);
                fz_rect box{run.bbox.x, run.bbox.y, run.bbox.x + run.bbox.dx, run.bbox.y + run.bbox.dy};
                if (RectsTouch(bounds, box))
                    fz_throw(ctx, FZ_ERROR_UNSUPPORTED, "An annotation overlaps this block. Original text was kept.");
            }
            float inset = std::min(run.bbox.dy * 0.2f, run.fontSize * 0.2f);
            fz_rect r{run.bbox.x, run.bbox.y + inset, run.bbox.x + run.bbox.dx, run.bbox.y + run.bbox.dy - inset};
            mutated = true;
            redact = pdf_create_annot(ctx, page, PDF_ANNOT_REDACT);
            pdf_set_annot_rect(ctx, redact, r);
            pdf_redact_options ropts{};
            ropts.image_method = PDF_REDACT_IMAGE_NONE;
            ropts.line_art = PDF_REDACT_LINE_ART_NONE;
            ropts.text = PDF_REDACT_TEXT_REMOVE;
            pdf_apply_redaction(ctx, redact, &ropts);

            pdf_obj* inherited = pdf_dict_get_inheritable(ctx, page->obj, PDF_NAME(Resources));
            pageRes = inherited ? pdf_copy_dict(ctx, inherited) : pdf_new_dict(ctx, doc, 1);
            pdf_obj* xo = pdf_dict_get(ctx, pageRes, PDF_NAME(XObject));
            pdf_dict_put_drop(ctx, pageRes, PDF_NAME(XObject), xo ? pdf_copy_dict(ctx, xo) : pdf_new_dict(ctx, doc, 1));
            xo = pdf_dict_get(ctx, pageRes, PDF_NAME(XObject));
            TempStr name;
            for (int n = 1;; n++) {
                name = fmt("SwiftLeafEdit%d", n);
                if (!pdf_dict_gets(ctx, xo, CStrTemp(name))) break;
            }
            pdf_dict_puts(ctx, xo, CStrTemp(name), form);
            pdf_dict_put(ctx, page->obj, PDF_NAME(Resources), pageRes);
            contents = pdf_new_array(ctx, doc, 4);
            pdf_array_push_drop(ctx, contents, AddContentStream(ctx, doc, StrL("q\\n")));
            pdf_obj* old = pdf_dict_get(ctx, page->obj, PDF_NAME(Contents));
            if (pdf_is_array(ctx, old)) {
                for (int n = 0; n < pdf_array_len(ctx, old); n++) pdf_array_push(ctx, contents, pdf_array_get(ctx, old, n));
            } else if (old) pdf_array_push(ctx, contents, old);
            pdf_array_push_drop(ctx, contents, AddContentStream(ctx, doc, fmt("Q\\nq /%s Do Q\\n", name)));
            pdf_dict_put(ctx, page->obj, PDF_NAME(Contents), contents);
            pdf_end_operation(ctx, doc);
            started = false;
            ok = true;
        }
        fz_always(ctx) {
            pdf_drop_obj(ctx, fontRef); pdf_drop_obj(ctx, fontDict);
            pdf_drop_obj(ctx, resources); pdf_drop_obj(ctx, form);
            pdf_drop_obj(ctx, pageRes); pdf_drop_obj(ctx, contents);
            fz_drop_buffer(ctx, buffer); fz_drop_font(ctx, font);
            pdf_drop_annot(ctx, redact);
        }
        fz_catch(ctx) {
            if (errorOut) *errorOut = str::DupTemp(Str(fz_caught_message(ctx)));
            logf("EngineMupdfReplaceText: page %d: %s\\n", pageNo, Str(fz_caught_message(ctx)));
            fz_report_error(ctx);
            if (started) {
                fz_try(ctx) { pdf_abandon_operation(ctx, doc); }
                fz_catch(ctx) { fz_report_error(ctx); }
            }
        }
        if (!ok && !mutated) return false;
    }

''' +tail+s[b:]
s=s.replace('(const unsigned char*)content.s, len(content)', '(const unsigned char*)ToStrTemp(content).s, len(content)')
p.write_text(s)
