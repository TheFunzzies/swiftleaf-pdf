#!/usr/bin/env bun
// Compares release exe/installer sizes against a recorded baseline.
// Warns (never fails) when a binary grows more than 10% over baseline.
const fs = require("fs");
const path = require("path");

const baselinePath = path.join(import.meta.dir, "..", "..", "tools", "swiftleaf", "baseline-sizes.txt");

interface Sizes { [name: string]: number; }

function parseBaseline(text: string): Sizes {
    const sizes: Sizes = {};
    for (const line of text.split("\n")) {
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith("#")) continue;
        const parts = trimmed.split(/\s+/);
        if (parts.length >= 2) {
            const name = parts[0];
            const bytes = parseInt(parts[1], 10);
            if (!isNaN(bytes)) sizes[name] = bytes;
        }
    }
    return sizes;
}

function getFileSize(p: string): number | null {
    try {
        const stat = fs.statSync(p);
        return stat.size;
    } catch {
        return null;
    }
}

const threshold = 0.10; // 10% growth triggers warning
let warned = false;

if (!fs.existsSync(baselinePath)) {
    console.log("check-release-size: baseline file not found, skipping");
    process.exit(0);
}

const baseline = parseBaseline(fs.readFileSync(baselinePath, "utf-8"));

for (const name of ["Swiftleaf.exe", "Swiftleaf-install.exe"]) {
    const distPath = path.join(process.cwd(), "dist", name);
    const actual = getFileSize(distPath);
    const expected = baseline[name];
    if (actual === null) {
        console.log(`check-release-size: ${name} not found in dist/`);
        continue;
    }
    if (!expected) {
        console.log(`check-release-size: ${name} has no baseline, skipping`);
        continue;
    }
    const delta = actual - expected;
    const pct = (Math.abs(delta) / expected) * 100;
    if (delta > 0 && pct > threshold * 100) {
        warned = true;
        console.log(
            `check-release-size: WARNING ${name} grew ${pct.toFixed(1)}% ` +
            `(${expected} -> ${actual} bytes)`
        );
    } else {
        console.log(
            `check-release-size: ${name} ${pct.toFixed(1)}% ` +
            `(${expected} -> ${actual} bytes)`
        );
    }
}

if (warned) {
    console.log("check-release-size: release size gate reported warnings (non-blocking)");
}
process.exit(0);
