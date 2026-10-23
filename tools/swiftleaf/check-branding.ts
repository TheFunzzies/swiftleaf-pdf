// Fails if "sumatra" (case-insensitive) appears in a tracked text file that is
// not covered by tools/swiftleaf/branding-allowlist.txt.
//
// Run: bun tools/swiftleaf/check-branding.ts [--verbose]
//
// Allowlist syntax (one entry per line, '#' starts a comment):
//   <glob>            path glob (Bun.Glob, forward slashes); matching files are skipped.
//                     A trailing '/' or '/**' covers a whole directory.
//   @token <literal>  literal identifier removed from every scanned line before the
//                     check (persisted names, build targets, window classes, ...).
import { Glob } from "bun";
import { dirname, join } from "node:path";

const root = join(dirname(import.meta.dir), "..");
const verbose = process.argv.includes("--verbose");
const allowFile = join(import.meta.dir, "branding-allowlist.txt");

const globs: Glob[] = [];
const tokens: string[] = [];
for (const raw of (await Bun.file(allowFile).text()).split(/\r?\n/)) {
  const line = raw.trim();
  if (!line || line.startsWith("#")) continue;
  if (line.startsWith("@token ")) {
    tokens.push(line.slice(7).trim());
    continue;
  }
  let g = line;
  if (g.endsWith("/")) g += "**";
  globs.push(new Glob(g));
}
// longest first so e.g. SumatraPDF-settings.txt wins over SumatraPDF
tokens.sort((a, b) => b.length - a.length);
const tokenRe = tokens.length
  ? new RegExp(tokens.map((t) => t.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")).join("|"), "gi")
  : null;

const proc = Bun.spawnSync(["git", "ls-files", "-z"], { cwd: root });
const files = new TextDecoder().decode(proc.stdout).split("\0").filter(Boolean);

let scanned = 0;
let allowed = 0;
const bad: string[] = [];
for (const f of files) {
  if (globs.some((g) => g.match(f))) {
    allowed++;
    continue;
  }
  const file = Bun.file(join(root, f));
  if (!(await file.exists())) continue;
  const buf = new Uint8Array(await file.arrayBuffer());
  if (buf.subarray(0, 8000).includes(0)) continue; // binary
  scanned++;
  const lines = new TextDecoder().decode(buf).split("\n");
  for (let i = 0; i < lines.length; i++) {
    let l = lines[i];
    if (tokenRe) l = l.replace(tokenRe, "");
    if (/sumatra/i.test(l)) bad.push(`${f}:${i + 1}: ${lines[i].trim().slice(0, 120)}`);
  }
}

if (verbose) console.log(`scanned ${scanned} files, ${allowed} allowlisted by path, ${tokens.length} tokens`);
if (bad.length) {
  console.error(`branding check FAILED: ${bad.length} mention(s) of "sumatra" outside the allowlist:`);
  for (const b of bad.slice(0, 200)) console.error("  " + b);
  if (bad.length > 200) console.error(`  ... and ${bad.length - 200} more`);
  console.error("Rename to SwiftLeaf, or add the path/identifier to tools/swiftleaf/branding-allowlist.txt.");
  process.exit(1);
}
console.log(`branding check OK (${scanned} files scanned)`);
