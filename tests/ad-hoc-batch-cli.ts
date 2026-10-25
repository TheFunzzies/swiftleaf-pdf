import { copyFileSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { EXE, ROOT, runStandalone, tmpPath } from "./util.ts";

function check(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

function run(args: string[]) {
  const proc = Bun.spawnSync([EXE, ...args], { stdout: "pipe", stderr: "pipe" });
  return {
    code: proc.exitCode,
    stdout: new TextDecoder().decode(proc.stdout),
    stderr: new TextDecoder().decode(proc.stderr),
  };
}

export async function testit(): Promise<void> {
  const input = tmpPath("batch-cli-input.pdf");
  const actions = tmpPath("batch-cli-actions.json");
  const output = tmpPath("batch-cli-input_batch.pdf");
  const report = tmpPath("batch-cli-compare.html");
  copyFileSync(join(ROOT, "tests", "issue-1846.stripped.pdf"), input);
  writeFileSync(actions, JSON.stringify({ actions: [{ type: "watermark", text: "BATCH-CLI" }] }));

  const batch = run(["-batch", actions, input]);
  check(batch.code === 0, `batch exit ${batch.code}: ${batch.stderr}`);
  const result = JSON.parse(batch.stdout);
  check(result.ok && result.results[0].ok, "batch JSON did not report success");
  check(existsSync(output), "batch output PDF was not created");

  const compare = run(["-compare", input, output, "-report", report]);
  check(compare.code === 0, `compare exit ${compare.code}: ${compare.stderr}`);
  check(JSON.parse(compare.stdout).ok, "compare JSON did not report success");
  check(readFileSync(report, "utf8").includes("PDF comparison"), "HTML report was not written");
}

if (import.meta.main) {
  await runStandalone(testit);
}
