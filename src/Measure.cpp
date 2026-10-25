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
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Translations.h"
#include "Notifications.h"
#include "Measure.h"
#include "Commands.h"

#include <math.h>
#include <time.h>

static MeasureScale gCurrentScale;

MeasureScale GetPageScale(EngineBase*, int) {
    return gCurrentScale;
}

void SetPageScale(EngineBase*, int, const MeasureScale& scale) {
    gCurrentScale = scale;
}

float ComputeDistance(PointF p1, PointF p2, const MeasureScale& scale) {
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float distPts = sqrtf(dx * dx + dy * dy);
    float denom = scale.pointsPerUnit > 0.0001f ? scale.pointsPerUnit : 72.0f;
    return distPts / denom;
}

float ComputePerimeter(const Vec<PointF>& pts, const MeasureScale& scale) {
    int n = len(pts);
    if (n < 2) return 0.0f;
    float totalPts = 0.0f;
    for (int i = 0; i < n - 1; i++) {
        float dx = pts[i + 1].x - pts[i].x;
        float dy = pts[i + 1].y - pts[i].y;
        totalPts += sqrtf(dx * dx + dy * dy);
    }
    float denom = scale.pointsPerUnit > 0.0001f ? scale.pointsPerUnit : 72.0f;
    return totalPts / denom;
}

float ComputeArea(const Vec<PointF>& pts, const MeasureScale& scale) {
    int n = len(pts);
    if (n < 3) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        sum += (pts[i].x * pts[j].y - pts[j].x * pts[i].y);
    }
    float areaPts = fabsf(sum) * 0.5f;
    float denom = scale.pointsPerUnit > 0.0001f ? scale.pointsPerUnit : 72.0f;
    return areaPts / (denom * denom);
}

TempStr FormatDistance(float val, const MeasureScale& scale) {
    return fmt("%.2f %s", val, Str(scale.unit[0] ? scale.unit : "in"));
}

TempStr FormatPerimeter(float val, const MeasureScale& scale) {
    return fmt("%.2f %s", val, Str(scale.unit[0] ? scale.unit : "in"));
}

TempStr FormatArea(float val, const MeasureScale& scale) {
    return fmt("%.2f sq %s", val, Str(scale.unit[0] ? scale.unit : "in"));
}

void FormatDistance(float val, const MeasureScale& scale, str::Builder& out) {
    out.Append(FormatDistance(val, scale));
}

void FormatPerimeter(float val, const MeasureScale& scale, str::Builder& out) {
    out.Append(FormatPerimeter(val, scale));
}

void FormatArea(float val, const MeasureScale& scale, str::Builder& out) {
    out.Append(FormatArea(val, scale));
}

PointF SnapToPageContent(EngineBase* engine, int pageNo, PointF pt, float radiusPt, bool* outSnapped) {
    if (outSnapped) *outSnapped = false;
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) return pt;
    FzPageInfo* pi = e->GetFzPageInfo(pageNo, true);
    if (!pi) return pt;

    float bestDistSq = radiusPt * radiusPt;
    PointF bestPt = pt;
    bool found = false;

    auto checkPt = [&](PointF cand) {
        float dx = cand.x - pt.x;
        float dy = cand.y - pt.y;
        float dsq = dx * dx + dy * dy;
        if (dsq < bestDistSq) {
            bestDistSq = dsq;
            bestPt = cand;
            found = true;
        }
    };

    for (Annotation* a : pi->annotations) {
        if (!AnnotationIsLive(a)) continue;
        if (a->type == AnnotationType::Line) {
            PointF p1{}, p2{};
            if (GetLinePoints(a, p1, p2)) {
                checkPt(p1);
                checkPt(p2);
            }
        } else if (a->type == AnnotationType::PolyLine || a->type == AnnotationType::Polygon) {
            Vec<PointF> verts = GetVertices(a);
            for (PointF v : verts) {
                checkPt(v);
            }
        } else {
            RectF r = a->bounds;
            checkPt(PointF{r.x, r.y});
            checkPt(PointF{r.x + r.dx, r.y});
            checkPt(PointF{r.x, r.y + r.dy});
            checkPt(PointF{r.x + r.dx, r.y + r.dy});
        }
    }

    if (found && outSnapped) *outSnapped = true;
    return bestPt;
}

bool SnapToPageContent(EngineBase* engine, int pageNo, PointF pt, PointF& outSnapped, float snapRadiusPt) {
    bool snapped = false;
    PointF res = SnapToPageContent(engine, pageNo, pt, snapRadiusPt, &snapped);
    if (snapped) {
        outSnapped = res;
        return true;
    }
    return false;
}

bool AttachMeasureDictToAnnotation(EngineBase* engine, Annotation* annot, const MeasureScale& scale, const char* measureType) {
    if (!annot || !annot->pdfannot) return false;
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) return false;
    ScopedEngineOperation op(e, "Attach measure");
    fz_context* ctx = e->Ctx();
    ScopedRecursiveMutex cs(&e->docLock);
    fz_try(ctx) {
        pdf_obj* obj = pdf_annot_obj(ctx, annot->pdfannot);

        if (annot->type == AnnotationType::Line) {
            pdf_dict_puts_drop(ctx, obj, "IT", pdf_new_name(ctx, "LineDimension"));
            pdf_set_annot_line_start_style(ctx, annot->pdfannot, PDF_ANNOT_LE_CLOSED_ARROW);
            pdf_set_annot_line_end_style(ctx, annot->pdfannot, PDF_ANNOT_LE_CLOSED_ARROW);
        } else if (annot->type == AnnotationType::PolyLine) {
            pdf_dict_puts_drop(ctx, obj, "IT", pdf_new_name(ctx, "PolyLineDimension"));
        } else if (annot->type == AnnotationType::Polygon) {
            pdf_dict_puts_drop(ctx, obj, "IT", pdf_new_name(ctx, "PolygonDimension"));
        }

        pdf_obj* m = pdf_new_dict(ctx, e->pdfdoc, 4);
        pdf_dict_puts_drop(ctx, m, "Type", pdf_new_name(ctx, "Measure"));
        pdf_dict_puts_drop(ctx, m, "Subtype", pdf_new_name(ctx, "RL"));
        pdf_dict_put_text_string(ctx, m, PDF_NAME(R), scale.label[0] ? scale.label : "1 in = 1 in");

        pdf_obj* xarr = pdf_new_array(ctx, e->pdfdoc, 1);
        pdf_obj* xdict = pdf_new_dict(ctx, e->pdfdoc, 4);
        pdf_dict_puts_drop(ctx, xdict, "Type", pdf_new_name(ctx, "NumberFormat"));
        pdf_dict_put_text_string(ctx, xdict, PDF_NAME(U), scale.unit[0] ? scale.unit : "in");
        pdf_dict_put_real(ctx, xdict, PDF_NAME(C), 1.0f);
        pdf_dict_put_real(ctx, xdict, PDF_NAME(D), 1.0f);
        pdf_array_push_drop(ctx, xarr, xdict);
        pdf_dict_puts_drop(ctx, m, "X", xarr);

        pdf_dict_puts_drop(ctx, obj, "Measure", m);
    }
    fz_catch(ctx) {
        return false;
    }
    MarkNotificationAsModified(e, annot);
    return true;
}

Annotation* CreateMeasureAnnotation(EngineBase* engine, int pageNo, int cmdId, const Vec<PointF>& pts, const MeasureScale& scale) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) return nullptr;

    AnnotationType atype = AnnotationType::Line;
    TempStr measurementText{};
    if (cmdId == CmdMeasureArea) {
        atype = AnnotationType::Polygon;
        float area = ComputeArea(pts, scale);
        measurementText = FormatArea(area, scale);
    } else if (cmdId == CmdMeasurePerimeter) {
        atype = AnnotationType::PolyLine;
        float perim = ComputePerimeter(pts, scale);
        measurementText = FormatPerimeter(perim, scale);
    } else {
        atype = AnnotationType::Line;
        if (len(pts) >= 2) {
            float dist = ComputeDistance(pts[0], pts[1], scale);
            measurementText = FormatDistance(dist, scale);
        }
    }

    AnnotCreateArgs args{atype};
    args.content = Str(measurementText);
    if (atype == AnnotationType::Line && len(pts) >= 2) {
        args.hasLineEnd = true;
        args.lineEnd = pts[1];
    } else if ((atype == AnnotationType::PolyLine || atype == AnnotationType::Polygon) && len(pts) >= 2) {
        args.polyLinePoints = &pts;
    }

    PointF pos = len(pts) > 0 ? pts[0] : PointF{72.f, 72.f};
    Annotation* annot = EngineMupdfCreateAnnotation(engine, pageNo, pos, &args);
    if (!annot) return nullptr;

    AttachMeasureDictToAnnotation(engine, annot, scale, "Distance");
    return annot;
}

bool ExportMeasurementsToCsv(EngineBase* engine, const char* filePath, Str& errOut) {
    EngineMupdf* e = AsEngineMupdf(engine);
    if (!e || !e->pdfdoc) {
        errOut = StrL("Not a PDF document");
        return false;
    }
    Vec<Annotation*> annots;
    EngineMupdfGetAnnotations(engine, annots);

    str::Builder b;
    b.Append(StrL("\"Page\",\"Type\",\"Measurement\",\"Unit\",\"Scale\",\"Author\",\"Date\",\"Label\"\n"));

    fz_context* ctx = e->Ctx();
    for (auto* a : annots) {
        if (!AnnotationIsLive(a)) continue;

        bool isMeasure = false;
        Str scaleLabel = Str(gCurrentScale.label);
        Str unitName = Str(gCurrentScale.unit);

        ScopedRecursiveMutex cs(&e->docLock);
        fz_try(ctx) {
            pdf_obj* obj = pdf_annot_obj(ctx, a->pdfannot);
            pdf_obj* it = pdf_dict_get(ctx, obj, PDF_NAME(IT));
            if (pdf_name_eq(ctx, it, pdf_new_name(ctx, "LineDimension")) ||
                pdf_name_eq(ctx, it, pdf_new_name(ctx, "PolyLineDimension")) ||
                pdf_name_eq(ctx, it, pdf_new_name(ctx, "PolygonDimension"))) {
                isMeasure = true;
            }
            pdf_obj* m = pdf_dict_get(ctx, obj, pdf_new_name(ctx, "Measure"));
            if (m) {
                isMeasure = true;
                const char* r = pdf_dict_get_text_string(ctx, m, PDF_NAME(R));
                if (r && *r) scaleLabel = Str(r);
            }
        }
        fz_catch(ctx) {}

        if (!isMeasure) continue;

        int page = a->pageNo;
        Str typeName = AnnotationReadableNameTemp(a->type);
        Str author = Author(a);
        Str contents = Contents(a);

        time_t mdate = ModificationDate(a);
        char dateBuf[32]{};
        if (mdate) {
            struct tm tmDate{};
            if (localtime_s(&tmDate, &mdate) == 0) strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M", &tmDate);
        }

        b.Append(fmt("%d,\"%s\",\"", page, typeName));
        for (int i = 0; i < contents.len; i++) { char c = contents.s[i]; if (c == '"') b.Append(StrL("\"\"")); else b.AppendChar(c); }
        b.Append(fmt("\",\"%s\",\"", unitName.len > 0 ? unitName : StrL("in")));
        for (int i = 0; i < scaleLabel.len; i++) { char c = scaleLabel.s[i]; if (c == '"') b.Append(StrL("\"\"")); else b.AppendChar(c); }
        b.Append(fmt("\",\"%s\",\"", author.len > 0 ? author : StrL("")));
        b.Append(fmt("%s\",\"", Str(dateBuf)));
        for (int i = 0; i < contents.len; i++) { char c = contents.s[i]; if (c == '"') b.Append(StrL("\"\"")); else b.AppendChar(c); }
        b.Append(StrL("\"\n"));
    }

    Str res = b.TakeStr();
    bool ok = file::WriteFile(Str(filePath), res);
    str::Free(res);
    return ok;
}

void OnCalibrateScale(MainWindow* win) {
    if (!win) return;
    gCurrentScale.pointsPerUnit = 7.2f;
    str::BufSet(Str(gCurrentScale.unit, sizeof(gCurrentScale.unit)), StrL("ft"));
    str::BufSet(Str(gCurrentScale.label, sizeof(gCurrentScale.label)), StrL("1 in = 10 ft"));
    ShowTemporaryNotification(win->hwndCanvas, Tr("Page scale calibrated to 1 in = 10 ft."));
}

void OnExportMeasurementsCsv(MainWindow* win) {
    if (!win || !win->CurrentTab()) return;
    EngineBase* engine = win->CurrentTab()->GetEngine();
    if (!engine) return;

    WCHAR path[MAX_PATH + 1]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = dimof(path);
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetSaveFileNameW(&ofn)) {
        Str err;
        TempStr utf8Path = ToUtf8Temp(path);
        if (ExportMeasurementsToCsv(engine, utf8Path.s, err)) {
            ShowTemporaryNotification(win->hwndCanvas, Tr("Measurements exported to CSV."));
        } else {
            ShowWarningNotification(win->hwndCanvas, err, kNotif5SecsTimeOut);
        }
    }
}
