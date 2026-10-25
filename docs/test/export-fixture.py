"""Regenerate with python docs/test/export-fixture.py (no dependencies)."""
from pathlib import Path
objects = []
def obj(data):
    objects.append(data.encode('ascii') if isinstance(data, str) else data)
def stream(data):
    return b'<< /Length '+str(len(data)).encode()+b' >>\nstream\n'+data+b'\nendstream'
obj('<< /Type /Catalog /Pages 2 0 R >>')
obj('<< /Type /Pages /Count 2 /Kids [3 0 R 4 0 R] >>')
obj('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> /XObject << /Im1 8 0 R >> >> /Contents 6 0 R /Annots [9 0 R] >>')
obj('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 7 0 R >>')
obj('<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
obj(stream(b'''BT /F1 24 Tf 50 740 Td (Export fixture) Tj ET
BT /F1 12 Tf 50 710 Td (Hello & <world>) Tj ET
BT /F1 12 Tf 50 690 Td (Visit example) Tj ET
BT /F1 12 Tf 50 665 Td (- List item) Tj ET
0.5 w 50 500 m 350 500 l S 50 550 m 350 550 l S 50 600 m 350 600 l S
50 500 m 50 600 l S 200 500 m 200 600 l S 350 500 m 350 600 l S
BT /F1 12 Tf 60 575 Td (Item) Tj ET
BT /F1 12 Tf 210 575 Td (Qty) Tj ET
BT /F1 12 Tf 60 525 Td (Apple) Tj ET
BT /F1 12 Tf 210 525 Td (=1+1) Tj ET
q 40 0 0 40 50 400 cm /Im1 Do Q
'''))
obj(stream(b'BT /F1 12 Tf 50 740 Td (Second page only) Tj ET\n'))
obj(b'<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 12 >>\nstream\n'+bytes([255,0,0,0,255,0,0,0,255,255,255,255])+b'\nendstream')
obj('<< /Type /Annot /Subtype /Link /Rect [50 686 150 706] /A << /S /URI /URI (https://example.com/?a=1&b=2) >> >>')
data = bytearray(b'%PDF-1.7\n')
offsets=[]
for i, value in enumerate(objects, 1):
    offsets.append(len(data)); data += f'{i} 0 obj\n'.encode()+value+b'\nendobj\n'
xref=len(data)
data += f'xref\n0 {len(objects)+1}\n0000000000 65535 f \n'.encode()
for off in offsets: data+=f'{off:010d} 00000 n \n'.encode()
data+=f'trailer\n<< /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
Path(__file__).with_suffix('.pdf').write_bytes(data)
