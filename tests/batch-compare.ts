import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { EXE, ROOT, runStandalone, tmpPath } from "./util.ts";

const LEFT = join(ROOT, "tests", "issue-1189.pdf");
const RIGHT = join(ROOT, "tests", "issue-1809.pdf");

function check(ok: boolean, message: string): void {
  if (!ok) throw new Error(message);
}

function run(args: string[]): { code: number; stdout: string; stderr: string } {
  const proc = Bun.spawnSync({ cmd: [EXE, ...args], cwd: ROOT, stdout: "pipe", stderr: "pipe" });
  return { code: proc.exitCode, stdout: proc.stdout.toString(), stderr: proc.stderr.toString() };
}

export async function testit(): Promise<void> {
  check(existsSync(LEFT) && existsSync(RIGHT), "batch/compare PDF fixtures are missing");
  const outputDir = tmpPath("batch-compare");
  mkdirSync(outputDir, { recursive: true });
  const actionsPath = tmpPath("batch-actions.json");
  const batchOutput = join(outputDir, "issue-1189_batch.pdf");
  writeFileSync(
    actionsPath,
    JSON.stringify({
      outputFolder: outputDir,
      suffix: "_batch",
      actions: [
        { type: "rotate", pages: "all", value: 90 },
        { type: "optimize", text: "web" },
      ],
    }),
  );
  const batch = run(["-batch", actionsPath, LEFT]);
  check(batch.code === 0, `-batch failed: ${batch.stdout.trim()} ${batch.stderr.trim()}`);
  const batchResult = JSON.parse(batch.stdout) as {
    ok?: boolean;
    results?: Array<{ output?: string; ok?: boolean; sizeBefore?: number; sizeAfter?: number }>;
  };
  check(batchResult.ok === true && batchResult.results?.[0]?.ok === true, `-batch result invalid: ${batch.stdout}`);
  check((batchResult.results?.[0]?.sizeBefore ?? 0) > 0, `optimizer audit missing: ${batch.stdout}`);
  check(existsSync(batchOutput), `batch output missing: ${batchOutput}`);
  check(statSync(batchOutput).size > 0, `batch output is empty: ${batchOutput}`);

  const reportPath = tmpPath("compare-report.html");
  const compare = run(["-compare", LEFT, RIGHT, "-report", reportPath]);
  check(compare.code === 0, `-compare failed: ${compare.stdout.trim()} ${compare.stderr.trim()}`);
  const compareResult = JSON.parse(compare.stdout) as { ok?: boolean; pages?: number };
  check(compareResult.ok === true && (compareResult.pages ?? 0) > 0, `-compare result invalid: ${compare.stdout}`);
  const report = readFileSync(reportPath, "utf8");
  check(
    report.includes("Pixel difference heatmap") && report.includes("Word changes") && report.includes("<svg"),
    "compare HTML report omitted the visual or word diff",
  );
}

if (import.meta.main) {
  await runStandalone(testit);
}
