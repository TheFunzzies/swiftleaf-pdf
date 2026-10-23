import { expect, test } from "bun:test";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { basename, join } from "node:path";
import { EXE, prepareTestEnvironment, ROOT, TESTS_TMP_DIR } from "./util.ts";

test("defaults to the Swiftleaf debug build", () => {
  expect(basename(EXE)).toBe("Swiftleaf.exe");
});

test("copies the app and its PDB", () => {
  prepareTestEnvironment();
  expect(existsSync(join(TESTS_TMP_DIR, "Swiftleaf.exe"))).toBe(true);
  expect(existsSync(join(TESTS_TMP_DIR, "Swiftleaf.pdb"))).toBe(true);
});

test("accepts Swiftleaf ProductName with -exe", async () => {
  const probe = join(ROOT, ".work", "ci-harness-probe.ts");
  mkdirSync(join(ROOT, ".work"), { recursive: true });
  writeFileSync(probe, 'import "../tests/util.ts";\n');
  const proc = Bun.spawn([process.execPath, probe, "-exe", join(ROOT, "out", "dbg64", "Swiftleaf.exe")], {
    cwd: ROOT,
    stdout: "pipe",
    stderr: "pipe",
  });
  const [stdout, stderr, code] = await Promise.all([
    new Response(proc.stdout).text(),
    new Response(proc.stderr).text(),
    proc.exited,
  ]);
  expect({ code, output: stdout + stderr }).toEqual({ code: 0, output: "" });
}, 30000); // PowerShell cold start can exceed bun's 5s default on CI runners

test("build docs load without optional CDN dependencies", async () => {
  const proc = Bun.spawn([process.execPath, "cmd/gen-docs.ts"], { cwd: ROOT, stdout: "pipe", stderr: "pipe" });
  const [stdout, stderr, code] = await Promise.all([
    new Response(proc.stdout).text(),
    new Response(proc.stderr).text(),
    proc.exited,
  ]);
  if (code !== 0) throw new Error(stdout + stderr);
  expect(code).toBe(0);
});
