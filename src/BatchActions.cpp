/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/JsonParser.h"
#include "Ocr.h"
#include "shared/PdfTools.h"
#include "shared/EngineAll.h"
#include "PageTools2.h"
#include "ExportFormats.h"
#include "BatchActions.h"

struct BatchAction {
    Str type;
    Str text;
    Str pages;
    Str title;
    Str author;
    Str subject;
    Str keywords;
    float value = 0;
};

struct BatchDefinition {
    Vec<BatchAction> actions;
    Str suffix = StrL("_batch");
    Str outputFolder;
};

static void ReadActionValue(BatchDefinition* batch, json::Value* v) {
    StrNode* a = v->path;
    if (!a) return;
    Str rootKey = json::PathSegKey(a);
    if (!a->next) {
        if (str::Eq(rootKey, StrL("suffix"))) batch->suffix = str::Dup(GetPermArena(), v->value);
        if (str::Eq(rootKey, StrL("outputFolder"))) batch->outputFolder = str::Dup(GetPermArena(), v->value);
        return;
    }
    if (!str::Eq(rootKey, StrL("actions")) || !a->next || json::PathSegIndex(a->next) < 0) return;
    int idx = json::PathSegIndex(a->next);
    if (idx >= len(batch->actions)) VecResize(batch->actions, idx + 1);
    BatchAction& action = batch->actions[idx];
    Str key = json::PathSegKey(a->next->next);
    Str value = str::Dup(GetPermArena(), v->value);
    if (str::Eq(key, StrL("/type")))
        action.type = value;
    else if (str::Eq(key, StrL("/text")))
        action.text = value;
    else if (str::Eq(key, StrL("/pages")))
        action.pages = value;
    else if (str::Eq(key, StrL("/title")))
        action.title = value;
    else if (str::Eq(key, StrL("/author")))
        action.author = value;
    else if (str::Eq(key, StrL("/subject")))
        action.subject = value;
    else if (str::Eq(key, StrL("/keywords")))
        action.keywords = value;
    else if (str::Eq(key, StrL("/value")))
        action.value = (float)atof(CStrTemp(value));
}

static bool ApplyBatchAction(Str src, Str dst, const BatchAction& action) {
    if (str::Eq(action.type, StrL("ocr"))) return BatchOcrPdf(src, dst);
    if (str::Eq(action.type, StrL("compress")) || str::Eq(action.type, StrL("optimize"))) {
        PdfOptimizerPreset preset = PdfOptimizerPreset::Web;
        if (str::EqI(action.text, StrL("print"))) preset = PdfOptimizerPreset::Print;
        if (str::EqI(action.text, StrL("archive"))) preset = PdfOptimizerPreset::Archive;
        if (str::EqI(action.text, StrL("min-size"))) preset = PdfOptimizerPreset::MinSize;
        return PdfOptimizeFile(src, dst, preset);
    }
    if (str::Eq(action.type, StrL("watermark"))) {
        PdfWatermarkParams p{};
        p.text = action.text;
        p.pagesSpec = action.pages;
        return PdfApplyWatermark(src, dst, p);
    }
    if (str::Eq(action.type, StrL("header-footer"))) {
        PdfHeaderFooterParams p{};
        p.headerCenter = action.text;
        p.pagesSpec = action.pages;
        return PdfApplyHeaderFooter(src, dst, p);
    }
    if (str::Eq(action.type, StrL("bates"))) {
        PdfBatesParams p{};
        p.prefix = action.text;
        p.pagesSpec = action.pages;
        return PdfApplyBatesNumbering(src, dst, p);
    }
    if (str::Eq(action.type, StrL("crop"))) {
        PdfPageBoxesParams p{};
        p.pagesSpec = action.pages;
        p.marginLeft = p.marginBottom = p.marginRight = p.marginTop = action.value;
        return PdfApplyPageBoxes(src, dst, p);
    }
    if (str::Eq(action.type, StrL("flatten-layers"))) return PdfFlattenLayers(src, dst);
    if (str::Eq(action.type, StrL("sanitize"))) {
        PdfSanitizeOptions opts{};
        Str error;
        bool ok = EngineMupdfSanitizePdf({src, {}}, dst, opts, &error);
        str::Free(error);
        return ok;
    }
    if (str::Eq(action.type, StrL("redact-matches"))) {
        int count = 0;
        Str error;
        bool ok = EngineMupdfRedactMatches({src, {}}, dst, action.text, PdfSearchMode::Text, &count, &error);
        str::Free(error);
        return ok;
    }
    if (str::Eq(action.type, StrL("properties"))) {
        PdfDocPropsParams p{};
        p.title = action.title;
        p.author = action.author;
        p.subject = action.subject;
        p.keywords = action.keywords;
        return PdfSetDocProps(src, dst, p);
    }
    if (str::Eq(action.type, StrL("export-text"))) {
        ConversionProgress progress{};
        return ExportPdf(src, dst, ExportFormat::Text, action.pages, progress);
    }
    if (str::Eq(action.type, StrL("export-html")) || str::Eq(action.type, StrL("export-markdown")) ||
        str::Eq(action.type, StrL("export-json")) || str::Eq(action.type, StrL("export-svg"))) {
        ExportFormat format = ExportFormat::Html;
        if (str::Eq(action.type, StrL("export-markdown"))) format = ExportFormat::Markdown;
        if (str::Eq(action.type, StrL("export-json"))) format = ExportFormat::Json;
        if (str::Eq(action.type, StrL("export-svg"))) format = ExportFormat::Svg;
        ConversionProgress progress{};
        return ExportPdf(src, dst, format, action.pages, progress);
    }
    return false;
}

static Str BatchExportExt(Str type) {
    if (str::Eq(type, StrL("export-text"))) return StrL(".txt");
    if (str::Eq(type, StrL("export-html"))) return StrL(".html");
    if (str::Eq(type, StrL("export-markdown"))) return StrL(".md");
    if (str::Eq(type, StrL("export-json"))) return StrL(".json");
    if (str::Eq(type, StrL("export-svg"))) return StrL(".svg");
    return {};
}

static TempStr BatchOutputPath(Str input, Str suffix, Str folder, Str outputExt = {}) {
    TempStr base = path::GetBaseNameTemp(input);
    TempStr stem = path::GetPathNoExtTemp(base);
    TempStr ext = outputExt ? str::DupTemp(outputExt) : path::GetExtTemp(input);
    TempStr name = fmt("%s%s%s", stem, suffix, ext);
    if (folder) return path::JoinTemp(folder, name);
    return path::JoinTemp(path::GetDirTemp(input), name);
}

TempStr RunBatchActionsJson(Str definition, const Vec<Str>& files) {
    BatchDefinition batch;
    if (!json::Parse(definition, MkFunc1(ReadActionValue, &batch))) {
        return str::DupTemp(StrL("{\"error\":\"invalid action JSON\",\"results\":[]}"));
    }
    if (batch.outputFolder && !file::CreateAll(batch.outputFolder, nullptr)) {
        return str::DupTemp(StrL("{\"error\":\"cannot create output folder\",\"results\":[]}"));
    }
    str::Builder out;
    bool allOk = true;
    out.Append(StrL("{\"ok\":"));
    out.Append(StrL("true,\"total\":"));
    out.Append(fmt("%d,\"results\":[", len(files)));
    for (int i = 0; i < len(files); i++) {
        if (i) out.AppendChar(',');
        Str current = files[i];
        bool ok = true;
        Str error;
        for (int j = 0; j < len(batch.actions); j++) {
            const BatchAction& action = batch.actions[j];
            bool last = j == len(batch.actions) - 1;
            Str outputExt = last ? BatchExportExt(action.type) : Str();
            TempStr dest;
            if (last) {
                dest = BatchOutputPath(files[i], batch.suffix, batch.outputFolder, outputExt);
            } else {
                TempStr suffix = fmt("%s_%d_%d", batch.suffix, i + 1, j + 1);
                dest = BatchOutputPath(files[i], suffix, batch.outputFolder);
            }
            TempStr staged =
                last ? BatchOutputPath(files[i], fmt("%s_partial", batch.suffix), batch.outputFolder, outputExt) : dest;
            ok = ApplyBatchAction(current, staged, action);
            if (ok && last) ok = file::RenameReplace(dest, staged);
            if (!ok) {
                error = action.type;
                file::Delete(staged);
                if (j > 0 && !str::Eq(current, files[i])) file::Delete(current);
                break;
            }
            if (j > 0 && !str::Eq(current, files[i])) file::Delete(current);
            current = str::DupTemp(dest);
        }
        if (len(batch.actions) == 0) {
            ok = false;
            error = StrL("no actions configured");
        }
        allOk = allOk && ok;
        out.Append(StrL("{\"input\":\""));
        out.Append(json::EscapeStrTemp(files[i]));
        out.Append(StrL("\",\"ok\":"));
        out.Append(ok ? StrL("true") : StrL("false"));
        if (ok) {
            out.Append(StrL(",\"output\":\""));
            out.Append(json::EscapeStrTemp(current));
            out.AppendChar('"');
        } else {
            out.Append(StrL(",\"error\":\"action failed: "));
            out.Append(json::EscapeStrTemp(error));
            out.AppendChar('"');
        }
        out.AppendChar('}');
    }
    out.Append(StrL("]}"));
    if (!allOk) {
        Str json = ToStrTemp(out);
        return str::ReplaceTemp(json, StrL("\"ok\":true"), StrL("\"ok\":false"));
    }
    return ToStrTemp(out);
}
