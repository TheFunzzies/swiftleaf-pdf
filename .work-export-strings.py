from pathlib import Path
import re
p=Path('src/ExportFormats.cpp');s=p.read_text()
for var in ['data','text','input']: s=re.sub(r'\b'+var+r'\[',var+'.s[',s)
for typ,var in [('char','s'),('char','text'),('WCHAR','wide'),('char','link')]:
 s=s.replace(f'for ({typ} c : {var}) {{',f'for (int charIndex=0;charIndex<len({var});charIndex++) {{\n        {typ} c={var}.s[charIndex];')
s=s.replace('for (unsigned char c : png) out.Append(fmt("%02x", (int)c));','for (int byteIndex=0;byteIndex<len(png);byteIndex++) out.Append(fmt("%02x", (int)(u8)png.s[byteIndex]));')
p.write_text(s)
# Preserve unrelated mixed newline content in checked-in generated filter files.
import subprocess
for fn in ['SumatraPDF','SumatraPDF-static']:
 p=Path('vs2022')/(fn+'.vcxproj.filters'); original=subprocess.check_output(['git','show','HEAD:'+p.as_posix()])
 for kind,old,new in [('ClCompile','PdfTools.cpp','ExportFormats.cpp'),('ClInclude','shared\\PdfTools.h','ExportFormats.h')]:
    anchor=f'    <{kind} Include="..\\src\\{old}"'.encode(); pos=original.index(anchor); close=f'</{kind}>'.encode(); end=original.index(close,pos)+len(close)
    block=original[pos:end].replace(old.encode(),new.encode()).replace(b'<Filter>src\\shared</Filter>',b'<Filter>src</Filter>')
    newline=b'\r\n' if original[end:end+2]==b'\r\n' else b'\n'
    original=original[:end]+newline+block+original[end:]
 p.write_bytes(original)
