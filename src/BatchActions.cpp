/* Copyright 2026 the Swiftleaf PDF authors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/DirScan.h"
#include "base/JsonParser.h"
#include "base/Win.h"
#include <commdlg.h>
#include <shlobj.h>
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "SumatraPDF.h"
#include "shared/AppTools.h"
#include "shared/Theme.h"
#include "DarkMode.h"
#include "Translations.h"
#include "Ocr.h"
#include "shared/PdfTools.h"
#include <mupdf/pdf.h>
#include <mupdf/fitz/json.h>
#include "PageTools2.h"
#include "ExportFormats.h"
#include "BatchActions.h"

extern "C" int pdfclean_main(int argc, char** argv);

enum class BatchKind {
    Ocr,
    Optimize,
    Watermark,
    Header,
    Bates,
    Crop,
    Rotate,
    Flatten,
    Sanitize,
    Redact,
    Encrypt,
    Properties,
    Export
};
struct BatchAction {
    BatchKind kind = BatchKind::Ocr;
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

static bool BatchNameValid(Str name) {
    if (len(name) == 0 || len(name) > 64) return false;
    for (int i = 0; i < len(name); i++) {
        char c = name.s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    return true;
}

static TempStr BatchNamedPath(Str name) {
    if (!BatchNameValid(name)) return {};
    TempStr dir = path::JoinTemp(GetAppDataDirTemp(), StrL("batch-actions"));
    if (!dir::CreateAll(dir)) return {};
    return path::JoinTemp(dir, fmt("%s.json", name));
}

static bool ParseBatch(Str definition, BatchDefinition* batch);

bool SaveBatchActionsJson(Str name, Str definition) {
    BatchDefinition batch;
    if (!BatchNameValid(name) || !ParseBatch(definition, &batch)) return false;
    TempStr path = BatchNamedPath(name);
    if (len(path) == 0) return false;
    file::AtomicFile output(path);
    return len(output.tempPath) > 0 && file::WriteFile(output.tempPath, definition) && output.Commit();
}

TempStr LoadBatchActionsJsonTemp(Str name) {
    TempStr path = BatchNamedPath(name);
    if (len(path) == 0) return {};
    Str data = file::ReadFile(path);
    TempStr result = str::DupTemp(data);
    str::Free(data);
    return result;
}

static Str BatchExportExt(Str type);

static bool ReadBatchObject(fz_json* obj, BatchDefinition* batch) {
    if (!obj || obj->type != FZ_JSON_OBJECT) return false;
    bool haveActions = false, haveSuffix = false, haveFolder = false;
    for (auto* field = obj->u.object; field; field = field->next) {
        Str key(field->key);
        auto* value = field->value;
        if (str::Eq(key, StrL("suffix"))) {
            if (haveSuffix || value->type != FZ_JSON_STRING) return false;
            haveSuffix = true;
            batch->suffix = str::DupTemp(Str(value->u.string));
        } else if (str::Eq(key, StrL("outputFolder"))) {
            if (haveFolder || value->type != FZ_JSON_STRING) return false;
            haveFolder = true;
            batch->outputFolder = str::DupTemp(Str(value->u.string));
        } else if (str::Eq(key, StrL("actions"))) {
            if (haveActions || value->type != FZ_JSON_ARRAY) return false;
            haveActions = true;
            for (auto* item = value->u.array; item; item = item->next) {
                if (len(batch->actions) >= 64 || item->value->type != FZ_JSON_OBJECT) return false;
                BatchAction action;
                unsigned seen = 0;
                const char* names[] = {"type", "text", "pages", "title", "author", "subject", "keywords", "value"};
                Str* strings[] = {&action.type,   &action.text,    &action.pages,   &action.title,
                                  &action.author, &action.subject, &action.keywords};
                for (auto* f = item->value->u.object; f; f = f->next) {
                    int index = -1;
                    for (int k = 0; k < dimof(names); k++)
                        if (str::Eq(Str(f->key), Str(names[k]))) index = k;
                    if (index < 0 || (seen & (1u << index))) return false;
                    seen |= 1u << index;
                    if (index == 7) {
                        if (f->value->type != FZ_JSON_NUMBER || !isfinite(f->value->u.number)) return false;
                        action.value = (float)f->value->u.number;
                    } else {
                        if (f->value->type != FZ_JSON_STRING) return false;
                        *strings[index] = str::DupTemp(Str(f->value->u.string));
                    }
                }
                if (!(seen & 1)) return false;
                Str t = action.type;
                unsigned allowed = 1, required = 1;
                if (str::Eq(t, StrL("ocr")))
                    action.kind = BatchKind::Ocr;
                else if (str::Eq(t, StrL("compress")) || str::Eq(t, StrL("optimize"))) {
                    action.kind = BatchKind::Optimize;
                    allowed |= 2;
                    if (len(action.text) && !str::Eq(action.text, StrL("web")) &&
                        !str::Eq(action.text, StrL("print")) && !str::Eq(action.text, StrL("archive")) &&
                        !str::Eq(action.text, StrL("min-size")))
                        return false;
                } else if (str::Eq(t, StrL("watermark")) || str::Eq(t, StrL("header-footer")) ||
                           str::Eq(t, StrL("bates"))) {
                    action.kind = str::Eq(t, StrL("watermark")) ? BatchKind::Watermark
                                  : str::Eq(t, StrL("bates"))   ? BatchKind::Bates
                                                                : BatchKind::Header;
                    allowed |= 2 | 4;
                    required |= 2;
                    if (!len(action.text)) return false;
                } else if (str::Eq(t, StrL("crop"))) {
                    action.kind = BatchKind::Crop;
                    allowed |= 128 | 4;
                    required |= 128;
                    if (!(action.value >= 0 && action.value <= 14400)) return false;
                } else if (str::Eq(t, StrL("rotate"))) {
                    action.kind = BatchKind::Rotate;
                    allowed |= 128;
                    required |= 128;
                    if (action.value != 90 && action.value != 180 && action.value != 270) return false;
                } else if (str::Eq(t, StrL("flatten")) || str::Eq(t, StrL("flatten-layers")))
                    action.kind = BatchKind::Flatten;
                else if (str::Eq(t, StrL("sanitize")))
                    action.kind = BatchKind::Sanitize;
                else if (str::Eq(t, StrL("encrypt")) || str::Eq(t, StrL("redact-matches"))) {
                    action.kind = str::Eq(t, StrL("encrypt")) ? BatchKind::Encrypt : BatchKind::Redact;
                    allowed |= 2;
                    required |= 2;
                    if (!len(action.text)) return false;
                } else if (str::Eq(t, StrL("properties")) || str::Eq(t, StrL("set-properties"))) {
                    action.kind = BatchKind::Properties;
                    allowed |= 8 | 16 | 32 | 64;
                    if (!(seen & (8 | 16 | 32 | 64))) return false;
                } else if (len(BatchExportExt(t))) {
                    // Multi-file image/SVG exports need a manifest transaction; refuse unsafe legacy plans.
                    if (str::Eq(t, StrL("export-images")) || str::Eq(t, StrL("export-image")) ||
                        str::Eq(t, StrL("export-svg")))
                        return false;
                    action.kind = BatchKind::Export;
                    allowed |= 4;
                    if (item->next) return false;
                } else
                    return false;
                if ((seen & ~allowed) || (seen & required) != required) return false;
                if (len(action.pages)) {
                    // Strict positive ascending ranges, no silently ignored fragments.
                    const char* q = CStrTemp(action.pages);
                    while (*q) {
                        char* end = nullptr;
                        long first = strtol(q, &end, 10);
                        if (end == q || first <= 0 || first > 1000000) return false;
                        q = end;
                        if (*q == '-') {
                            long last = strtol(q + 1, &end, 10);
                            if (end == q + 1 || last < first || last > 1000000) return false;
                            q = end;
                        }
                        if (!*q) break;
                        if (*q++ != ',' || !*q) return false;
                    }
                }
                VecAppend(batch->actions, action);
            }
        } else
            return false;
    }
    return haveActions && len(batch->actions) && BatchNameValid(batch->suffix);
}

static void CheckBatchStrings(int* valid, json::Value* v) {
    for (int i = 0; i < len(v->value); i++)
        if (!v->value.s[i]) *valid = false;
    for (StrNode* p = v->path; p; p = p->next)
        for (int i = 0; i < len(p->s); i++)
            if (!p->s.s[i]) *valid = false;
}

static bool ParseBatch(Str definition, BatchDefinition* batch) {
    int stringsValid = 1;
    if (len(definition) > 1024 * 1024 || !json::Parse(definition, MkFunc1(CheckBatchStrings, &stringsValid)) ||
        !stringsValid)
        return false;
    // The structural parser also sees empty containers and rejects unknown fields.
    fz_context* ctx = fz_new_context(nullptr, nullptr, 16 * 1024 * 1024);
    if (!ctx) return false;
    fz_pool* pool = nullptr;
    bool ok = false;
    fz_var(pool);
    fz_var(ok);
    fz_try(ctx) {
        pool = fz_new_pool(ctx);
        ok = ReadBatchObject(fz_parse_json(ctx, pool, CStrTemp(definition)), batch);
    }
    fz_always(ctx) {
        fz_drop_pool(ctx, pool);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return ok;
}

static bool ApplyBatchAction(Str src, Str dst, const BatchAction& action, PdfOptimizationAudit* auditOut) {
    if (str::Eq(action.type, StrL("ocr"))) return BatchOcrPdf(src, dst);
    if (str::Eq(action.type, StrL("compress")) || str::Eq(action.type, StrL("optimize"))) {
        PdfOptimizerPreset preset = PdfOptimizerPreset::Web;
        if (str::EqI(action.text, StrL("print"))) preset = PdfOptimizerPreset::Print;
        if (str::EqI(action.text, StrL("archive"))) preset = PdfOptimizerPreset::Archive;
        if (str::EqI(action.text, StrL("min-size"))) preset = PdfOptimizerPreset::MinSize;
        return PdfOptimizeFile(src, dst, preset, auditOut);
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
    if (str::Eq(action.type, StrL("rotate"))) {
        fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
        if (!ctx) return false;
        fz_register_document_handlers(ctx);
        fz_document* doc = nullptr;
        int nPages = 0;
        fz_try(ctx) {
            doc = fz_open_document(ctx, CStrTemp(src));
            nPages = fz_count_pages(ctx, doc);
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
        }
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        if (nPages <= 0) return false;
        Vec<PdfMergeSource> sources;
        VecAppend(sources, PdfMergeSource{src, {}});
        Vec<PdfMergePage> pages;
        int degrees = (int)action.value;
        for (int i = 1; i <= nPages; i++) VecAppend(pages, PdfMergePage{0, i, degrees});
        return EngineMupdfMergePdfs(sources, pages, dst);
    }
    if (str::Eq(action.type, StrL("flatten")) || str::Eq(action.type, StrL("flatten-layers")))
        return PdfFlattenLayers(src, dst);
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
    if (str::Eq(action.type, StrL("encrypt"))) {
        char* pwdZ = CStrTemp(action.text);
        file::AtomicFile output(dst);
        if (len(output.tempPath) == 0) return false;
        char* argv[] = {(char*)"clean", (char*)"-E",   (char*)"aes-256",         (char*)"-U", pwdZ, (char*)"-O",
                        pwdZ,           CStrTemp(src), CStrTemp(output.tempPath)};
        fz_set_optind(0);
        int res = pdfclean_main(9, argv);
        if (res == 0 && !output.Commit()) res = 1;
        return res == 0;
    }
    if (str::Eq(action.type, StrL("properties")) || str::Eq(action.type, StrL("set-properties"))) {
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
    if (str::Eq(action.type, StrL("export-images")) || str::Eq(action.type, StrL("export-image"))) {
        fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
        if (!ctx) return false;
        fz_register_document_handlers(ctx);
        fz_document* doc = nullptr;
        bool ok = true;
        fz_try(ctx) {
            doc = fz_open_document(ctx, CStrTemp(src));
            int nPages = fz_count_pages(ctx, doc);
            if (nPages <= 0) {
                ok = false;
            } else {
                for (int i = 0; i < nPages; i++) {
                    fz_page* page = fz_load_page(ctx, doc, i);
                    fz_pixmap* pix = fz_new_pixmap_from_page(ctx, page, fz_scale(2.0f, 2.0f), fz_device_rgb(ctx), 0);
                    TempStr outImg =
                        (nPages == 1) ? str::DupTemp(dst) : fmt("%s_%d.png", path::GetPathNoExtTemp(dst), i + 1);
                    fz_save_pixmap_as_png(ctx, pix, CStrTemp(outImg));
                    fz_drop_pixmap(ctx, pix);
                    fz_drop_page(ctx, page);
                }
            }
        }
        fz_catch(ctx) {
            fz_report_error(ctx);
            ok = false;
        }
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return ok;
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
    if (str::Eq(type, StrL("export-images")) || str::Eq(type, StrL("export-image"))) return StrL(".png");
    return {};
}

static TempStr BatchOutputPath(Str input, Str suffix, Str folder, Str outputExt = {}) {
    TempStr base = path::GetBaseNameTemp(input);
    TempStr stem = path::GetPathNoExtTemp(base);
    TempStr ext = len(outputExt) > 0 ? str::DupTemp(outputExt) : path::GetExtTemp(input);
    TempStr name = fmt("%s%s%s", stem, suffix, ext);
    if (len(folder) > 0) return path::JoinTemp(folder, name);
    return path::JoinTemp(path::GetDirTemp(input), name);
}

static bool BatchDestinations(const BatchDefinition& batch, const Vec<Str>& files, StrVec& outputs) {
    if (!len(files) || len(files) > 10000) return false;
    for (Str input : files) {
        if (!file::Exists(input)) return false;
        TempStr dst = path::NormalizeTemp(BatchOutputPath(input, batch.suffix, batch.outputFolder,
                                                          BatchExportExt(batch.actions[len(batch.actions) - 1].type)));
        if (FileSystemEntryExists(dst)) return false;
        for (Str source : files)
            if (path::IsSame(source, dst)) return false;
        for (int j = 0; j < len(outputs); j++)
            if (path::IsSame(outputs[j], dst)) return false;
        outputs.Append(dst);
    }
    return true;
}

TempStr RunBatchActionsJson(Str definition, const Vec<Str>& files, BatchProgress* progress) {
    BatchDefinition batch;
    StrVec outputs;
    if (!ParseBatch(definition, &batch) || !BatchDestinations(batch, files, outputs))
        return StrL(
            "{\"ok\":false,\"error\":\"Invalid plan, duplicate output, input overwrite or existing "
            "destination\",\"results\":[]}");
    if (len(batch.outputFolder) && !dir::CreateAll(batch.outputFolder))
        return StrL("{\"ok\":false,\"error\":\"Cannot create output folder\",\"results\":[]}");
    str::Builder records;
    bool allOk = true;
    if (progress) {
        InterlockedExchange(&progress->completedFiles, 0);
        InterlockedExchange(&progress->totalFiles, len(files));
    }
    for (int i = 0; i < len(files); i++) {
        AutoArenaSavepoint scratch;
        if (i) records.AppendChar(',');
        Str current = files[i];
        bool ok = true;
        Vec<file::AtomicFile*> stages;
        PdfOptimizationAudit audit{};
        if (progress) {
            InterlockedExchange(&progress->currentFile, i + 1);
            InterlockedExchange(&progress->totalActions, len(batch.actions));
        }
        for (int j = 0; ok && j < len(batch.actions); j++) {
            if (progress && InterlockedCompareExchange(&progress->cancelled, 0, 0)) {
                ok = false;
                break;
            }
            if (progress) InterlockedExchange(&progress->currentAction, j + 1);
            // Only owned, exclusively created siblings may be removed during cleanup.
            auto* stage = new file::AtomicFile(outputs[i]);
            VecAppend(stages, stage);
            ok = len(stage->tempPath) && ApplyBatchAction(current, stage->tempPath, batch.actions[j], &audit);
            if (ok) current = stage->tempPath;
        }
        if (progress && InterlockedCompareExchange(&progress->cancelled, 0, 0)) ok = false;
        if (ok) {
            auto handle = file::OpenReadWrite(current, false);
            ok = handle != file::kInvalidFileHandle;
            if (ok) {
                ok = file::Flush(handle);
                file::Close(handle);
            }
            // No replace flag: a racing writer must win without losing its file.
            if (ok) ok = MoveFileExW(CWStrTemp(current), CWStrTemp(outputs[i]), MOVEFILE_WRITE_THROUGH) != 0;
        }
        for (auto* stage : stages) delete stage;
        allOk = allOk && ok;
        records.Append(
            fmt("{\"input\":\"%s\",\"ok\":%s", json::EscapeStrTemp(files[i]), ok ? StrL("true") : StrL("false")));
        if (ok)
            records.Append(fmt(",\"output\":\"%s\",\"beforeBytes\":%lld,\"afterBytes\":%lld",
                               json::EscapeStrTemp(outputs[i]), audit.beforeBytes, audit.afterBytes));
        else
            records.Append(StrL(",\"error\":\"Action failed, cancelled or output already exists\""));
        records.AppendChar('}');
        if (progress) InterlockedExchange(&progress->completedFiles, i + 1);
        if (progress && InterlockedCompareExchange(&progress->cancelled, 0, 0)) break;
    }
    return fmt("{\"ok\":%s,\"total\":%d,\"results\":[%s]}", allOk ? StrL("true") : StrL("false"), len(files),
               ToStr(records));
}

struct BatchWizardJob {
    volatile LONG refs = 1;
    Str definition;
    Vec<Str> files;
    BatchProgress progress;
    Str resultJson;
    volatile LONG done = 0;
    ~BatchWizardJob() {
        str::Free(definition);
        str::Free(resultJson);
        for (Str f : files) str::Free(f);
    }
    void Release() {
        if (!InterlockedDecrement(&refs)) delete this;
    }
};

static void RunBatchWizardThread(BatchWizardJob* job) {
    TempStr res = RunBatchActionsJson(job->definition, job->files, &job->progress);
    job->resultJson = str::Dup(res);
    InterlockedExchange(&job->done, 1);
    job->Release();
}

struct BatchActionsDialog : WindowBase {
    MainWindow* win = nullptr;
    VirtFixedLinesText* status = nullptr;
    VirtText* fileCountText = nullptr;
    VirtButton* btnAddFiles = nullptr;
    VirtButton* btnAddFolder = nullptr;
    VirtButton* btnSetFolder = nullptr;
    VirtButton* btnStart = nullptr;
    VirtButton* btnCancel = nullptr;
    DropDown* actionPreset = nullptr;
    Edit* suffixEdit = nullptr;
    Vec<Str> inputFiles;
    Str outputFolder;
    BatchWizardJob* job = nullptr;
    bool running = false;

    ~BatchActionsDialog() override {
        if (job) {
            InterlockedExchange(&job->progress.cancelled, 1);
            job->Release();
        }
        for (Str f : inputFiles) str::Free(f);
        str::Free(outputFolder);
    }

    bool Create(MainWindow* win);
    void OnAddFiles(VirtMouseEvent*);
    void OnAddFolder(VirtMouseEvent*);
    void OnSetFolder(VirtMouseEvent*);
    void OnStart(VirtMouseEvent*);
    void OnCancel(VirtMouseEvent*);
    void Poll(WindowBase::TimerEvent*);
    void Closed(WindowBase::CloseEvent*) { Close(); }
};

void BatchActionsDialog::OnAddFiles(VirtMouseEvent*) {
    WCHAR buf[65536]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = dimof(buf);
    ofn.lpstrFilter = L"PDF Files\0*.pdf\0All Files\0*.*\0";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    const WCHAR* p = buf;
    TempStr dir = ToUtf8Temp(WStr(p));
    p += wcslen(p) + 1;
    if (!*p) {
        VecAppend(inputFiles, str::Dup(dir));
    } else {
        while (*p) {
            TempStr full = path::JoinTemp(dir, ToUtf8Temp(WStr(p)));
            VecAppend(inputFiles, str::Dup(full));
            p += wcslen(p) + 1;
        }
    }
    fileCountText->SetText(fmt(Tr("%d PDF files selected").s, len(inputFiles)));
    btnStart->SetIsEnabled(len(inputFiles) > 0);
}

void BatchActionsDialog::OnAddFolder(VirtMouseEvent*) {
    WCHAR pathBuf[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select Folder with PDF Files";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    if (SHGetPathFromIDListW(pidl, pathBuf)) {
        TempStr dir = ToUtf8Temp(WStr(pathBuf));
        DirIter it{dir};
        it.includeDirs = false;
        it.includeFiles = true;
        it.recurse = true;
        for (DirIterEntry* e : it) {
            if (str::EqI(path::GetExtTemp(e->name), StrL(".pdf"))) {
                VecAppend(inputFiles, str::Dup(e->filePath));
            }
        }
        fileCountText->SetText(fmt(Tr("%d PDF files selected").s, len(inputFiles)));
        btnStart->SetIsEnabled(len(inputFiles) > 0);
    }
    CoTaskMemFree(pidl);
}

void BatchActionsDialog::OnSetFolder(VirtMouseEvent*) {
    WCHAR pathBuf[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select Output Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    if (SHGetPathFromIDListW(pidl, pathBuf)) {
        str::ReplaceWithCopy(&outputFolder, ToUtf8Temp(WStr(pathBuf)));
        status->SetText(fmt(Tr("Output folder: %s").s, outputFolder));
    }
    CoTaskMemFree(pidl);
}

void BatchActionsDialog::OnStart(VirtMouseEvent*) {
    if (running || len(inputFiles) == 0) return;
    int sel = CbGetCurrentSelection(actionPreset);
    Str def;
    switch (sel) {
        case 0:
            def = StrL("{\"actions\":[{\"type\":\"ocr\"}]}");
            break;
        case 1:
            def = StrL("{\"actions\":[{\"type\":\"optimize\",\"text\":\"web\"}]}");
            break;
        case 2:
            def = StrL("{\"actions\":[{\"type\":\"optimize\",\"text\":\"print\"}]}");
            break;
        case 3:
            def = StrL("{\"actions\":[{\"type\":\"optimize\",\"text\":\"min-size\"}]}");
            break;
        case 4:
            def = StrL("{\"actions\":[{\"type\":\"sanitize\"}]}");
            break;
        case 5:
            def = StrL("{\"actions\":[{\"type\":\"flatten\"}]}");
            break;
        case 6:
            def = StrL("{\"actions\":[{\"type\":\"watermark\",\"text\":\"CONFIDENTIAL\"}]}");
            break;
        default:
            def = StrL("{\"actions\":[{\"type\":\"optimize\",\"text\":\"web\"}]}");
            break;
    }
    TempStr sfx = suffixEdit ? suffixEdit->GetTextTemp() : StrL("_batch");
    if (len(sfx) == 0) sfx = StrL("_batch");

    str::Builder b;
    b.Append(StrL("{\"suffix\":\""));
    b.Append(json::EscapeStrTemp(sfx));
    b.Append(StrL("\""));
    if (len(outputFolder) > 0) {
        b.Append(StrL(",\"outputFolder\":\""));
        b.Append(json::EscapeStrTemp(outputFolder));
        b.AppendChar('"');
    }
    b.Append(StrL(",\"actions\":"));
    b.Append(Str(def.s + 11)); // skip {"actions":

    BatchDefinition typed;
    StrVec outputs;
    if (!ParseBatch(ToStr(b), &typed) || !BatchDestinations(typed, inputFiles, outputs)) {
        status->SetText(Tr("Invalid plan or output collision. No files written."));
        return;
    }
    str::Builder preview;
    preview.Append(StrL("Validated PDF plan:\n"));
    for (const auto& a : typed.actions)
        preview.Append(
            fmt("  PDF -> %s: %s\n", a.kind == BatchKind::Export ? BatchExportExt(a.type) : StrL("PDF"), a.type));
    for (int i = 0; i < len(inputFiles); i++) preview.Append(fmt("%s -> %s\n", inputFiles[i], outputs[i]));
    preview.Append(StrL("Existing files will not be overwritten. Run this plan?"));
    if (MessageBoxW(hwnd, CWStrTemp(ToStr(preview)), L"Batch plan preview", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return;
    if (job) job->Release();
    job = new BatchWizardJob();
    job->definition = str::Dup(ToStr(b));
    for (Str f : inputFiles) VecAppend(job->files, str::Dup(f));

    running = true;
    btnAddFiles->SetIsEnabled(false);
    btnAddFolder->SetIsEnabled(false);
    btnSetFolder->SetIsEnabled(false);
    actionPreset->SetIsEnabled(false);
    btnStart->SetIsEnabled(false);
    btnCancel->SetText(Tr("Cancel"));
    status->SetText(Tr("Starting batch execution..."));

    InterlockedIncrement(&job->refs);
    ThreadHandle thread = StartThread(MkFunc0(RunBatchWizardThread, job), StrL("Batch Actions"));
    if (!thread) {
        job->Release();
        InterlockedExchange(&job->done, 1);
        running = false;
        btnStart->SetIsEnabled(true);
        status->SetText(Tr("Could not start batch worker."));
        return;
    }
    SafeCloseThreadHandle(&thread);
    SetTimer(hwnd, 1, 100, nullptr);
}

void BatchActionsDialog::OnCancel(VirtMouseEvent*) {
    if (running && job) {
        InterlockedExchange(&job->progress.cancelled, 1);
        status->SetText(Tr("Cancelling..."));
        return;
    }
    Close();
}

void BatchActionsDialog::Poll(WindowBase::TimerEvent*) {
    if (!job) return;
    if (!InterlockedCompareExchange(&job->done, 0, 0)) {
        int f = InterlockedCompareExchange(&job->progress.currentFile, 0, 0);
        int totF = InterlockedCompareExchange(&job->progress.totalFiles, 0, 0);
        int a = InterlockedCompareExchange(&job->progress.currentAction, 0, 0);
        int totA = InterlockedCompareExchange(&job->progress.totalActions, 0, 0);
        if (totF > 0) {
            status->SetText(fmt(Tr("Processing file %d of %d (action %d of %d)...").s, f, totF, a, totA));
        }
        return;
    }
    KillTimer(hwnd, 1);
    running = false;
    btnAddFiles->SetIsEnabled(true);
    btnAddFolder->SetIsEnabled(true);
    btnSetFolder->SetIsEnabled(true);
    actionPreset->SetIsEnabled(true);
    btnStart->SetIsEnabled(len(inputFiles) > 0);
    btnCancel->SetText(Tr("Close"));
    if (InterlockedCompareExchange(&job->progress.cancelled, 0, 0)) {
        status->SetText(Tr("Batch actions cancelled."));
    } else {
        int comp = InterlockedCompareExchange(&job->progress.completedFiles, 0, 0);
        status->SetText(fmt("Batch finished (%d files). %s", comp,
                            str::StartsWith(job->resultJson, StrL("{\"ok\":true"))
                                ? StrL("Saved successfully.")
                                : StrL("Failed; inspect plan and output paths.")));
    }
}

bool BatchActionsDialog::Create(MainWindow* w) {
    win = w;
    closeOnEsc = true;
    onClose = MkMethod1<BatchActionsDialog, WindowBase::CloseEvent*, &BatchActionsDialog::Closed>(this);
    onTimer = MkMethod1<BatchActionsDialog, WindowBase::TimerEvent*, &BatchActionsDialog::Poll>(this);

    CreateCustomArgs args;
    args.owner = win ? win->hwndFrame : nullptr;
    args.title = Tr("Action Wizard");
    args.font = GetDefaultGuiFont();
    args.style = WS_OVERLAPPEDWINDOW;
    args.visible = false;
    args.bgColor = DarkModeDialogBgColor();
    if (!CreateCustom(args)) return false;

    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;
    box->gap = DpiScale(10);

    status = new VirtFixedLinesText();
    status->font = font;
    status->nLines = 2;
    status->SetText(Tr("Run automated batch actions over multiple PDF files."));
    box->AddChild(status);

    fileCountText = NewVirtText({.s = Tr("No PDF files selected"), .font = font});
    box->AddChild(fileCountText);

    auto* hFiles = new HBox();
    hFiles->gap = DpiScale(8);
    btnAddFiles = NewThemedButton(hwnd, Tr("Add Files..."), font, false);
    btnAddFiles->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnAddFiles>(this);
    hFiles->AddChild(btnAddFiles);

    btnAddFolder = NewThemedButton(hwnd, Tr("Add Folder..."), font, false);
    btnAddFolder->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnAddFolder>(this);
    hFiles->AddChild(btnAddFolder);

    btnSetFolder = NewThemedButton(hwnd, Tr("Output Folder..."), font, false);
    btnSetFolder->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnSetFolder>(this);
    hFiles->AddChild(btnSetFolder);
    box->AddChild(hFiles);

    box->AddChild(NewVirtText({.s = Tr("Action Preset:"), .font = font}));
    actionPreset = new DropDown();
    actionPreset->Create({.parent = hwnd, .font = font});
    StrVec presets;
    presets.Append(Tr("OCR Searchable"));
    presets.Append(Tr("Optimize for Web"));
    presets.Append(Tr("Optimize for Print"));
    presets.Append(Tr("Optimize for Min Size"));
    presets.Append(Tr("Sanitize Document"));
    presets.Append(Tr("Flatten Layers"));
    presets.Append(Tr("Watermark (CONFIDENTIAL)"));
    actionPreset->SetItems(presets);
    CbSetCurrentSelection(actionPreset, 1);
    box->AddChild(actionPreset);

    auto* hSuffix = new HBox();
    hSuffix->gap = DpiScale(8);
    hSuffix->AddChild(NewVirtText({.s = Tr("File suffix:"), .font = font}));
    Edit::CreateArgs sargs;
    sargs.parent = hwnd;
    sargs.withBorder = true;
    sargs.text = StrL("_batch");
    sargs.font = font;
    suffixEdit = new Edit();
    suffixEdit->Create(sargs);
    hSuffix->AddChild(suffixEdit, 1);
    box->AddChild(hSuffix);

    auto* buttons = new HBox();
    buttons->gap = DpiScale(8);
    btnStart = NewThemedButton(hwnd, Tr("Start Batch"), font, true);
    btnStart->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnStart>(this);
    btnStart->SetIsEnabled(false);
    buttons->AddChild(btnStart);

    btnCancel = NewThemedButton(hwnd, Tr("Close"), font, false);
    btnCancel->onClick = MkMethod1<BatchActionsDialog, VirtMouseEvent*, &BatchActionsDialog::OnCancel>(this);
    buttons->AddChild(btnCancel);
    box->AddChild(buttons);

    layout = new Padding(box, DpiScaledInsets(16));
    RECT rect = {0, 0, DpiScale(540), DpiScale(340)};
    AdjustWindowRectEx(&rect, args.style, FALSE, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    UpdateTheme();
    HwndCenterDialog(hwnd, win ? win->hwndFrame : nullptr);
    SetIsVisible(true);
    return true;
}

void ShowBatchActionsDialog(MainWindow* win) {
    auto* dlg = new BatchActionsDialog();
    if (!dlg->Create(win)) delete dlg;
}
