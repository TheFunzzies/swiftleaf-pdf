from pathlib import Path
import subprocess
for fn in ['SumatraPDF','SumatraPDF-static']:
 p=Path('vs2022')/(fn+'.vcxproj.filters'); original=subprocess.check_output(['git','show','HEAD:'+p.as_posix()])
 for kind,old,new in [('ClCompile','PdfTools.cpp','ExportFormats.cpp'),('ClInclude','shared\\PdfTools.h','ExportFormats.h')]:
    anchor=f'    <{kind} Include="..\\src\\{old}"'.encode(); pos=original.index(anchor); close=f'</{kind}>'.encode(); end=original.index(close,pos)+len(close)
    block=original[pos:end].replace(old.encode(),new.encode()).replace(b'<Filter>src\\shared</Filter>',b'<Filter>src</Filter>').replace(b'\r\n',b'\n')
    end=original.index(b'\n',end)+1
    original=original[:end]+block+b'\n'+original[end:]
 p.write_bytes(original)
