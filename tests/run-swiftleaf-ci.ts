// Bounded Swiftleaf checks; each case gets a fresh process and portable app.
import { appendFileSync, cpSync, existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";

const root = resolve(import.meta.dir, "..");
const resultsDir = join(root, ".work", "swiftleaf-ci");
const timeoutMs = 120_000;
const smokeTests = [
  "free-text-annotation-placement",
  "free-text-custom-style",
  "free-text-draft",
  "free-text-edit-matches-render",
  "free-text-edit-toolbar",
  "free-text-in-place-edit",
  "free-text-transparent",
  "annot-undo-redo",
  "annot-undo-one-step",
  "annot-moveable-types",
  "pdf-edit-toolbar-interaction",
  "redact-annotations",
  "sign-with-image",
  "tool-merge",
  "edit-safety",
  "annot-copy-paste",
  "annot-cut-paste",
  "annot-nudge",
  "annot-color-dropdown",
  "annot-contents-click-away",
  "annot-filter-close-tab",
  "annot-filter-close-window",
  "annot-filter-syntax",
  "annot-filter-toolbar",
  "annot-list-placement",
];

// Keep observed baseline failures visible; a different failure still fails CI.
const knownFailures: Record<string, { reason: string; pattern: RegExp }> = {};

async function runCase(name: string): Promise<void> {
  const { prepareTestEnvironment, runTest } = await import("./util.ts");
  const { hasInteractiveDesktop, setTestWindowLayout } = await import("./winapi.ts");
  if (!hasInteractiveDesktop()) throw new Error("CI needs an interactive desktop for GUI tests");
  setTestWindowLayout("workArea");
  prepareTestEnvironment();
  const { testit } = await import(`./${name}.ts`);
  await runTest(name, testit);
}

async function snapshot(path: string): Promise<void> {
  try {
    const { captureWindowToPng, getForegroundWindow } = await import("./winapi.ts");
    captureWindowToPng(getForegroundWindow(), path);
  } catch (e) {
    console.error(`screenshot: ${e}`);
  }
}

async function main(): Promise<void> {
  const requested = process.argv.slice(2);
  const names = requested.length ? requested : smokeTests;
  for (const name of names) {
    if (!smokeTests.includes(name)) throw new Error(`unknown Swiftleaf smoke test: ${name}`);
  }
  mkdirSync(resultsDir, { recursive: true });
  const rows: string[] = [];
  let failures = 0;
  for (const name of names) {
    const dir = join(resultsDir, name);
    mkdirSync(dir, { recursive: true });
    const args = name === "edit-safety" ? [join(import.meta.dir, `${name}.ts`)] : [import.meta.path, "--case", name];
    const started = performance.now();
    const proc = Bun.spawn([process.execPath, ...args], {
      cwd: root,
      env: { ...process.env, SUMATRA_TEST_EXE: join(root, "out", "dbg64", "Swiftleaf.exe") },
      stdout: "pipe",
      stderr: "pipe",
    });
    let timedOut = false;
    const timer = setTimeout(async () => {
      timedOut = true;
      await snapshot(join(dir, "timeout.png"));
      // Terminate the app and compiler too, before the next case starts.
      const kill = Bun.spawn(["taskkill", "/PID", String(proc.pid), "/T", "/F"], {
        stdout: "ignore",
        stderr: "ignore",
      });
      await kill.exited;
      proc.kill();
    }, timeoutMs);
    const [stdout, stderr, code] = await Promise.all([
      new Response(proc.stdout).text(),
      new Response(proc.stderr).text(),
      proc.exited,
    ]);
    clearTimeout(timer);
    const output = stdout + stderr;
    writeFileSync(join(dir, "output.txt"), output);
    const elapsed = ((performance.now() - started) / 1000).toFixed(1);
    const known = knownFailures[name];
    const expected = !timedOut && code !== 0 && known?.pattern.test(output);
    const status = timedOut
      ? "TIMEOUT"
      : expected
        ? "KNOWN FAILURE"
        : code !== 0
          ? "FAIL"
          : known
            ? "UNEXPECTED PASS"
            : "PASS";
    if (status !== "PASS" && status !== "KNOWN FAILURE") failures++;
    if (code !== 0 || timedOut) {
      await snapshot(join(dir, "failure.png"));
      const scratch = join(root, ".work", "tests-tmp", "tmp");
      if (existsSync(scratch)) cpSync(scratch, join(dir, "files"), { recursive: true });
      console.error(output);
    }
    const note = known ? known.reason : "";
    console.log(`${status} ${name} (${elapsed}s) ${note}`);
    rows.push(`| ${name} | ${status} | ${elapsed}s | ${note} |`);
  }
  const summary = [
    "## Swiftleaf smoke tests",
    "",
    "| Test | Result | Time | Baseline reason |",
    "| --- | --- | --- | --- |",
    ...rows,
    "",
  ].join("\n");
  writeFileSync(join(resultsDir, "results.md"), summary);
  if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, summary);
  if (failures) throw new Error(`${failures} Swiftleaf smoke test(s) failed`);
}

if (import.meta.main) {
  try {
    if (process.argv[2] === "--case") {
      const name = process.argv[3]!;
      if (!smokeTests.includes(name) || name === "edit-safety") throw new Error(`invalid case: ${name}`);
      await runCase(name);
    } else {
      await main();
    }
  } catch (e) {
    console.error(e);
    process.exitCode = 1;
  }
}
