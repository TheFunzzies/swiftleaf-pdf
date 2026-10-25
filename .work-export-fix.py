from pathlib import Path
p=Path('src/ExportFormats.cpp'); s=p.read_text()
s=s.replace('    bool ok = false;\n    fz_var(ok);\n    fz_try(d.ctx) {','    bool ok = false;\n    Vec<int> selected;\n    fz_var(ok);\n    fz_try(d.ctx) {',1).replace('        Vec<int> selected;\n        if (!PageRange(range,','        if (!PageRange(range,',1)
# MuPDF longjmp must not cross C++ builder or ZIP owners.
for name,end in [('WriteDocx','static void SheetCell'),('WriteXlsx','static const Str kShapeTree'),('WritePptx','static void RtfText')]:
    a=s.index('static bool '+name); b=s.index(end,a); chunk=s[a:b].replace('CheckCancel(d.ctx, p);','if (Cancelled(p)) return false;'); s=s[:a]+chunk+s[b:]
s=s.replace('static void WriteTextFormats(', 'static bool WriteTextFormats(')
a=s.index('static bool WriteTextFormats'); b=s.index('static bool WriteSvg',a); chunk=s[a:b].replace('CheckCancel(d.ctx, p);','if (Cancelled(p)) return false;'); chunk=chunk.rstrip(); pos=chunk.rfind('}'); chunk=chunk[:pos]+'    return true;\n'+chunk[pos:]+'\n\n'; s=s[:a]+chunk+s[b:]
s=s.replace('            WriteTextFormats(doc, format, text, progress);\n            ok = file::WriteFile(output.tempPath, ToStr(text));','            ok = WriteTextFormats(doc, format, text, progress) && file::WriteFile(output.tempPath, ToStr(text));')
a=s.index('static Str ImagePng'); b=s.index('static void Base64',a); chunk=s[a:b].replace('fz_rethrow(ctx);','return {};'); s=s[:a]+chunk+s[b:]
a=s.index('static bool WriteSvg'); b=s.index('bool ExportPdf',a); chunk=s[a:b].replace('CheckCancel(d.ctx, p);','if (Cancelled(p)) return false;').replace('fz_rethrow(d.ctx);','return Fail(p, Str(fz_caught_message(d.ctx)));'); s=s[:a]+chunk+s[b:]
# Ensure failed image conversion never yields a successful broken package.
s=s.replace('Str data = ImagePng(d.ctx, b->u.i.image);','Str data = ImagePng(d.ctx, b->u.i.image);\n                if (!len(data)) return false;').replace('Str png = ImagePng(d.ctx, b->u.i.image);','Str png = ImagePng(d.ctx, b->u.i.image);\n                if (!len(png)) return false;')
a=s.index('                    int col = 0;'); b=s.index("                    out.AppendChar('\\n');",a)
s=s[:a]+'''                    int indent = (int)((l->bbox.x0-page->text->mediabox.x0)/(page->bodySize*0.5f));
                    for (int i=0;i<indent && i<300;i++) out.AppendChar(' ');
                    float previous = l->bbox.x0;
                    for (auto* c=l->first_char;c;c=c->next) {
                        fz_rect box=fz_rect_from_quad(c->quad);
                        int gap=(int)((box.x0-previous)/(page->bodySize*0.5f));
                        if (gap>1) for (int i=0;i<gap && i<100;i++) out.AppendChar(' ');
                        Rune(out,c->c); previous=box.x1;
                    }
'''+s[b:]
# Page list lives outside the MuPDF exception region.
a=s.index('bool CreatePdfFiles'); chunk=s[a:]; chunk=chunk.replace('    fz_var(writer);','    Vec<int> selected;\n    fz_var(writer);',1).replace('            Vec<int> selected;','            VecReset(selected);',1); s=s[:a]+chunk
p.write_text(s)
# Keep premake output changes local to the newly added source/header.
import subprocess
for fn in ['SumatraPDF','SumatraPDF-static']:
    for suffix in ['.vcxproj','.vcxproj.filters']:
        p=Path('vs2022')/(fn+suffix)
        original=subprocess.check_output(['git','show','HEAD:'+p.as_posix()]).decode()
        anchor='    <ClCompile Include="..\\src\\PdfTools.cpp"'
        pos=original.index(anchor); end=original.index('\n',pos)
        if suffix.endswith('filters'):
            end=original.index('</ClCompile>',pos)+len('</ClCompile>')
        block=original[pos:end]
        original=original[:end]+'\n'+block.replace('PdfTools.cpp','ExportFormats.cpp')+original[end:]
        # Header belongs beside PdfTools shared declaration in VS filters.
        anchor='    <ClInclude Include="..\\src\\shared\\PdfTools.h"'
        pos=original.index(anchor); end=original.index('\n',pos)
        if suffix.endswith('filters'): end=original.index('</ClInclude>',pos)+len('</ClInclude>')
        block=original[pos:end]
        original=original[:end]+'\n'+block.replace('shared\\PdfTools.h','ExportFormats.h')+original[end:]
        p.write_text(original)
