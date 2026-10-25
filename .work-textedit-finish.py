from pathlib import Path
import subprocess
p=Path('CHANGELOG.md')
b=subprocess.check_output(['git','show','fed9140^:CHANGELOG.md'])
e=b'\r\n' if b.splitlines(keepends=True)[0].endswith(b'\r\n') else b'\n'
intro='''# Changelog

## Unreleased

- Existing-text editing: Unicode Latin, Cyrillic, Greek and CJK with embedded,
  subset CID fonts, explicit ToUnicode maps, installed-family fallback and
  character-aware wrapping. Unsupported glyphs and block styles are rejected
  before source removal; failed edits keep the draft. See `docs/text-editing.md`
  for remaining shaping, style and object-editing limits.
- Review & Measurement tools (TASK W-REVIEW):
'''.encode().replace(b'\n',e)
b=intro+b[b.index(b'\n')+1:]
b=b.replace(e+b'g'+e+e+b'## Unreleased'+e+e,e+e,1)
p.write_bytes(b)
for f in ['src/shared/EngineMupdf.cpp','src/EditText.cpp']:
    p=Path(f); s=p.read_text().replace('mixed styles, rotation, transparency','mixed styles, spacing, rotation, transparency'); p.write_text(s)
