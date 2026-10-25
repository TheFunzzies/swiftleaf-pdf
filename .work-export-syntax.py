from pathlib import Path
import xml.etree.ElementTree as E
root=E.parse('vs2022/SumatraPDF.vcxproj').getroot(); ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
groups=root.findall('m:ItemDefinitionGroup',ns)
g=next(x for x in groups if "Debug|x64" in x.attrib.get('Condition',''))
c=g.find('m:ClCompile',ns)
incs=c.find('m:AdditionalIncludeDirectories',ns).text.split(';')
defs=c.find('m:PreprocessorDefinitions',ns).text.split(';')
args=['/nologo','/Zs','/std:c++20','/utf-8','/EHsc','/Y-']
args += ['/I"'+str((Path('vs2022')/p).resolve())+'"' for p in incs if not p.startswith('%')]
args += ['/D'+p for p in defs if not p.startswith('%')]
args+=['src/ExportFormats.cpp','src/PdfTools.cpp']
Path('.work-export-syntax.rsp').write_text('\n'.join(args),encoding='utf-8')
Path('.work-export-syntax.cmd').write_text('@call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul\n@cl @.work-export-syntax.rsp\n')
