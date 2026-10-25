from pathlib import Path
import subprocess
for fn in ['SumatraPDF','SumatraPDF-static']:
 for suffix in ['.vcxproj','.vcxproj.filters']:
    p=Path('vs2022')/(fn+suffix)
    original=subprocess.check_output(['git','show','HEAD:'+p.as_posix()]).decode('utf-8').replace('\r\n','\n')
    for kind,old,new in [('ClCompile','PdfTools.cpp','ExportFormats.cpp'),('ClInclude','shared\\PdfTools.h','ExportFormats.h')]:
        anchor=f'    <{kind} Include="..\\src\\{old}"'
        pos=original.index(anchor); end=original.index('\n',pos)
        if suffix.endswith('filters'): end=original.index(f'</{kind}>',pos)+len(f'</{kind}>')
        block=original[pos:end]
        original=original[:end]+'\n'+block.replace(old,new)+original[end:]
    p.write_text(original,encoding='utf-8')
