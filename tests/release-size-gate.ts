import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "fs";
import { join } from "path";
import { checkReleaseSizes } from "../tools/check-release-size";
import { runStandalone, TESTS_TMP_DIR } from "./util";

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

  // Create isolated temp directory with mock binaries matching baseline
  const testDir = join(TESTS_TMP_DIR, "release-size-test");
  mkdirSync(testDir, { recursive: true });

  try {
    const baseExeSize = parsed.files["Swiftleaf.exe"];
    const baseInstallSize = parsed.files["Swiftleaf-install.exe"] ?? baseExeSize;

    // 1. Files match baseline exactly -> passes
    writeFileSync(join(testDir, "Swiftleaf.exe"), Buffer.alloc(baseExeSize));
    writeFileSync(join(testDir, "Swiftleaf-install.exe"), Buffer.alloc(baseInstallSize));

    const okPass = checkReleaseSizes(testDir);
    if (!okPass) {
      throw new Error("expected checkReleaseSizes to pass for exact baseline sizes");
    }

    // 2. Simulated +5% growth: should warn but pass (return true)
    const warnExeSize = Math.round(baseExeSize * 1.05);
    writeFileSync(join(testDir, "Swiftleaf.exe"), Buffer.alloc(warnExeSize));
    writeFileSync(join(testDir, "Swiftleaf-install.exe"), Buffer.alloc(warnExeSize));

    const okWarn = checkReleaseSizes(testDir);
    if (!okWarn) {
      throw new Error("expected 5% growth to pass with warning, not fail");
    }

    // 3. Simulated +15% growth: should fail (return false)
    const failExeSize = Math.round(baseExeSize * 1.15);
    writeFileSync(join(testDir, "Swiftleaf.exe"), Buffer.alloc(failExeSize));
    writeFileSync(join(testDir, "Swiftleaf-install.exe"), Buffer.alloc(failExeSize));

    const okFail = checkReleaseSizes(testDir);
    if (okFail) {
      throw new Error("expected >10% growth to fail");
    }
  } finally {
    rmSync(testDir, { recursive: true, force: true });
  }
}

if (import.meta.main) {
  await runStandalone(testit);
}
