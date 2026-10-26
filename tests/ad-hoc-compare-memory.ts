// Synthetic scanned-document working-set probe. Run explicitly; writes large reports under tests/tmp.
import { deflateSync } from "node:zlib";
import { writeFileSync, readFileSync, mkdtempSync, statSync } from "node:fs";
import { join } from "node:path";
import { EXE, tmpPath, runStandalone } from "./util.ts";

function scannedPdf(path: string, pages: number) {
  const width = 600,
    height = 800;
  const image = Buffer.alloc(width * height * 3);
  let state = 42;
  for (let i = 0; i < image.length; i++) {
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    image[i] = state & 255;
  }
  const compressed = deflateSync(image);
  const objects: Buffer[] = [
    Buffer.from("<< /Type /Catalog /Pages 2 0 R >>"),
    Buffer.from(
      `<< /Type /Pages /Count ${pages} /Kids [${Array.from({ length: pages }, (_, i) => `${4 + i * 2} 0 R`).join(" ")}] >>`,
    ),
    Buffer.concat([
      Buffer.from(
        `<< /Type /XObject /Subtype /Image /Width ${width} /Height ${height} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /Length ${compressed.length} >>\nstream\n`,
      ),
      compressed,
      Buffer.from("\nendstream"),
    ]),
  ];
  for (let i = 0; i < pages; i++) {
    const stream = "q 600 0 0 800 0 0 cm /Im Do Q";
    objects.push(
      Buffer.from(
        `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /XObject << /Im 3 0 R >> >> /Contents ${objects.length + 2} 0 R >>`,
      ),
      Buffer.from(`<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`),
    );
  }
  const parts = [Buffer.from("%PDF-1.4\n")],
    offsets = [0];
  let size = parts[0].length;
  objects.forEach((obj, i) => {
    offsets.push(size);
    const part = Buffer.concat([Buffer.from(`${i + 1} 0 obj\n`), obj, Buffer.from("\nendobj\n")]);
    parts.push(part);
    size += part.length;
  });
  parts.push(
    Buffer.from(
      `xref\n0 ${offsets.length}\n0000000000 65535 f \n${offsets
        .slice(1)
        .map((x) => `${String(x).padStart(10, "0")} 00000 n \n`)
        .join("")}trailer\n<< /Size ${offsets.length} /Root 1 0 R >>\nstartxref\n${size}\n%%EOF\n`,
    ),
  );
  writeFileSync(path, Buffer.concat(parts));
}
export async function testit() {
  const dir = mkdtempSync(tmpPath("compare-memory-"));
  const results = [];
  for (const pages of [16, 80]) {
    const pdf = join(dir, `scan-${pages}.pdf`),
      report = join(dir, `report-${pages}.html`);
    scannedPdf(pdf, pages);
    // Paths travel as environment data, never interpolated shell code.
    const script = `$sw=[Diagnostics.Stopwatch]::StartNew(); $p=Start-Process -FilePath $env:COMPARE_EXE -ArgumentList @('-compare', ('"'+$env:COMPARE_PDF+'"'), ('"'+$env:COMPARE_PDF+'"'), '-report', ('"'+$env:COMPARE_REPORT+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput ($env:COMPARE_REPORT+'.json') -RedirectStandardError ($env:COMPARE_REPORT+'.err'); $handle=$p.Handle; $peak=0L; while (!$p.HasExited) { $p.Refresh(); $peak=[Math]::Max($peak,$p.PeakWorkingSet64); Start-Sleep -Milliseconds 100 }; $p.WaitForExit(); @{pages=[int]$env:COMPARE_PAGES; seconds=$sw.Elapsed.TotalSeconds; peakMiB=$peak/1MB; exitCode=$p.ExitCode}|ConvertTo-Json -Compress`;
    const p = Bun.spawn(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script], {
      env: { ...process.env, COMPARE_EXE: EXE, COMPARE_PDF: pdf, COMPARE_REPORT: report, COMPARE_PAGES: String(pages) },
      stdout: "pipe",
      stderr: "pipe",
    });
    const output = await new Response(p.stdout).text();
    const error = await new Response(p.stderr).text();
    if (await p.exited) throw new Error(error);
    const result = JSON.parse(output);
    if ((result.exitCode !== null && result.exitCode !== 0) || !JSON.parse(readFileSync(report + ".json", "utf8")).ok)
      throw new Error(`compare failed: ${output}`);
    result.reportMiB = statSync(report).size / (1024 * 1024);
    results.push(result);
    console.log(JSON.stringify(result));
  }
  writeFileSync(join(dir, "measurements.json"), JSON.stringify(results, null, 2));
  console.log(`Measurements: ${dir}`);
}
if (import.meta.main) await runStandalone(testit);
