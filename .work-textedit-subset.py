from pathlib import Path
import subprocess

# Preserve pre-existing mixed line endings in shared files.
def original(path):
    return subprocess.check_output(['git', 'show', 'fed9140^:' + path])

p = Path('src/libsumatrapdf.def')
b = original(str(p).replace('\\', '/'))
for anchor, extra in [(b'fz_encode_character', [b'fz_encode_character_with_fallback']),
                      (b'pdf_add_simple_font', [b'pdf_add_cid_font', b'pdf_font_writing_supported', b'pdf_subset_fonts'])]:
    lines=b.splitlines(keepends=True)
    for i,line in enumerate(lines):
        if line.strip()==anchor:
            ending=b'\r\n' if line.endswith(b'\r\n') else b'\n'
            lines[i]=line+b''.join(b'\t'+name+ending for name in extra)
            break
    b=b''.join(lines)
p.write_bytes(b)

p=Path('CHANGELOG.md')
b=original('CHANGELOG.md')
first=b.index(b'\n')+1
ending=b'\r\n' if b[:first].endswith(b'\r\n') else b'\n'
header='''# Changelog

## Unreleased

- Existing-text editing: Unicode Latin, Cyrillic, Greek and CJK with embedded,
  subset CID fonts, explicit ToUnicode maps, installed-family fallback and
  character-aware wrapping. Unsupported glyphs and block styles are rejected
  before source removal; failed edits keep the draft. See `docs/text-editing.md`
  for remaining shaping, style and object-editing limits.
- Review & Measurement tools (TASK W-REVIEW):
'''.encode().replace(b'\n',ending)
b=header+b[first:]
b=b.replace(b'g'+ending+ending+b'## Unreleased'+ending+ending,b'',1)
p.write_bytes(b)

p=Path('src/shared/EngineMupdf.cpp')
s=p.read_text()
start=s.index('bool EngineMupdfReplaceText(')
end=s.index('//--- Undo / redo',start)
r=s[start:end]
r=r.replace('fz_stext_page* source = nullptr;', '''fz_stext_page* source = nullptr;
        pdf_document* prepared = nullptr;
        pdf_obj* preparedPage = nullptr;
        pdf_obj* preparedForm = nullptr;
        pdf_graft_map* graft = nullptr;''')
r=r.replace('fz_var(source);','''fz_var(source);
        fz_var(prepared); fz_var(preparedPage); fz_var(preparedForm); fz_var(graft);''')
a=r.index('            // A nested journal operation')
b=r.index('            // Keep annotations intact:',a)
prep=r[a:b]
prep=prep[prep.index('            resources ='):]
prep=prep.replace('(ctx, doc,', '(ctx, prepared,')
prep='            prepared = pdf_create_document(ctx);\n'+prep
prep=prep.replace('form = pdf_new_xobject(ctx, prepared, media, fz_identity, resources, buffer);', '''preparedPage = pdf_add_page(ctx, prepared, media, 0, resources, buffer);
            pdf_insert_page(ctx, prepared, -1, preparedPage);
            pdf_subset_fonts(ctx, prepared, 0, nullptr);
            preparedForm = pdf_new_xobject(ctx, prepared, media, fz_identity, resources, buffer);''')
r=r[:a]+prep+r[b:]
a=r.index('            float inset =')
r=r[:a]+'''            // Only the prepared replacement's fonts are subset. Existing fonts
            // and shared resources are never changed by that operation.
            pdf_begin_operation(ctx, doc, "Replace text block");
            started = true;
            graft = pdf_new_graft_map(ctx, doc);
            form = pdf_graft_mapped_object(ctx, graft, preparedForm);
'''+r[a:]
r=r.replace('fz_drop_stext_page(ctx, source);', '''fz_drop_stext_page(ctx, source);
            pdf_drop_obj(ctx, preparedPage); pdf_drop_obj(ctx, preparedForm);
            pdf_drop_graft_map(ctx, graft);
            pdf_drop_document(ctx, prepared);''')
s=s[:start]+r+s[end:]
p.write_text(s)
