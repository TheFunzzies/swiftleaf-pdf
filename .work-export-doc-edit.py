from pathlib import Path
p=Path('CHANGELOG.md'); s=p.read_text(encoding='utf-8'); pos=s.find('\n')+1
entry='\n## Unreleased\n\n- Export PDF pages to DOCX, PPTX, XLSX, HTML, RTF, Markdown, spaced text, JSON and per-page SVG ZIP with page ranges and a limitations note.\n- Create or combine PDFs from Office files, text, RTF, HTML, Markdown, images and clipboard images. Office uses Microsoft Office automation or LibreOffice when available.\n\n'
if '\n## Unreleased\n' in s: s=s.replace('\n## Unreleased\n','\n## Unreleased\n'+entry.split('## Unreleased\n')[1],1)
else: s=s[:pos]+entry+s[pos:]
p.write_text(s,encoding='utf-8')
p=Path('src/shared/CommandAvailability.cpp'); s=p.read_text(); anchor='static const i32 removeIfNoDiskAccessPerm[] = {'; s=s.replace(anchor,anchor+'\n    CmdExportPdf,\n    CmdCreatePdfFiles,\n    CmdCreatePdfClipboard,'); s=s.replace('    if (cmdId == CmdScanOcr &&','    if (cmdId == CmdExportPdf && (!ctx.allowsCopyingText || !HasPermission(Perm::CopySelection))) {\n        return CommandVisibility::Hide;\n    }\n    if (cmdId == CmdScanOcr &&',1); p.write_text(s)
p=Path('src/PdfTools.cpp'); s=p.read_text(); s=s.replace('    Str destination;\n    Str range;\n    ExportFormat format','    Str destination;\n    Str temporaryInput;\n    Str range;\n    ExportFormat format',1); s=s.replace('    ~ConversionJob() {\n        str::Free(destination);','    ~ConversionJob() {\n        if (len(temporaryInput)) file::Delete(temporaryInput);\n        str::Free(temporaryInput);\n        str::Free(destination);',1)
s=s.replace('    for (Str input : inputs) job->sources.Append(input);','''    if (len(clipboardPath)) {
        job->temporaryInput=str::Dup(GetTempFilePathTemp(StrL("conversion.bmp")));
        if (!CopyFileW(ToWStrTemp(clipboardPath).s,ToWStrTemp(job->temporaryInput).s,FALSE)) {
            job->Release(); job=nullptr;
            status->SetText(Tr("Cannot copy the clipboard image for conversion.")); return;
        }
        job->sources.Append(job->temporaryInput);
    } else for (Str input : inputs) job->sources.Append(input);''')
s=s.replace('    browseDefExt = ToWStrTemp(Str(kExportExtensions[index]));\n','')
s=s.replace('    if (!win || !win->IsDocLoaded() || !IsPdfDoc(win->CurrentTab())) return;','    if (!win || !CanAccessDisk() || !HasPermission(Perm::CopySelection) || !win->IsDocLoaded() || !IsPdfDoc(win->CurrentTab())) return;',1)
s=s.replace('void ShowCreatePdfFilesDialog(MainWindow* win, bool clipboard) {\n    if (!win) return;','void ShowCreatePdfFilesDialog(MainWindow* win, bool clipboard) {\n    if (!win || !CanAccessDisk()) return;')
p.write_text(s)
