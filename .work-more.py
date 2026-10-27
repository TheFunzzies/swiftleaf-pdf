from pathlib import Path
exec(Path('.work-redact.py').read_text().split("p='src/shared/EngineAll.h'")[0])
p='src/shared/EngineAll.h'
edit(p,'    bool preview = false;','    bool preview = false;\n    bool blackBoxes = true;')
p='src/shared/EngineMupdf.cpp'
edit(p,'            redaction.black_boxes = 1;','            redaction.black_boxes = !review || review->blackBoxes;')
edit(p,'            pdf_redact_page(ctx, doc, page, &redaction);','''            pdf_redact_page(ctx, doc, page, &redaction);
            if (review && len(review->reason)) {
                pdf_annot* note = pdf_create_annot(ctx, page, PDF_ANNOT_TEXT);
                pdf_set_annot_contents(ctx, note, CStrTemp(review->reason));
                pdf_drop_annot(ctx, note);
            }''')
p='src/PdfTools.cpp'
edit(p,'    Edit* reason = nullptr;','    Edit* reason = nullptr;\n    Checkbox* blackBoxes = nullptr;')
edit(p,'reason = AddLabeledEdit(Tr("Reason (audit note; black overlay):"), {});','reason = AddLabeledEdit(Tr("Reason (saved page note):"), {});\n        blackBoxes = AddCheck(Tr("Black overlay (otherwise leave removed area blank)"), true);')
edit(p,'        review.reason = reason->GetTextTemp();','        review.reason = reason->GetTextTemp();\n        review.blackBoxes = blackBoxes->IsChecked();')
# Common optimizer keeps the smaller file atomically, including same-path callers.
edit(p,'    if (pdfclean_main(len(argv), argv.els) != 0 || !output.Commit()) return false;', '''    if (pdfclean_main(len(argv), argv.els) != 0) return false;
    bool keptOriginal = file::GetSize(output.tempPath) >= file::GetSize(srcPath);
    if (keptOriginal && !CopyFileW(ToWStrTemp(srcPath).s, ToWStrTemp(output.tempPath).s, FALSE)) return false;
    if (!output.Commit()) return false;''')
edit(p,'        auditOut->afterBytes = file::GetSize(destPath);','''        auditOut->afterBytes = file::GetSize(destPath);
        auditOut->keptOriginal = keptOriginal;
        PdfSpaceReport(srcPath, auditOut->streamsBefore);
        PdfSpaceReport(destPath, auditOut->streamsAfter);''')
# Replace duplicate UI compressor with the shared path.
s=Path(p).read_text(); a=s.index('    // Light: clean',s.index('void PdfCompressDialog::DoIt')); b=s.index('    if (res == 0)',a)
s=s[:a]+'''    PdfOptimizationAudit audit;
    PdfOptimizerPreset preset = level == CompressLevel::Light ? PdfOptimizerPreset::Print :
        level == CompressLevel::Medium ? PdfOptimizerPreset::Web : PdfOptimizerPreset::MinSize;
    int res = PdfOptimizeFile(srcPath, destPath, preset, &audit) ? 0 : 1;
    if (!res) {
        MessageBoxW(hwnd, ToWStrTemp(fmt("%lld -> %lld bytes%s\\nEncoded streams before/after:\\nImages %lld/%lld; fonts %lld/%lld; other %lld/%lld.\\nRemaining bytes: dictionaries, xref and file overhead.\\nArchive/Print are lossless cleanup; neither converts to PDF/A.",
            audit.beforeBytes, audit.afterBytes, audit.keptOriginal ? StrL(" (original kept)") : StrL(""),
            audit.streamsBefore[0], audit.streamsAfter[0], audit.streamsBefore[1], audit.streamsAfter[1],
            audit.streamsBefore[2], audit.streamsAfter[2])).s, L"Compression report", MB_OK);
    }
''' +s[b:]; s=s.replace('res == 0 && output.Commit()', 'res == 0'); Path(p).write_text(s,newline='\r\n')
# Encoded stream contributions; no claim that decoded bytes equal file size.
s=Path(p).read_text(); pos=s.index('bool PdfOptimizeFile(')
s=s[:pos]+'''static void PdfSpaceReport(Str path, i64 (&sizes)[3]) {
    sizes[0] = sizes[1] = sizes[2] = 0;
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return;
    pdf_document* doc = nullptr;
    fz_var(doc);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(path));
        for (int i = 1; i < pdf_xref_len(ctx, doc); i++) {
            if (!pdf_obj_num_is_stream(ctx, doc, i)) continue;
            pdf_obj* obj = pdf_load_object(ctx, doc, i);
            int type = 2;
            if (pdf_dict_get(ctx, obj, PDF_NAME(Subtype)) == PDF_NAME(Image)) type = 0;
            if (pdf_dict_get(ctx, obj, PDF_NAME(Length1)) || pdf_dict_get(ctx, obj, PDF_NAME(Subtype)) == PDF_NAME(Type1C) ||
                pdf_dict_get(ctx, obj, PDF_NAME(Subtype)) == PDF_NAME(CIDFontType0C)) type = 1;
            sizes[type] += pdf_dict_get_int64(ctx, obj, PDF_NAME(Length));
            pdf_drop_obj(ctx, obj);
        }
    }
    fz_always(ctx) { pdf_drop_document(ctx, doc); }
    fz_catch(ctx) { fz_report_error(ctx); }
    fz_drop_context(ctx);
}

'''+s[pos:]; Path(p).write_text(s,newline='\r\n')
p='src/shared/PdfTools.h'
edit(p,'    i64 afterBytes = 0;','    i64 afterBytes = 0;\n    i64 streamsBefore[3]{};\n    i64 streamsAfter[3]{};\n    bool keptOriginal = false;')
p='src/BatchActions.cpp'
edit(p,'audit.beforeBytes, audit.afterBytes));','audit.beforeBytes, audit.afterBytes));\n                out.Append(fmt(",\\"keptOriginal\\":%s,\\"imageBytes\\":%lld,\\"fontBytes\\":%lld,\\"otherStreamBytes\\":%lld", audit.keptOriginal ? StrL("true") : StrL("false"), audit.streamsAfter[0], audit.streamsAfter[1], audit.streamsAfter[2]));')
