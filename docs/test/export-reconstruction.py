"""Regenerate with python docs/test/export-reconstruction.py (no dependencies)."""
from pathlib import Path

objects = []


def obj(data):
    objects.append(data.encode("ascii") if isinstance(data, str) else data)


def stream(data):
    return b"<< /Length " + str(len(data)).encode() + b" >>\nstream\n" + data + b"\nendstream"


obj("<< /Type /Catalog /Pages 2 0 R >>")
obj("<< /Type /Pages /Count 1 /Kids [3 0 R] >>")
obj(
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
    "/Resources << /Font << /F1 5 0 R /F2 6 0 R /F3 7 0 R >> >> /Contents 4 0 R >>"
)
obj(
    stream(
        b"""BT /F1 20 Tf 50 750 Td (Reconstruction) Tj ET
BT /F1 12 Tf 50 700 Td (Left one) Tj ET
BT /F1 12 Tf 50 680 Td (Left two) Tj ET
BT /F1 12 Tf 320 700 Td (Right one) Tj ET
BT /F1 12 Tf 320 680 Td (Right two) Tj ET
BT /F2 12 Tf 50 640 Td (Bold) Tj ET
BT /F3 12 Tf 90 640 Td (Italic) Tj ET
1 0 0 rg BT /F1 12 Tf 135 640 Td (Red) Tj ET 0 0 0 rg
0.5 w 50 400 m 350 400 l S 50 450 m 350 450 l S 50 500 m 350 500 l S 50 550 m 350 550 l S
50 400 m 50 550 l S 350 400 m 350 550 l S
150 400 m 150 450 l S 250 400 m 250 500 l S
BT /F1 12 Tf 60 525 Td (Merged Header) Tj ET
BT /F1 12 Tf 60 475 Td (Span Two) Tj ET
BT /F1 12 Tf 260 475 Td (42) Tj ET
BT /F1 12 Tf 60 425 Td (A) Tj ET
BT /F1 12 Tf 160 425 Td (12.50) Tj ET
BT /F1 12 Tf 260 425 Td (=1+1) Tj ET
0.5 w 400 400 m 550 400 l S 400 450 m 550 450 l S 400 500 m 550 500 l S
400 400 m 400 500 l S 475 400 m 475 500 l S 550 400 m 550 500 l S
BT /F1 12 Tf 410 475 Td (Second) Tj ET
BT /F1 12 Tf 485 475 Td (7) Tj ET
BT /F1 12 Tf 410 425 Td (Table) Tj ET
BT /F1 12 Tf 485 425 Td (8) Tj ET
BT /F1 12 Tf 50 330 Td (Name) Tj ET
BT /F1 12 Tf 200 330 Td (Qty) Tj ET
BT /F1 12 Tf 50 310 Td (Beta) Tj ET
BT /F1 12 Tf 200 310 Td (2) Tj ET
BT /F1 12 Tf 50 290 Td (Gamma) Tj ET
BT /F1 12 Tf 200 290 Td (3) Tj ET
"""
    )
)
obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>")
obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique >>")
data = bytearray(b"%PDF-1.7\n")
offsets = []
for i, value in enumerate(objects, 1):
    offsets.append(len(data))
    data += f"{i} 0 obj\n".encode() + value + b"\nendobj\n"
xref = len(data)
data += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode()
for off in offsets:
    data += f"{off:010d} 00000 n \n".encode()
data += (
    f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
)
Path(__file__).with_suffix(".pdf").write_bytes(data)
