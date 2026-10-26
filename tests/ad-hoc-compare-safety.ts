import { mkdtempSync, readFileSync, writeFileSync, existsSync, mkdirSync } from "node:fs";
import { join } from "node:path";
import { EXE, runStandalone, tmpPath } from "./util.ts";
function check(v: unknown, message: string) {
  if (!v) throw new Error(message);
}
function pdf(path: string, pages: string[], raw = false) {
  const objects = [
    "<< /Type /Catalog /Pages 2 0 R >>",
    `<< /Type /Pages /Count ${pages.length} /Kids [${pages.map((_, i) => `${4 + i * 2} 0 R`).join(" ")}] >>`,
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
  ];
  for (const text of pages) {
    const stream = raw ? text : `BT /F1 12 Tf 30 150 Td (${text}) Tj ET`;
    objects.push(
      `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 200] /Resources << /Font << /F1 3 0 R >> >> /Contents ${objects.length + 2} 0 R >>`,
      `<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`,
    );
  }
  let data = "%PDF-1.4\n";
  const offsets = [0];
  objects.forEach((o, i) => {
    offsets.push(data.length);
    data += `${i + 1} 0 obj\n${o}\nendobj\n`;
  });
  const xref = data.length;
  data +=
    `xref\n0 ${offsets.length}\n0000000000 65535 f \n` +
    offsets
      .slice(1)
      .map((n) => `${String(n).padStart(10, "0")} 00000 n \n`)
      .join("") +
    `trailer\n<< /Size ${offsets.length} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
  writeFileSync(path, data);
}
export async function testit() {
  const dir = mkdtempSync(tmpPath("compare-safety-"));
  const a = join(dir, "a.pdf"),
    b = join(dir, "b.pdf"),
    report = join(dir, "report.html");
  const run = (...args: string[]) => {
    const p = Bun.spawnSync([EXE, ...args], { stdout: "pipe", stderr: "pipe" });
    return { code: p.exitCode, json: JSON.parse(new TextDecoder().decode(p.stdout)) };
  };
  pdf(a, ["one old middle stable old end", "second page", "third page"]);
  pdf(b, ["inserted page", "one new middle stable new end", "third page", "second page"]);
  const compared = run("-compare", a, b, "-report", report);
  check(compared.code === 0 && compared.json.report === report, "compare failed or omitted report path");
  const html = readFileSync(report, "utf8");
  check(!html.includes("<del>old middle stable old</del>"), "separate edits swallowed unchanged middle");
  check(
    html.replace(/<[^>]*>/g, "").includes("inserted page") && html.includes("Left page 2 / Right page 4"),
    "inserted/moved pages not aligned",
  );
  check(
    html.includes("<del>old</del>") && html.includes("<ins>new</ins> middle stable"),
    "unchanged middle not preserved",
  );
  check(html.includes("Left page 1 / Right page 2"), "edited page consumed inserted page");
  check(run("-compare", a, b, "-report", join(dir, "report.pdf")).code === 0, "PDF report failed");
  check(readFileSync(join(dir, "report.pdf")).subarray(0, 5).toString() === "%PDF-", "invalid PDF report");
  writeFileSync(report, "KEEP");
  check(run("-compare", a, join(dir, "missing.pdf"), "-report", report).code !== 0, "load failure succeeded");
  check(readFileSync(report, "utf8") === "KEEP", "failed compare replaced report");
  check(run("-compare", a, b, "-report", dir).code !== 0, "unwritable report succeeded");
  const broken = join(dir, "broken.pdf");
  pdf(broken, ["q /Missing Do Q"], true);
  check(run("-compare", a, broken, "-report", report).code !== 0, "damaged page became empty text / zero change");
  check(readFileSync(report, "utf8") === "KEEP", "damaged page replaced existing report");
  const plan = join(dir, "plan.json");
  const invalid = [
    { actions: [] },
    { actions: [{ type: "unknown" }] },
    { actions: [{ type: "optimize", text: "typo" }] },
    { actions: [{ type: "export-text" }, { type: "rotate", value: 90 }] },
    { actions: [{ type: "rotate", value: "90" }] },
    { actions: [{ type: "rotate", value: 45 }] },
    { actions: [{ type: "ocr", typo: {} }] },
    { actions: [{ type: "ocr" }, {}] },
    { suffix: "", actions: [{ type: "ocr" }] },
  ];
  const before = readFileSync(a);
  for (const p of invalid) {
    writeFileSync(plan, JSON.stringify(p));
    check(run("-batch", plan, a).code !== 0, `accepted invalid plan ${JSON.stringify(p)}`);
    check(readFileSync(a).equals(before), "input changed");
  }
  writeFileSync(
    plan,
    JSON.stringify({ suffix: "_text", actions: [{ type: "rotate", value: 90 }, { type: "export-text" }] }),
  );
  const valid = run("-batch", plan, a);
  check(valid.code === 0 && valid.json.ok && valid.json.results[0].ok, "valid batch chain failed");
  check(readFileSync(join(dir, "a_text.txt"), "utf8").includes("middle stable"), "staged export lost text");
  for (const definition of [
    '{"actions":[{"type":"ocr","type":"rotate","value":90}]}',
    '{"suffix":"_ok\\u0000bad","actions":[{"type":"rotate","value":90}]}',
  ]) {
    writeFileSync(plan, definition);
    check(run("-batch", plan, a).code !== 0, "duplicate key or NUL accepted");
  }
  const out = join(dir, "a_batch.pdf");
  writeFileSync(out, "KEEP");
  writeFileSync(plan, JSON.stringify({ actions: [{ type: "rotate", value: 90 }] }));
  check(run("-batch", plan, a).code !== 0, "overwrote existing destination");
  check(readFileSync(out, "utf8") === "KEEP", "existing output changed");
  const sub = join(dir, "sub");
  mkdirSync(sub);
  const duplicate = join(sub, "a.pdf");
  pdf(duplicate, ["other"]);
  const outputFolder = join(dir, "outputs");
  writeFileSync(plan, JSON.stringify({ outputFolder, actions: [{ type: "rotate", value: 90 }] }));
  check(run("-batch", plan, a, duplicate).code !== 0, "basename collision accepted");
  check(!existsSync(join(outputFolder, "a_batch.pdf")), "collision wrote output before validation");
}
if (import.meta.main) await runStandalone(testit);
