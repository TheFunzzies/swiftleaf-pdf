import { existsSync, readFileSync, writeFileSync } from "fs";
import { join } from "path";
import { checkReleaseSizes } from "../tools/check-release-size";
import { runStandalone } from "./util";

export async function testit(): Promise<void> {
  const root = join(import.meta.dir, "..");
  const baselinePath = join(root, "tools", "release-size-baseline.json");

  if (!existsSync(baselinePath)) {
    throw new Error("release-size-baseline.json missing");
  }

  const raw = readFileSync(baselinePath, "utf8");
  const parsed = JSON.parse(raw);
  if (!parsed.files || !parsed.files["Swiftleaf.exe"]) {
    throw new Error("baseline files missing Swiftleaf.exe");
  }

  // 1. Current sizes match or are within baseline
  const okPass = checkReleaseSizes();
  if (!okPass) {
    throw new Error("expected checkReleaseSizes to pass for current build");
  }

  // 2. Simulated +5% growth: should warn but pass (return true)
  const origFiles = { ...parsed.files };
  try {
    const smallerFiles: Record<string, number> = {};
    for (const [k, v] of Object.entries(origFiles)) {
      smallerFiles[k] = Math.round(Number(v) * 0.95); // 5% growth
    }
    parsed.files = smallerFiles;
    writeFileSync(baselinePath, JSON.stringify(parsed, null, 2), "utf8");

    const okWarn = checkReleaseSizes();
    if (!okWarn) {
      throw new Error("expected 5% growth to pass with warning, not fail");
    }

    // 3. Simulated +15% growth: should fail (return false)
    const muchSmallerFiles: Record<string, number> = {};
    for (const [k, v] of Object.entries(origFiles)) {
      muchSmallerFiles[k] = Math.round(Number(v) * 0.85); // 17.6% growth (>10%)
    }
    parsed.files = muchSmallerFiles;
    writeFileSync(baselinePath, JSON.stringify(parsed, null, 2), "utf8");

    const okFail = checkReleaseSizes();
    if (okFail) {
      throw new Error("expected >10% growth to fail");
    }
  } finally {
    parsed.files = origFiles;
    writeFileSync(baselinePath, JSON.stringify(parsed, null, 2) + "\n", "utf8");
  }
}

if (import.meta.main) {
  await runStandalone(testit);
}
