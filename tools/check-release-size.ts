import { existsSync, readFileSync, statSync, writeFileSync } from "fs";
import { join, resolve } from "path";

interface BaselineData {
  version?: string;
  comment?: string;
  files: Record<string, number>;
}

const ROOT = resolve(import.meta.dir, "..");
const BASELINE_PATH = join(ROOT, "tools", "release-size-baseline.json");
const THRESHOLD_PCT = 10.0;

function formatSize(bytes: number): string {
  const mb = (bytes / (1024 * 1024)).toFixed(2);
  return `${bytes.toLocaleString("en-US")} bytes (${mb} MB)`;
}

export function checkReleaseSizes(distDir?: string, updateBaseline = false): boolean {
  const targetDir = distDir
    ? resolve(distDir)
    : existsSync(join(ROOT, "dist"))
      ? join(ROOT, "dist")
      : join(ROOT, "out", "rel64");

  if (!existsSync(BASELINE_PATH)) {
    throw new Error(`Baseline file not found at ${BASELINE_PATH}`);
  }

  const baseline: BaselineData = JSON.parse(readFileSync(BASELINE_PATH, "utf8"));
  let hasFailure = false;
  let hasWarning = false;

  console.log(`\n=== Release Binary Size Gate ===`);
  console.log(`Target directory: ${targetDir}`);
  console.log(`Growth failure threshold: >${THRESHOLD_PCT}%\n`);

  const currentSizes: Record<string, number> = {};

  for (const [filename, baseBytes] of Object.entries(baseline.files)) {
    let filePath = join(targetDir, filename);
    if (!existsSync(filePath) && filename === "Swiftleaf-install.exe") {
      const fallback = join(targetDir, "Swiftleaf.exe");
      if (existsSync(fallback)) {
        filePath = fallback;
      }
    }

    if (!existsSync(filePath)) {
      console.error(`::error::Required release binary not found: ${filePath}`);
      hasFailure = true;
      continue;
    }

    const currentBytes = statSync(filePath).size;
    currentSizes[filename] = currentBytes;

    const diffBytes = currentBytes - baseBytes;
    const growthPct = (diffBytes / baseBytes) * 100;
    const sign = diffBytes >= 0 ? "+" : "";

    console.log(`• ${filename}:`);
    console.log(`  Current:  ${formatSize(currentBytes)}`);
    console.log(`  Baseline: ${formatSize(baseBytes)}`);
    console.log(`  Delta:    ${sign}${diffBytes.toLocaleString("en-US")} bytes (${sign}${growthPct.toFixed(2)}%)`);

    if (growthPct > THRESHOLD_PCT) {
      console.error(
        `::error::${filename} size grew by ${growthPct.toFixed(2)}%, exceeding the ${THRESHOLD_PCT}% gate limit!`,
      );
      hasFailure = true;
    } else if (growthPct > 0) {
      console.warn(
        `::warning::${filename} size grew by ${growthPct.toFixed(2)}% (${diffBytes.toLocaleString("en-US")} bytes larger than baseline).`,
      );
      hasWarning = true;
    } else {
      console.log(`  Status:   OK (within baseline)`);
    }
    console.log("");
  }

  if (updateBaseline) {
    baseline.files = { ...baseline.files, ...currentSizes };
    writeFileSync(BASELINE_PATH, JSON.stringify(baseline, null, 2) + "\n", "utf8");
    console.log(`Updated baseline at ${BASELINE_PATH}`);
  }

  if (hasFailure) {
    console.error(`FAILED: One or more binaries exceeded the >${THRESHOLD_PCT}% size growth threshold.`);
    return false;
  }

  if (hasWarning) {
    console.log(`PASSED with warnings: Sizes grew but stayed within the allowed ${THRESHOLD_PCT}% threshold.`);
  } else {
    console.log(`PASSED: All release binaries are at or below baseline size.`);
  }

  return true;
}

if (import.meta.main) {
  const args = process.argv.slice(2);
  const update = args.includes("--update");
  const distArg = args.find((a) => !a.startsWith("--"));
  const ok = checkReleaseSizes(distArg, update);
  if (!ok) {
    process.exit(1);
  }
}
