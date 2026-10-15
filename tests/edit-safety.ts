// Run from the source root after a Release x64 build: bun tests/edit-safety.ts
// Compiles actual function bodies, with controlled app/engine boundaries.
// This is a fault test, not a PDF rendering or full-app integration test.
import { mkdirSync, readFileSync, writeFileSync, existsSync } from "node:fs";
import { resolve } from "node:path";

const dest = resolve("tests/tmp/edit-safety");
mkdirSync(dest, { recursive: true });
function slice(s: string, start: string, end: string) {
  const a = s.indexOf(start);
  const b = s.indexOf(end, a + start.length);
  if (a < 0 || b < a) throw new Error(`Source boundary changed: ${start} / ${end}`);
  return s.slice(a, b);
}
const organize = readFileSync("src/PageOrganize.cpp", "utf8");
writeFileSync(
  `${dest}/organize-under-test.inc`,
  slice(organize, "// one step Undo", "// pages 1..n") +
    slice(organize, "static int LastUndoStepFor", "// thumbnails can be dragged"),
);
const engine = readFileSync("src/shared/EngineMupdf.cpp", "utf8");
writeFileSync(`${dest}/encoding-under-test.inc`, slice(engine, "// UTF-8 to a PDF string", "static float TextWidth"));
// Stop immediately before document access: reaching here means validation allowed mutation.
const preflight = slice(engine, "bool EngineMupdfReplaceText(", "    auto* ctx = e->Ctx();");
writeFileSync(
  `${dest}/preflight-under-test.inc`,
  preflight.replace("EngineMupdfReplaceText", "TextPreflight") + "return true;\n}\n",
);

const vswhere = `${process.env["ProgramFiles(x86)"]}/Microsoft Visual Studio/Installer/vswhere.exe`;
const vs = Bun.spawnSync([
  vswhere,
  "-latest",
  "-products",
  "*",
  "-requires",
  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
  "-property",
  "installationPath",
]);
if (!vs.success) throw new Error("Visual Studio C++ tools not found");
const vcvars = `${vs.stdout.toString().trim()}/VC/Auxiliary/Build/vcvars64.bat`;
if (!existsSync(vcvars)) throw new Error("vcvars64.bat not found");
const quote = (s: string) => `"${s}"`;
const libs =
  "gdiplus gdi32 user32 comctl32 shlwapi Version wininet shcore wintrust crypt32 shell32 ole32 oleAut32 urlmon windowscodecs advapi32 dbghelp"
    .split(" ")
    .map((s) => `${s}.lib`);
const args = [
  "cl",
  "/nologo",
  "/std:c++20",
  "/utf-8",
  "/O2",
  "/MT",
  "/DNDEBUG",
  "/DIS_TRACY=0",
  `/I${quote(resolve("src"))}`,
  `/I${quote(dest)}`,
  quote(resolve("tests/edit-safety.cpp")),
  `/Fo${quote(`${dest}/test.obj`)}`,
  `/Fe${quote(`${dest}/test.exe`)}`,
  "/link",
  "/OPT:REF",
  quote(resolve("out/rel64/obj-s/base.lib")),
  quote(resolve("out/rel64/obj-s/mupdf.lib")),
  quote(resolve("out/rel64/obj-s/a-lcms2.lib")),
  ...libs,
];
const batch = `${dest}/build.cmd`;
writeFileSync(batch, `@echo off\r\ncall ${quote(vcvars)} >nul\r\nif errorlevel 1 exit /b 1\r\n${args.join(" ")}\r\n`);
const build = Bun.spawnSync(["cmd.exe", "/d", "/c", batch], { stdout: "inherit", stderr: "inherit" });
if (!build.success) process.exit(build.exitCode);
const data = `${dest}/data`;
mkdirSync(data, { recursive: true });
const run = Bun.spawnSync([`${dest}/test.exe`], {
  stdout: "inherit",
  stderr: "inherit",
  env: { ...process.env, TEMP: data, TMP: data },
});
process.exit(run.exitCode);
