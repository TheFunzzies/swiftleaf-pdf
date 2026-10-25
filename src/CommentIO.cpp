/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Pixmap.h"
#include "base/GdiPlusUtil.h"
#include "base/GuessFileType.h"
#include "base/Win.h"
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Gfx.h"
#include "gui/Layout.h"
#include "gui/PlatformFont.h"
#include "gui/win/WinGui.h"

extern "C" {
#include <mupdf/pdf.h>
#include <mupdf/fitz/xml.h>
}

#include "Settings.h"
#include "AppSettings.h"
#include "DocProperties.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "EngineMupdf.h"
#include "DisplayModel.h"
#include "Annotation.h"
#include "PdfCreator.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Translations.h"
#include "Notifications.h"
#include "CommentIO.h"

#include <time.h>

static void XmlEscape(str::Builder& b, Str text) {
    for (int i = 0; i < text.len; i++) {
        char c = text.s[i];
        switch (c) {
            case '&':
                b.Append(StrL("&amp;"));
                break;
            case '<':
                b.Append(StrL("&lt;"));
                break;
            case '>':
                b.Append(StrL("&gt;"));
                break;
            case '"':
                b.Append(StrL("&quot;"));
                break;
            case '\'':
                b.Append(StrL("&apos;"));
                break;
            default:
                b.AppendChar(c);
                break;
        }
    }
}

Str GetAnnotationState(Annotation* annot) {
    if (!AnnotationIsLive(annot)) return {};
    EngineMupdf* e = annot->engine;
    if (!e || !e->pdfdoc) return {};
    fz_context* ctx = e->Ctx();
    ScopedRecursiveMutex cs(&e->docLock);
    Str res{};
    fz_try(ctx) {
        pdf_obj* obj = pdf_annot_obj(ctx, annot->pdfannot);
        pdf_obj* s = pdf_dict_gets(ctx, obj, "State");
        if (s) {
            const char* str = pdf_to_text_string(ctx, s);
            if (str && *str) {
                res = str::DupTemp(Str(str));
            }
        }
    }
    fz_catch(ctx) {
        res = {};
    }
    return res;
}

Str GetAnnotationStateModel(Annotation* annot) {
    if (!AnnotationIsLive(annot)) return {};
    EngineMupdf* e = annot->engine;
    if (!e || !e->pdfdoc) return {};
    fz_context* ctx = e->Ctx();
    ScopedRecursiveMutex cs(&e->docLock);
    Str res{};
    fz_try(ctx) {
        pdf_obj* obj = pdf_annot_obj(ctx, annot->pdfannot);
        pdf_obj* s = pdf_dict_gets(ctx, obj, "StateModel");
        if (s) {
            const char* str = pdf_to_text_string(ctx, s);
            if (str && *str) {
                res = str::DupTemp(Str(str));
            }
        }
    }
    fz_catch(ctx) {
        res = {};
    }
    return res;
}

bool SetAnnotationState(Annotation* annot, Str state, Str stateModel) {
    if (!AnnotationIsLive(annot)) return false;
    EngineMupdf* e = annot->engine;
    if (!e || !e->pdfdoc) return false;
    ScopedEngineOperation op(e, "Set annotation state");
    fz_context* ctx = e->Ctx();
    ScopedRecursiveMutex cs(&e->docLock);
    bool ok = false;
    fz_try(ctx) {
        pdf_obj* obj = pdf_annot_obj(ctx, annot->pdfannot);
        pdf_dict_puts_drop(ctx, obj, "State", pdf_new_text_string(ctx, CStrTemp(state)));
        const char* sm = stateModel.len > 0 ? CStrTemp(stateModel) : kStateModelReview;
        pdf_dict_puts_drop(ctx, obj, "StateModel", pdf_new_text_string(ctx, sm));
        ok = true;
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
        ok = false;
    }
    if (ok) {
        MarkNotificationAsModified(e, annot);
    }
    return ok;
}

Annotation* CreateAnnotationReply(Annotation* parentAnnot, Str replyText, Str author) {
    if (!AnnotationIsLive(parentAnnot)) return nullptr;
    EngineMupdf* e = parentAnnot->engine;
    if (!e || !e->pdfdoc) return nullptr;

    AnnotCreateArgs args{AnnotationType::Text};
    args.content = replyText;
    RectF r = parentAnnot->bounds;
    PointF pos{r.x + 20.f, r.y + 20.f};

    Annotation* child = EngineMupdfCreateAnnotation(e, parentAnnot->pageNo, pos, &args);
    if (!child) return nullptr;

    ScopedEngineOperation op(e, "Reply to comment");
    fz_context* ctx = e->Ctx();
    ScopedRecursiveMutex cs(&e->docLock);
    fz_try(ctx) {
        pdf_obj* pobj = pdf_annot_obj(ctx, parentAnnot->pdfannot);
        pdf_obj* cobj = pdf_annot_obj(ctx, child->pdfannot);
        pdf_dict_put(ctx, cobj, PDF_NAME(IRT), pobj);
        pdf_dict_puts_drop(ctx, cobj, "RT", pdf_new_name(ctx, "R"));
        if (author.len > 0) {
            pdf_set_annot_author(ctx, child->pdfannot, CStrTemp(author));
        }
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }

    MarkNotificationAsModified(e, child);
    return child;
}

void GetAnnotationReplies(Annotation* parentAnnot, Vec<Annotation*>& outReplies) {
    if (!AnnotationIsLive(parentAnnot)) return;
    EngineMupdf* e = parentAnnot->engine;
    if (!e) return;
    int pid = AnnotationId(parentAnnot);

    FzPageInfo* pi = e->GetFzPageInfo(parentAnnot->pageNo, true);
    if (!pi) return;
    for (Annotation* a : pi->annotations) {
        if (!AnnotationIsLive(a) || a == parentAnnot) continue;
        int replyTo = AnnotationReplyTo(a);
        if (pid > 0 && replyTo == pid) {
            VecAppend(outReplies, a);
        } else {
            fz_context* ctx = e->Ctx();
            ScopedRecursiveMutex cs(&e->docLock);
            pdf_obj* irtObj = pdf_dict_get(ctx, pdf_annot_obj(ctx, a->pdfannot), PDF_NAME(IRT));
            if (irtObj && irtObj == pdf_annot_obj(ctx, parentAnnot->pdfannot)) {
                VecAppend(outReplies, a);
            }
        }
    }
}

bool ExportAnnotationsToXfdf(EngineBase* engine, const char* filePath, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    Vec<Annotation*> annots;
    EngineMupdfGetAnnotations(engine, annots);

    str::Builder b;
    b.Append(StrL("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    b.Append(StrL("<xfdf xmlns=\"http://ns.adobe.com/xfdf/\" xml:space=\"preserve\">\n"));
    b.Append(StrL("<annots>\n"));

    for (Annotation* a : annots) {
        if (!AnnotationIsLive(a)) continue;
        int page0 = a->pageNo - 1;
        RectF r = a->bounds;
        Str typeName = {};
        switch (a->type) {
            case AnnotationType::Text:
                typeName = StrL("text");
                break;
            case AnnotationType::FreeText:
                typeName = StrL("freetext");
                break;
            case AnnotationType::Line:
                typeName = StrL("line");
                break;
            case AnnotationType::Square:
                typeName = StrL("square");
                break;
            case AnnotationType::Circle:
                typeName = StrL("circle");
                break;
            case AnnotationType::Polygon:
                typeName = StrL("polygon");
                break;
            case AnnotationType::PolyLine:
                typeName = StrL("polyline");
                break;
            case AnnotationType::Highlight:
                typeName = StrL("highlight");
                break;
            case AnnotationType::Underline:
                typeName = StrL("underline");
                break;
            case AnnotationType::Squiggly:
                typeName = StrL("squiggly");
                break;
            case AnnotationType::StrikeOut:
                typeName = StrL("strikeout");
                break;
            case AnnotationType::Stamp:
                typeName = StrL("stamp");
                break;
            case AnnotationType::Caret:
                typeName = StrL("caret");
                break;
            case AnnotationType::Ink:
                typeName = StrL("ink");
                break;
            case AnnotationType::FileAttachment:
                typeName = StrL("fileattachment");
                break;
            case AnnotationType::Redact:
                typeName = StrL("redact");
                break;
            default:
                continue;
        }

        b.Append(
            fmt("  <%s page=\"%d\" rect=\"%.2f,%.2f,%.2f,%.2f\"", typeName, page0, r.x, r.y, r.x + r.dx, r.y + r.dy));

        Str author = Author(a);
        if (author.len > 0) {
            b.Append(StrL(" title=\""));
            XmlEscape(b, author);
            b.Append(StrL("\""));
        }

        time_t mdate = ModificationDate(a);
        if (mdate) {
            struct tm tmDate{};
            char dateText[32]{};
            if (localtime_s(&tmDate, &mdate) == 0) {
                strftime(dateText, sizeof(dateText), "D:%Y%m%d%H%M%S", &tmDate);
                b.Append(fmt(" date=\"%s\"", Str(dateText)));
            }
        }

        PdfColor col = GetColor(a);
        if (col != 0) {
            u8 cr, cg, cb, ca;
            UnpackPdfColor(col, cr, cg, cb, ca);
            b.Append(fmt(" color=\"#%02X%02X%02X\"", (int)cr, (int)cg, (int)cb));
        }

        PdfColor icol = InteriorColor(a);
        if (icol != 0) {
            u8 cr, cg, cb, ca;
            UnpackPdfColor(icol, cr, cg, cb, ca);
            b.Append(fmt(" interior-color=\"#%02X%02X%02X\"", (int)cr, (int)cg, (int)cb));
        }

        int bw = BorderWidth(a);
        if (bw > 0) {
            b.Append(fmt(" width=\"%d\"", bw));
        }

        Str state = GetAnnotationState(a);
        if (state.len > 0) {
            b.Append(fmt(" state=\"%s\"", state));
            Str sm = GetAnnotationStateModel(a);
            b.Append(fmt(" statemodel=\"%s\"", sm.len > 0 ? sm : StrL("Review")));
        }

        int irt = AnnotationReplyTo(a);
        if (irt > 0) {
            b.Append(fmt(" inreplyto=\"%d\"", irt));
        }
        int id = AnnotationId(a);
        if (id > 0) {
            b.Append(fmt(" name=\"%d\"", id));
        }

        if (a->type == AnnotationType::Line) {
            PointF p1{}, p2{};
            if (GetLinePoints(a, p1, p2)) {
                b.Append(fmt(" start=\"%.2f,%.2f\" end=\"%.2f,%.2f\"", p1.x, p1.y, p2.x, p2.y));
            }
        } else if (a->type == AnnotationType::Polygon || a->type == AnnotationType::PolyLine) {
            Vec<PointF> verts = GetVertices(a);
            if (len(verts) > 0) {
                b.Append(StrL(" vertices=\""));
                for (int vi = 0; vi < len(verts); vi++) {
                    if (vi > 0) b.Append(StrL(" "));
                    b.Append(fmt("%.2f,%.2f", verts[vi].x, verts[vi].y));
                }
                b.Append(StrL("\""));
            }
        }

        Str contents = Contents(a);
        if (contents.len > 0) {
            b.Append(StrL(">\n    <contents>"));
            XmlEscape(b, contents);
            b.Append(fmt("</contents>\n  </%s>\n", typeName));
        } else {
            b.Append(StrL(" />\n"));
        }
    }

    b.Append(StrL("</annots>\n"));
    b.Append(StrL("</xfdf>\n"));

    Str xml = b.TakeStr();
    bool ok = file::WriteFile(Str(filePath), xml);
    str::Free(xml);
    if (!ok) {
        errOut = StrL("Failed to write XFDF file");
        return false;
    }
    return true;
}

static fz_xml* FindXmlNodeRec(fz_xml* node, const char* targetTag) {
    while (node) {
        char* tag = fz_xml_tag(node);
        if (tag && str::EqI(Str(tag), Str(targetTag))) {
            return node;
        }
        fz_xml* down = fz_xml_down(node);
        if (down) {
            fz_xml* hit = FindXmlNodeRec(down, targetTag);
            if (hit) return hit;
        }
        node = fz_xml_next(node);
    }
    return nullptr;
}

bool ImportAnnotationsFromXfdf(EngineBase* engine, const char* filePath, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    Str data = file::ReadFile(Str(filePath));
    if (data.len == 0) {
        str::Free(data);
        errOut = StrL("Failed to read XFDF file");
        return false;
    }
    fz_context* ctx = e->Ctx();
    fz_xml* xml = nullptr;
    fz_try(ctx) {
        fz_buffer* buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*)data.s, (size_t)data.len);
        xml = fz_parse_xml(ctx, buf, 0);
        fz_drop_buffer(ctx, buf);
    }
    fz_catch(ctx) {
        xml = nullptr;
    }
    str::Free(data);
    if (!xml) {
        errOut = StrL("Failed to parse XFDF XML");
        return false;
    }

    fz_xml* annotsNode = FindXmlNodeRec(xml, "annots");
    fz_xml* node = annotsNode ? fz_xml_down(annotsNode) : fz_xml_down(xml);

    int count = 0;
    while (node) {
        char* tag = fz_xml_tag(node);
        if (tag) {
            AnnotationType atype = AnnotationType::Unknown;
            if (str::EqI(Str(tag), StrL("text")))
                atype = AnnotationType::Text;
            else if (str::EqI(Str(tag), StrL("freetext")))
                atype = AnnotationType::FreeText;
            else if (str::EqI(Str(tag), StrL("line")))
                atype = AnnotationType::Line;
            else if (str::EqI(Str(tag), StrL("square")))
                atype = AnnotationType::Square;
            else if (str::EqI(Str(tag), StrL("circle")))
                atype = AnnotationType::Circle;
            else if (str::EqI(Str(tag), StrL("polygon")))
                atype = AnnotationType::Polygon;
            else if (str::EqI(Str(tag), StrL("polyline")))
                atype = AnnotationType::PolyLine;
            else if (str::EqI(Str(tag), StrL("highlight")))
                atype = AnnotationType::Highlight;
            else if (str::EqI(Str(tag), StrL("underline")))
                atype = AnnotationType::Underline;
            else if (str::EqI(Str(tag), StrL("squiggly")))
                atype = AnnotationType::Squiggly;
            else if (str::EqI(Str(tag), StrL("strikeout")))
                atype = AnnotationType::StrikeOut;
            else if (str::EqI(Str(tag), StrL("stamp")))
                atype = AnnotationType::Stamp;
            else if (str::EqI(Str(tag), StrL("caret")))
                atype = AnnotationType::Caret;
            else if (str::EqI(Str(tag), StrL("ink")))
                atype = AnnotationType::Ink;
            else if (str::EqI(Str(tag), StrL("fileattachment")))
                atype = AnnotationType::FileAttachment;
            else if (str::EqI(Str(tag), StrL("redact")))
                atype = AnnotationType::Redact;

            if (atype != AnnotationType::Unknown) {
                const char* pageAtt = fz_xml_att(node, "page");
                int pageNo = pageAtt ? atoi(pageAtt) + 1 : 1;
                if (pageNo >= 1 && pageNo <= e->pageCount) {
                    AnnotCreateArgs args{atype};
                    const char* rectAtt = fz_xml_att(node, "rect");
                    float rx0 = 50, ry0 = 50, rx1 = 150, ry1 = 150;
                    if (rectAtt) {
                        sscanf_s(rectAtt, "%f,%f,%f,%f", &rx0, &ry0, &rx1, &ry1);
                        args.hasRect = true;
                        args.rect = {std::min(rx0, rx1), std::min(ry0, ry1), fabsf(rx1 - rx0), fabsf(ry1 - ry0)};
                    }
                    const char* titleAtt = fz_xml_att(node, "title");
                    const char* colorAtt = fz_xml_att(node, "color");
                    if (colorAtt && colorAtt[0] == '#') {
                        unsigned int c = 0;
                        if (sscanf_s(colorAtt + 1, "%x", &c) == 1) {
                            u8 cr = (c >> 16) & 0xFF;
                            u8 cg = (c >> 8) & 0xFF;
                            u8 cb = c & 0xFF;
                            args.col.parsedOk = true;
                            args.col.col = MkRgb(cr, cg, cb);
                            args.col.pdfCol = MkPdfColor(cr, cg, cb);
                        }
                    }
                    fz_xml* contentsNode = fz_xml_find_down(node, "contents");
                    char* contentsText = contentsNode ? fz_xml_text(fz_xml_down(contentsNode)) : nullptr;
                    if (!contentsText && contentsNode) contentsText = fz_xml_text(contentsNode);
                    if (contentsText) {
                        args.content = Str(contentsText);
                    }

                    const char* startAtt = fz_xml_att(node, "start");
                    const char* endAtt = fz_xml_att(node, "end");
                    if (startAtt && endAtt) {
                        float sx = 0, sy = 0, ex = 0, ey = 0;
                        sscanf_s(startAtt, "%f,%f", &sx, &sy);
                        sscanf_s(endAtt, "%f,%f", &ex, &ey);
                        args.hasLineEnd = true;
                        args.lineEnd = {ex, ey};
                    }

                    Vec<PointF> vertList;
                    const char* vertsAtt = fz_xml_att(node, "vertices");
                    if (vertsAtt) {
                        const char* p = vertsAtt;
                        while (*p) {
                            while (*p == ' ' || *p == '\t') p++;
                            if (!*p) break;
                            float vx = 0, vy = 0;
                            if (sscanf_s(p, "%f,%f", &vx, &vy) == 2) {
                                VecAppend(vertList, PointF{vx, vy});
                                while (*p && *p != ' ' && *p != '\t') p++;
                            } else {
                                break;
                            }
                        }
                        if (len(vertList) >= 2) {
                            args.polyLinePoints = &vertList;
                        }
                    }

                    PointF pos = args.hasRect ? PointF{args.rect.x, args.rect.y} : PointF{rx0, ry0};
                    Annotation* created = EngineMupdfCreateAnnotation(e, pageNo, pos, &args);
                    if (created) {
                        if (titleAtt) {
                            ScopedRecursiveMutex cs(&e->docLock);
                            fz_try(ctx) {
                                pdf_set_annot_author(ctx, created->pdfannot, titleAtt);
                            }
                            fz_catch(ctx) {}
                        }
                        const char* stateAtt = fz_xml_att(node, "state");
                        const char* modelAtt = fz_xml_att(node, "statemodel");
                        if (stateAtt) {
                            SetAnnotationState(created, Str(stateAtt), modelAtt ? Str(modelAtt) : StrL("Review"));
                        }
                        count++;
                    }
                }
            }
        }
        node = fz_xml_next(node);
    }
    fz_drop_xml(ctx, xml);
    MarkNotificationAsModified(e, nullptr);
    return count > 0;
}

bool ExportAnnotationsToFdf(EngineBase* engine, const char* filePath, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    Vec<Annotation*> annots;
    EngineMupdfGetAnnotations(engine, annots);

    str::Builder b;
    b.Append(StrL("%FDF-1.2\n%\xE2\xE3\xCF\xD3\n1 0 obj\n<< /FDF << /Annots [\n"));

    for (Annotation* a : annots) {
        if (!AnnotationIsLive(a)) continue;
        int page0 = a->pageNo - 1;
        RectF r = a->bounds;
        Str subtype = {};
        switch (a->type) {
            case AnnotationType::Text:
                subtype = StrL("Text");
                break;
            case AnnotationType::FreeText:
                subtype = StrL("FreeText");
                break;
            case AnnotationType::Line:
                subtype = StrL("Line");
                break;
            case AnnotationType::Square:
                subtype = StrL("Square");
                break;
            case AnnotationType::Circle:
                subtype = StrL("Circle");
                break;
            case AnnotationType::Polygon:
                subtype = StrL("Polygon");
                break;
            case AnnotationType::PolyLine:
                subtype = StrL("PolyLine");
                break;
            case AnnotationType::Highlight:
                subtype = StrL("Highlight");
                break;
            case AnnotationType::Underline:
                subtype = StrL("Underline");
                break;
            case AnnotationType::Squiggly:
                subtype = StrL("Squiggly");
                break;
            case AnnotationType::StrikeOut:
                subtype = StrL("StrikeOut");
                break;
            case AnnotationType::Stamp:
                subtype = StrL("Stamp");
                break;
            case AnnotationType::Caret:
                subtype = StrL("Caret");
                break;
            case AnnotationType::Ink:
                subtype = StrL("Ink");
                break;
            case AnnotationType::FileAttachment:
                subtype = StrL("FileAttachment");
                break;
            case AnnotationType::Redact:
                subtype = StrL("Redact");
                break;
            default:
                continue;
        }

        b.Append(StrL("<< /Type /Annot\n"));
        b.Append(fmt(" /Subtype /%s\n", subtype));
        b.Append(fmt(" /Page %d\n", page0));
        b.Append(fmt(" /Rect [ %.2f %.2f %.2f %.2f ]\n", r.x, r.y, r.x + r.dx, r.y + r.dy));

        Str author = Author(a);
        if (author.len > 0) {
            b.Append(fmt(" /T (%s)\n", author));
        }

        Str contents = Contents(a);
        if (contents.len > 0) {
            b.Append(StrL(" /Contents ("));
            for (int i = 0; i < contents.len; i++) {
                char c = contents.s[i];
                if (c == '(' || c == ')' || c == '\\') b.AppendChar('\\');
                b.AppendChar(c);
            }
            b.Append(StrL(")\n"));
        }

        PdfColor col = GetColor(a);
        if (col != 0) {
            u8 cr, cg, cb, ca;
            UnpackPdfColor(col, cr, cg, cb, ca);
            b.Append(fmt(" /C [ %.3f %.3f %.3f ]\n", (double)(cr / 255.f), (double)(cg / 255.f), (double)(cb / 255.f)));
        }

        Str state = GetAnnotationState(a);
        if (state.len > 0) {
            b.Append(fmt(" /State (%s)\n", state));
            Str sm = GetAnnotationStateModel(a);
            b.Append(fmt(" /StateModel (%s)\n", sm.len > 0 ? sm : StrL("Review")));
        }

        int irt = AnnotationReplyTo(a);
        if (irt > 0) {
            b.Append(fmt(" /IRT %d\n", irt));
        }

        b.Append(StrL(">>\n"));
    }

    b.Append(StrL("] >> >>\nendobj\ntrailer\n<< /Root 1 0 R >>\n%%EOF\n"));

    Str fdf = b.TakeStr();
    bool ok = file::WriteFile(Str(filePath), fdf);
    str::Free(fdf);
    if (!ok) {
        errOut = StrL("Failed to write FDF file");
        return false;
    }
    return true;
}

bool ImportAnnotationsFromFdf(EngineBase* engine, const char* filePath, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    Str data = file::ReadFile(Str(filePath));
    if (data.len == 0) {
        str::Free(data);
        errOut = StrL("Failed to read FDF file");
        return false;
    }

    const char* p = data.s;
    const char* end = data.s + data.len;
    int count = 0;

    while (p < end) {
        const char* annotStart = strstr(p, "<<");
        if (!annotStart) break;
        const char* annotEnd = strstr(annotStart, ">>");
        if (!annotEnd) break;

        Str block(annotStart, (int)(annotEnd - annotStart + 2));

        if (str::Contains(block, StrL("/Subtype"))) {
            AnnotationType atype = AnnotationType::Text;
            if (str::Contains(block, StrL("/Subtype /FreeText")))
                atype = AnnotationType::FreeText;
            else if (str::Contains(block, StrL("/Subtype /Line")))
                atype = AnnotationType::Line;
            else if (str::Contains(block, StrL("/Subtype /Square")))
                atype = AnnotationType::Square;
            else if (str::Contains(block, StrL("/Subtype /Circle")))
                atype = AnnotationType::Circle;
            else if (str::Contains(block, StrL("/Subtype /Polygon")))
                atype = AnnotationType::Polygon;
            else if (str::Contains(block, StrL("/Subtype /PolyLine")))
                atype = AnnotationType::PolyLine;
            else if (str::Contains(block, StrL("/Subtype /Highlight")))
                atype = AnnotationType::Highlight;
            else if (str::Contains(block, StrL("/Subtype /Underline")))
                atype = AnnotationType::Underline;
            else if (str::Contains(block, StrL("/Subtype /Squiggly")))
                atype = AnnotationType::Squiggly;
            else if (str::Contains(block, StrL("/Subtype /StrikeOut")))
                atype = AnnotationType::StrikeOut;
            else if (str::Contains(block, StrL("/Subtype /Stamp")))
                atype = AnnotationType::Stamp;
            else if (str::Contains(block, StrL("/Subtype /Caret")))
                atype = AnnotationType::Caret;
            else if (str::Contains(block, StrL("/Subtype /Ink")))
                atype = AnnotationType::Ink;

            int pageNo = 1;
            const char* pageStr = strstr(block.s, "/Page ");
            if (pageStr) {
                pageNo = atoi(pageStr + 6) + 1;
            }
            if (pageNo < 1 || pageNo > e->pageCount) pageNo = 1;

            AnnotCreateArgs args{atype};
            const char* rectStr = strstr(block.s, "/Rect [");
            if (rectStr) {
                float rx0 = 50, ry0 = 50, rx1 = 150, ry1 = 150;
                sscanf_s(rectStr + 7, "%f %f %f %f", &rx0, &ry0, &rx1, &ry1);
                args.hasRect = true;
                args.rect = {std::min(rx0, rx1), std::min(ry0, ry1), fabsf(rx1 - rx0), fabsf(ry1 - ry0)};
            }

            const char* contentsStr = strstr(block.s, "/Contents (");
            if (contentsStr) {
                const char* cstart = contentsStr + 11;
                const char* cend = strchr(cstart, ')');
                if (cend) {
                    args.content = Str(cstart, (int)(cend - cstart));
                }
            }

            PointF pos = args.hasRect ? PointF{args.rect.x, args.rect.y} : PointF{50.f, 50.f};
            Annotation* created = EngineMupdfCreateAnnotation(e, pageNo, pos, &args);
            if (created) {
                const char* stateStr = strstr(block.s, "/State (");
                if (stateStr) {
                    const char* sstart = stateStr + 8;
                    const char* send = strchr(sstart, ')');
                    if (send) {
                        Str state(sstart, (int)(send - sstart));
                        SetAnnotationState(created, state, StrL("Review"));
                    }
                }
                count++;
            }
        }
        p = annotEnd + 2;
    }

    str::Free(data);
    MarkNotificationAsModified(e, nullptr);
    return count > 0;
}

static bool ExportCommentSummaryTxt(EngineBase* engine, const char* filePath, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    Vec<Annotation*> annots;
    EngineMupdfGetAnnotations(engine, annots);

    str::Builder b;
    b.Append(fmt("Comments Summary - SwiftLeaf PDF\nTotal Comments: %d\n\n", len(annots)));

    for (int p = 1; p <= e->pageCount; p++) {
        Vec<Annotation*> pageAnnots;
        for (auto* a : annots) {
            if (a->pageNo == p) VecAppend(pageAnnots, a);
        }
        if (len(pageAnnots) == 0) continue;

        b.Append(
            fmt("--- Page %d (%d comment%s) ---\n", p, len(pageAnnots), len(pageAnnots) == 1 ? StrL("") : StrL("s")));
        int idx = 1;
        for (auto* a : pageAnnots) {
            Str author = Author(a);
            Str typeName = AnnotationReadableNameTemp(a->type);
            Str state = GetAnnotationState(a);
            time_t mdate = ModificationDate(a);
            char dateBuf[32]{};
            if (mdate) {
                struct tm tmDate{};
                if (localtime_s(&tmDate, &mdate) == 0) strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M", &tmDate);
            }

            b.Append(fmt("  [%d] %s", idx++, typeName));
            if (author.len > 0) b.Append(fmt(" by %s", author));
            if (dateBuf[0]) b.Append(fmt(" on %s", Str(dateBuf)));
            if (state.len > 0) b.Append(fmt(" [Status: %s]", state));
            b.Append(StrL("\n"));

            Str contents = Contents(a);
            if (contents.len > 0) {
                b.Append(fmt("      \"%s\"\n", contents));
            }
        }
        b.Append(StrL("\n"));
    }

    Str res = b.TakeStr();
    bool ok = file::WriteFile(Str(filePath), res);
    str::Free(res);
    return ok;
}

static bool ExportCommentSummaryCsv(EngineBase* engine, const char* filePath, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    Vec<Annotation*> annots;
    EngineMupdfGetAnnotations(engine, annots);

    str::Builder b;
    b.Append(StrL("\"Page\",\"Type\",\"Author\",\"Date\",\"Status\",\"StateModel\",\"InReplyTo\",\"Content\"\n"));

    for (auto* a : annots) {
        int page = a->pageNo;
        Str typeName = AnnotationReadableNameTemp(a->type);
        Str author = Author(a);
        Str state = GetAnnotationState(a);
        Str model = GetAnnotationStateModel(a);
        int irt = AnnotationReplyTo(a);
        Str contents = Contents(a);

        time_t mdate = ModificationDate(a);
        char dateBuf[32]{};
        if (mdate) {
            struct tm tmDate{};
            if (localtime_s(&tmDate, &mdate) == 0) strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M", &tmDate);
        }

        b.Append(fmt("%d,\"%s\",\"", page, typeName));
        for (int i = 0; i < author.len; i++) {
            char c = author.s[i];
            if (c == '"')
                b.Append(StrL("\"\""));
            else
                b.AppendChar(c);
        }
        b.Append(fmt("\",\"%s\",\"", Str(dateBuf)));
        for (int i = 0; i < state.len; i++) {
            char c = state.s[i];
            if (c == '"')
                b.Append(StrL("\"\""));
            else
                b.AppendChar(c);
        }
        b.Append(StrL("\",\""));
        for (int i = 0; i < model.len; i++) {
            char c = model.s[i];
            if (c == '"')
                b.Append(StrL("\"\""));
            else
                b.AppendChar(c);
        }
        b.Append(fmt("\",\"%d\",\"", irt));
        for (int i = 0; i < contents.len; i++) {
            char c = contents.s[i];
            if (c == '"')
                b.Append(StrL("\"\""));
            else
                b.AppendChar(c);
        }
        b.Append(StrL("\"\n"));
    }

    Str res = b.TakeStr();
    bool ok = file::WriteFile(Str(filePath), res);
    str::Free(res);
    return ok;
}

static bool ExportCommentSummaryPdf(EngineBase* engine, const char* filePath, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    Vec<Annotation*> annots;
    EngineMupdfGetAnnotations(engine, annots);

    int width = 1275;
    int height = 1650;
    Gdiplus::Bitmap bmp(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics gs(&bmp);
    gs.Clear(Gdiplus::Color(255, 255, 255, 255));
    gs.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::Font titleFont(L"Arial", 22, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font headerFont(L"Arial", 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font regularFont(L"Arial", 12, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush blackBrush(Gdiplus::Color(255, 20, 20, 20));
    Gdiplus::SolidBrush grayBrush(Gdiplus::Color(255, 100, 100, 100));

    float y = 50.0f;
    gs.DrawString(L"SwiftLeaf PDF - Comments Summary", -1, &titleFont, Gdiplus::PointF(50.0f, y), &blackBrush);
    y += 40.0f;

    TempStr totalStr = fmt("Total comments: %d", len(annots));
    WStr wtotal = ToWStrTemp(totalStr);
    gs.DrawString(wtotal.s, -1, &headerFont, Gdiplus::PointF(50.0f, y), &grayBrush);
    y += 35.0f;

    for (int p = 1; p <= e->pageCount && y < height - 80; p++) {
        Vec<Annotation*> pageAnnots;
        for (auto* a : annots) {
            if (a->pageNo == p) VecAppend(pageAnnots, a);
        }
        if (len(pageAnnots) == 0) continue;

        TempStr pStr = fmt("Page %d (%d)", p, len(pageAnnots));
        WStr wpStr = ToWStrTemp(pStr);
        gs.DrawString(wpStr.s, -1, &headerFont, Gdiplus::PointF(50.0f, y), &blackBrush);
        y += 25.0f;

        for (auto* a : pageAnnots) {
            if (y >= height - 80) break;
            Str author = Author(a);
            Str typeName = AnnotationReadableNameTemp(a->type);
            Str state = GetAnnotationState(a);
            Str contents = Contents(a);

            TempStr line = fmt("[%s] %s: %s", typeName, author.len > 0 ? author : StrL("Unknown"),
                               contents.len > 0 ? contents : StrL(""));
            if (state.len > 0) {
                line = fmt("%s (%s)", line, state);
            }
            WStr wline = ToWStrTemp(line);
            gs.DrawString(wline.s, -1, &regularFont, Gdiplus::PointF(70.0f, y), &blackBrush);
            y += 20.0f;
        }
        y += 15.0f;
    }

    PdfCreator creator;
    if (!creator.AddPageFromGdiplusBitmap(&bmp, 150)) {
        errOut = StrL("Failed to create summary PDF page");
        return false;
    }
    if (!creator.SaveToFile(Str(filePath))) {
        errOut = StrL("Failed to save summary PDF");
        return false;
    }
    return true;
}

bool ExportCommentSummary(EngineBase* engine, const char* filePath, const char* format, Str& errOut) {
    if (str::EqI(Str(format), StrL("csv")) || str::EndsWithI(Str(filePath), StrL(".csv"))) {
        return ExportCommentSummaryCsv(engine, filePath, errOut);
    }
    if (str::EqI(Str(format), StrL("txt")) || str::EndsWithI(Str(filePath), StrL(".txt"))) {
        return ExportCommentSummaryTxt(engine, filePath, errOut);
    }
    return ExportCommentSummaryPdf(engine, filePath, errOut);
}

bool FlattenAnnotations(EngineBase* engine, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    fz_context* ctx = e->Ctx();
    ScopedRecursiveMutex cs(&e->docLock);
    bool ok = false;
    fz_try(ctx) {
        pdf_bake_document(ctx, e->pdfdoc, 1, 0);
        ok = true;
    }
    fz_catch(ctx) {
        errOut = str::DupTemp(Str(fz_caught_message(ctx)));
        ok = false;
    }
    if (ok) {
        for (int i = 1; i <= e->pageCount; i++) {
            FzPageInfo* pi = e->GetFzPageInfo(i, false);
            if (pi) {
                for (auto* a : pi->annotations) {
                    delete a;
                }
                VecClear(pi->annotations);
                pi->annotsLoaded = false;
                if (pi->page) {
                    fz_drop_page(ctx, pi->page);
                    pi->page = nullptr;
                }
                if (pi->displayList) {
                    fz_drop_display_list(ctx, pi->displayList);
                    pi->displayList = nullptr;
                }
                pi->elementsNeedRebuilding = true;
            }
        }
        MarkNotificationAsModified(e, nullptr);
    }
    return ok;
}

void OnExportXfdf(MainWindow* win) {
    if (!win || !win->CurrentTab()) return;
    EngineBase* engine = win->CurrentTab()->GetEngine();
    if (!engine) return;

    WCHAR path[MAX_PATH + 1]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    ofn.lpstrFilter = L"XFDF Files (*.xfdf)\0*.xfdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.lpstrDefExt = L"xfdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetSaveFileNameW(&ofn)) {
        Str err;
        TempStr utf8Path = ToUtf8Temp(path);
        if (ExportAnnotationsToXfdf(engine, utf8Path.s, err)) {
            ShowTemporaryNotification(win->hwndCanvas, Tr("Comments exported to XFDF successfully."));
        } else {
            ShowWarningNotification(win->hwndCanvas, err, kNotif5SecsTimeOut);
        }
    }
}

void OnImportXfdf(MainWindow* win) {
    if (!win || !win->CurrentTab()) return;
    EngineBase* engine = win->CurrentTab()->GetEngine();
    if (!engine) return;

    WCHAR path[MAX_PATH + 1]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    ofn.lpstrFilter = L"XFDF Files (*.xfdf)\0*.xfdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        Str err;
        TempStr utf8Path = ToUtf8Temp(path);
        if (ImportAnnotationsFromXfdf(engine, utf8Path.s, err)) {
            HwndRepaintNow(win->hwndCanvas);
            ShowTemporaryNotification(win->hwndCanvas, Tr("Comments imported from XFDF."));
        } else {
            ShowWarningNotification(win->hwndCanvas, err, kNotif5SecsTimeOut);
        }
    }
}

void OnExportFdf(MainWindow* win) {
    if (!win || !win->CurrentTab()) return;
    EngineBase* engine = win->CurrentTab()->GetEngine();
    if (!engine) return;

    WCHAR path[MAX_PATH + 1]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    ofn.lpstrFilter = L"FDF Files (*.fdf)\0*.fdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.lpstrDefExt = L"fdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetSaveFileNameW(&ofn)) {
        Str err;
        TempStr utf8Path = ToUtf8Temp(path);
        if (ExportAnnotationsToFdf(engine, utf8Path.s, err)) {
            ShowTemporaryNotification(win->hwndCanvas, Tr("Comments exported to FDF successfully."));
        } else {
            ShowWarningNotification(win->hwndCanvas, err, kNotif5SecsTimeOut);
        }
    }
}

void OnImportFdf(MainWindow* win) {
    if (!win || !win->CurrentTab()) return;
    EngineBase* engine = win->CurrentTab()->GetEngine();
    if (!engine) return;

    WCHAR path[MAX_PATH + 1]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    ofn.lpstrFilter = L"FDF Files (*.fdf)\0*.fdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        Str err;
        TempStr utf8Path = ToUtf8Temp(path);
        if (ImportAnnotationsFromFdf(engine, utf8Path.s, err)) {
            HwndRepaintNow(win->hwndCanvas);
            ShowTemporaryNotification(win->hwndCanvas, Tr("Comments imported from FDF."));
        } else {
            ShowWarningNotification(win->hwndCanvas, err, kNotif5SecsTimeOut);
        }
    }
}

void OnCommentSummary(MainWindow* win) {
    if (!win || !win->CurrentTab()) return;
    EngineBase* engine = win->CurrentTab()->GetEngine();
    if (!engine) return;

    WCHAR path[MAX_PATH + 1]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    ofn.lpstrFilter = L"PDF Summary (*.pdf)\0*.pdf\0Text Summary (*.txt)\0*.txt\0CSV Summary (*.csv)\0*.csv\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetSaveFileNameW(&ofn)) {
        Str err;
        TempStr utf8Path = ToUtf8Temp(path);
        bool ok = false;
        if (str::EndsWithI(utf8Path, StrL(".csv"))) {
            ok = ExportCommentSummaryCsv(engine, utf8Path.s, err);
        } else if (str::EndsWithI(utf8Path, StrL(".txt"))) {
            ok = ExportCommentSummaryTxt(engine, utf8Path.s, err);
        } else {
            ok = ExportCommentSummaryPdf(engine, utf8Path.s, err);
        }
        if (ok) {
            ShowTemporaryNotification(win->hwndCanvas, Tr("Comment summary generated."));
        } else {
            ShowWarningNotification(win->hwndCanvas, err, kNotif5SecsTimeOut);
        }
    }
}

void OnFlattenComments(MainWindow* win) {
    if (!win || !win->CurrentTab()) return;
    EngineBase* engine = win->CurrentTab()->GetEngine();
    if (!engine) return;

    Str err;
    if (FlattenAnnotations(engine, err)) {
        HwndRepaintNow(win->hwndCanvas);
        ShowTemporaryNotification(win->hwndCanvas, Tr("All comments flattened into document."));
    } else {
        ShowWarningNotification(win->hwndCanvas, err, kNotif5SecsTimeOut);
    }
}

void OnReplyToComment(MainWindow* win, Annotation* annot) {
    if (!win || !win->CurrentTab()) return;
    WindowTab* tab = win->CurrentTab();
    Annotation* sel = annot ? annot : tab->selectedAnnotation;
    if (!sel) {
        ShowWarningNotification(win->hwndCanvas, Tr("Please select an annotation to reply to."), kNotif5SecsTimeOut);
        return;
    }
    Str author = StrL("User");
    Annotation* reply = CreateAnnotationReply(sel, StrL("Reply"), author);
    if (reply) {
        tab->selectedAnnotation = reply;
        HwndRepaintNow(win->hwndCanvas);
        ShowTemporaryNotification(win->hwndCanvas, Tr("Reply added."));
    }
}

void OnSetCommentStatus(MainWindow* win, Annotation* annot, const char* status) {
    if (!win || !win->CurrentTab()) return;
    WindowTab* tab = win->CurrentTab();
    Annotation* sel = annot ? annot : tab->selectedAnnotation;
    if (!sel) {
        ShowWarningNotification(win->hwndCanvas, Tr("Please select an annotation to update status."),
                                kNotif5SecsTimeOut);
        return;
    }
    if (SetAnnotationState(sel, Str(status), StrL(kStateModelReview))) {
        HwndRepaintNow(win->hwndCanvas);
        ShowTemporaryNotification(win->hwndCanvas, fmt(Tr("Comment marked as %s.").s, Str(status)));
    }
}
