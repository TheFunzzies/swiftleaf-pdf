import { expect, test } from "bun:test";
import { mkdtemp, mkdir, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

test("release gate: 10% boundary, growth failure, missing measurements advisory", async () => {
  const dir = await mkdtemp(join(tmpdir(), "swiftleaf-gate-"));
  try {
    const dist = join(dir, "dist");
    await mkdir(dist);
    const baseline = join(dir, "baseline.json");
    const report = join(dir, "report.md");
    await writeFile(baseline, JSON.stringify({ source: "test", assets: { "test.exe": 100 } }));
    for (const [size, expected] of [
      [110, 0],
      [111, 1],
    ] as const) {
      await writeFile(join(dist, "test.exe"), Buffer.alloc(size));
      const result = Bun.spawnSync([
        "pwsh",
        "-NoProfile",
        "-File",
        resolve("tools/release-gate.ps1"),
        "-Dist",
        dist,
        "-Baseline",
        baseline,
        "-Report",
        report,
      ]);
      expect(result.exitCode).toBe(expected);
      expect(await readFile(report, "utf8")).toContain(`${size} bytes`);
    }
    await rm(join(dist, "test.exe"));
    const result = Bun.spawnSync([
      "pwsh",
      "-NoProfile",
      "-File",
      resolve("tools/release-gate.ps1"),
      "-Dist",
      dist,
      "-Baseline",
      baseline,
      "-Report",
      report,
    ]);
    expect(result.exitCode).toBe(0);
    expect(await readFile(report, "utf8")).toContain("unavailable");
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
}, 60000);
