// Write a small multi-page sample PDF (text, headings, a table, shapes) used for
// manual testing and screenshots.  bun tools/swiftleaf/make-sample.ts out.pdf
import { writeFileSync } from "node:fs";

const out = process.argv[2] ?? "docs/test/swiftleaf-sample.pdf";

const lorem =
  "Swiftleaf is a fast, lightweight PDF reader and editor for Windows. It opens large documents " +
  "instantly, renders crisply at any zoom level and lets you highlight, comment, fill forms, sign " +
  "and reorganize pages without leaving the app. This sample document exists so the interface can " +
  "be exercised with realistic content: paragraphs of running text, headings, a simple table and a " +
  "few shapes.";

function esc(s: string): string {
  return s.replace(/\\/g, "\\\\").replace(/\(/g, "\\(").replace(/\)/g, "\\)");
}

function wrap(text: string, maxChars: number): string[] {
  const words = text.split(" ");
  const lines: string[] = [];
  let cur = "";
  for (const w of words) {
    if ((cur + " " + w).trim().length > maxChars) {
      lines.push(cur.trim());
      cur = w;
    } else {
      cur += " " + w;
    }
  }
  if (cur.trim()) lines.push(cur.trim());
  return lines;
}

function pageContent(n: number, total: number): string {
  const ops: string[] = [];
  // header band
  ops.push("0.06 0.48 0.42 rg 0 762 612 30 re f");
  ops.push(`BT /F2 12 Tf 1 1 1 rg 50 772 Td (${esc("Swiftleaf PDF - Sample Document")}) Tj ET`);
  ops.push(`BT /F2 22 Tf 0.1 0.1 0.1 rg 50 715 Td (${esc(`Chapter ${n}: Section Title`)}) Tj ET`);
  let y = 680;
  for (let p = 0; p < 3; p++) {
    for (const line of wrap(lorem, 92)) {
      ops.push(`BT /F1 11 Tf 0.15 0.15 0.15 rg 50 ${y} Td (${esc(line)}) Tj ET`);
      y -= 15;
    }
    y -= 12;
  }
  // table
  ops.push(`BT /F2 14 Tf 0.1 0.1 0.1 rg 50 ${y} Td (Feature overview) Tj ET`);
  y -= 22;
  const rows = [
    ["Feature", "Status", "Notes"],
    ["Annotations", "Ready", "Highlight, notes, shapes, ink"],
    ["Forms", "Ready", "Fill and save AcroForms"],
    ["Signatures", "Ready", "Image and digital signatures"],
    ["Organize", "Ready", "Insert, delete, rotate, reorder"],
  ];
  for (let r = 0; r < rows.length; r++) {
    const fill = r === 0 ? "0.86 0.93 0.92" : r % 2 ? "1 1 1" : "0.96 0.96 0.96";
    ops.push(`${fill} rg 50 ${y - 6} 512 20 re f`);
    ops.push(`0.8 0.8 0.8 RG 0.5 w 50 ${y - 6} 512 20 re S`);
    const font = r === 0 ? "/F2" : "/F1";
    ops.push(`BT ${font} 10 Tf 0.1 0.1 0.1 rg 58 ${y} Td (${esc(rows[r][0])}) Tj ET`);
    ops.push(`BT ${font} 10 Tf 0.1 0.1 0.1 rg 220 ${y} Td (${esc(rows[r][1])}) Tj ET`);
    ops.push(`BT ${font} 10 Tf 0.1 0.1 0.1 rg 330 ${y} Td (${esc(rows[r][2])}) Tj ET`);
    y -= 20;
  }
  // shapes
  y -= 30;
  ops.push(`0.93 0.42 0.16 rg 80 ${y - 60} 120 60 re f`);
  ops.push(`0.2 0.45 0.8 rg 250 ${y - 60} m 310 ${y} l 370 ${y - 60} l f`);
  ops.push(`0.1 0.6 0.35 rg 430 ${y - 30} m 430 ${y - 13.4} 443.4 ${y} 460 ${y} c 476.6 ${y} 490 ${y - 13.4} 490 ${y - 30} c 490 ${y - 46.6} 476.6 ${y - 60} 460 ${y - 60} c 443.4 ${y - 60} 430 ${y - 46.6} 430 ${y - 30} c f`);
  // footer
  ops.push(`BT /F1 9 Tf 0.45 0.45 0.45 rg 280 30 Td (Page ${n} of ${total}) Tj ET`);
  return ops.join("\n");
}

const nPages = 6;
const objs: string[] = [];
const add = (s: string) => {
  objs.push(s);
  return objs.length;
};
const catalog = add("");
const pages = add("");
const f1 = add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
const f2 = add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
const kids: number[] = [];
for (let i = 1; i <= nPages; i++) {
  const content = pageContent(i, nPages);
  const c = add(`<< /Length ${content.length} >>\nstream\n${content}\nendstream`);
  kids.push(
    add(
      `<< /Type /Page /Parent ${pages} 0 R /MediaBox [0 0 612 792] /Contents ${c} 0 R /Resources << /Font << /F1 ${f1} 0 R /F2 ${f2} 0 R >> >> >>`,
    ),
  );
}
objs[catalog - 1] = `<< /Type /Catalog /Pages ${pages} 0 R >>`;
objs[pages - 1] = `<< /Type /Pages /Kids [${kids.map((k) => `${k} 0 R`).join(" ")}] /Count ${nPages} >>`;

let pdf = "%PDF-1.7\n";
const offsets: number[] = [];
objs.forEach((o, i) => {
  offsets.push(pdf.length);
  pdf += `${i + 1} 0 obj\n${o}\nendobj\n`;
});
const xref = pdf.length;
pdf += `xref\n0 ${objs.length + 1}\n0000000000 65535 f \n`;
for (const off of offsets) pdf += `${String(off).padStart(10, "0")} 00000 n \n`;
pdf += `trailer\n<< /Size ${objs.length + 1} /Root ${catalog} 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
writeFileSync(out, pdf, "latin1");
console.log(`wrote ${out}`);
