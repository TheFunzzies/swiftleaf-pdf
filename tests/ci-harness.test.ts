import { expect, test } from "bun:test";
import { existsSync } from "node:fs";
import { basename, join } from "node:path";
import { EXE, prepareTestEnvironment, ROOT, TESTS_TMP_DIR } from "./util.ts";

test("defaults to the Swiftleaf debug build", () => {
  expect(basename(EXE)).toBe("Swiftleaf.exe");
});

test("copies the app and its linker-named PDB", () => {
  prepareTestEnvironment();
  expect(existsSync(join(TESTS_TMP_DIR, "Swiftleaf.exe"))).toBe(true);
  expect(existsSync(join(TESTS_TMP_DIR, "SumatraPDF.pdb"))).toBe(true);
});

test("accepts Swiftleaf ProductName with -exe", async () => {
  const proc = Bun.spawn(
    [process.execPath, "-e", 'await import("./tests/util.ts")', "-exe", join(ROOT, "out", "dbg64", "Swiftleaf.exe")],
    { cwd: ROOT, stdout: "pipe", stderr: "pipe" },
  );
  const [stdout, stderr, code] = await Promise.all([
    new Response(proc.stdout).text(),
    new Response(proc.stderr).text(),
    proc.exited,
  ]);
  expect({ code, output: stdout + stderr }).toEqual({ code: 0, output: "" });
});
