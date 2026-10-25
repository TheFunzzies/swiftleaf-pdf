from pathlib import Path
p=Path('src/ExportFormats.cpp'); s=p.read_text()
s=s.replace('Str source = StrL("docs/test/export-fixture.pdf");','Str source = path::JoinTemp(GetSelfExeDirTemp(), StrL("../../docs/test/export-fixture.pdf"));')
s=s.replace('Str sample = StrL("docs/test/swiftleaf-sample.pdf");','Str sample = path::JoinTemp(GetSelfExeDirTemp(), StrL("../../docs/test/swiftleaf-sample.pdf"));')
# Theme supplies explicit color/font defaults to consumers of the presentation package.
a=s.index('static bool WritePptx(')
s=s[:a]+'''static void SlideTheme(str::Builder& theme) {
    theme.Append(StrL("<a:theme xmlns:a=\\\"http://schemas.openxmlformats.org/drawingml/2006/main\\\" name=\\\"SwiftLeaf\\\"><a:themeElements><a:clrScheme name=\\\"SwiftLeaf\\\">"));
    const char* names[]={"dk1","lt1","dk2","lt2","accent1","accent2","accent3","accent4","accent5","accent6","hlink","folHlink"};
    const char* colors[]={"000000","FFFFFF","333333","EEEEEE","24704B","336699","996633","663399","339999","999933","0563C1","954F72"};
    for(int i=0;i<dimof(names);i++) theme.Append(fmt("<a:%s><a:srgbClr val=\\\"%s\\\"/></a:%s>",Str(names[i]),Str(colors[i]),Str(names[i])));
    theme.Append(StrL("</a:clrScheme><a:fontScheme name=\\\"SwiftLeaf\\\"><a:majorFont><a:latin typeface=\\\"Arial\\\"/><a:ea typeface=\\\"\\\"/><a:cs typeface=\\\"\\\"/></a:majorFont><a:minorFont><a:latin typeface=\\\"Arial\\\"/><a:ea typeface=\\\"\\\"/><a:cs typeface=\\\"\\\"/></a:minorFont></a:fontScheme><a:fmtScheme name=\\\"SwiftLeaf\\\"><a:fillStyleLst>"));
    for(int i=0;i<3;i++) theme.Append(StrL("<a:solidFill><a:schemeClr val=\\\"phClr\\\"/></a:solidFill>"));
    theme.Append(StrL("</a:fillStyleLst><a:lnStyleLst>"));
    for(int i=0;i<3;i++) theme.Append(StrL("<a:ln w=\\\"12700\\\"><a:solidFill><a:schemeClr val=\\\"phClr\\\"/></a:solidFill><a:prstDash val=\\\"solid\\\"/></a:ln>"));
    theme.Append(StrL("</a:lnStyleLst><a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst><a:bgFillStyleLst>"));
    for(int i=0;i<3;i++) theme.Append(StrL("<a:solidFill><a:schemeClr val=\\\"phClr\\\"/></a:solidFill>"));
    theme.Append(StrL("</a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>"));
}

'''+s[a:]
s=s.replace('    str::Builder master, mr, layout, lr;','    str::Builder master, mr, layout, lr, theme;\n    SlideTheme(theme);')
s=s.replace('    mr.Append(StrL("</Relationships>"));','    Relationship(mr, 2, StrL("theme"), StrL("../theme/theme1.xml"));\n    mr.Append(StrL("</Relationships>"));',1)
s=s.replace('    ContentType(types, StrL("ppt/presentation.xml"),','    ContentType(types, StrL("ppt/theme/theme1.xml"), StrL("theme"));\n    ContentType(types, StrL("ppt/presentation.xml"),',1)
s=s.replace('    return Part(zip, StrL("ppt/presentation.xml"), pres)', '    return Part(zip, StrL("ppt/theme/theme1.xml"), theme) && Part(zip, StrL("ppt/presentation.xml"), pres)',1)
p.write_text(s)
# Avoid unrelated changelog reflow while keeping the new entry.
import subprocess
p=Path('CHANGELOG.md'); original=subprocess.check_output(['git','show','2a72b54^:CHANGELOG.md']).decode('utf-8')
current=p.read_text(encoding='utf-8'); start=current.index('## Unreleased'); end=current.index('\n## ',start+1); entry=current[start:end]
pos=original.index('\n')+1
p.write_text(original[:pos]+'\n'+entry+'\n'+original[pos:],encoding='utf-8')
p=Path('docs/test/export-fixture.py'); p.write_text(p.read_text(encoding='utf-8'),encoding='utf-8',newline='\n')
p=Path('.work-export-syntax.rsp'); s=p.read_text(encoding='utf-8'); s+='\n/W4\n/WX\n'+ '\n'.join('/wd'+n for n in '4127;4189;4324;4458;4522;4611;4800;6319;4100;4701;4702;4703;4706;6324;4302;4311;4838;4819'.split(';')); p.write_text(s,encoding='utf-8')
