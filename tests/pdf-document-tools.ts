// Object-level round trips through the production merge/sanitize/redact functions.
// Uses the same Release libraries as edit-safety.ts; also runs in branch CI.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";

const dest = resolve("tests/tmp/pdf-document-tools");
mkdirSync(dest, { recursive: true });
const engine = readFileSync("src/shared/EngineMupdf.cpp", "utf8");
const a = engine.indexOf("static pdf_document* OpenPdfForMerge(");
const b = engine.indexOf("// Step one operation back", a);
if (a < 0 || b < a) throw new Error("Merge source boundary changed");
writeFileSync(`${dest}/merge-under-test.inc`, engine.slice(a, b));
const all = readFileSync("src/shared/EngineAll.h", "utf8");
writeFileSync(`${dest}/merge-types.inc`, all.slice(all.indexOf("struct PdfMergeSource {"), all.indexOf('// Swiftleaf Edit Text:')));
const original = readFileSync("tests/edit-safety.ts", "utf8");
const build = original.slice(original.indexOf("const vswhere ="))
  .replace('quote(resolve("tests/edit-safety.cpp"))', 'quote(resolve("tests/pdf-document-tools.cpp"))')
  .replace('`/I${quote(dest)}`', '`/I${quote(dest)}`, `/I${quote(resolve("src/shared"))}`, `/I${quote(resolve("ext/mupdf/include"))}`');
writeFileSync(`${dest}/build.ts`, 'import { mkdirSync, existsSync, writeFileSync } from "node:fs";\nimport { resolve } from "node:path";\n' + `const dest = ${JSON.stringify(dest)};\n` + build);
const proc = Bun.spawnSync([process.execPath, `${dest}/build.ts`], { stdout: "inherit", stderr: "inherit" });
process.exit(proc.exitCode);
