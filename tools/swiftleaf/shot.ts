// Launch the app with -for-testing, size the window, optionally send commands,
// capture the frame to a PNG and quit.
//
//   bun tools/swiftleaf/shot.ts <out.png> [file.pdf] [--exe path] [--size 1400x900]
//                               [--cmd CmdName]... [--click-toolbar x,y]... [--wait ms]
// Actions (--cmd, --click-toolbar) run in the order given.
import { spawn } from "bun";
import { resolve } from "node:path";
import {
  captureWindowToPng,
  findVisibleChildWindow,
  moveWindow,
  packCoords,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  postMessage,
  setProcessDpiAware,
  sleep,
  waitForTopWindow,
} from "../../tests/winapi";
import { cmdId } from "../../tests/util";

const WM_COMMAND = 0x0111;
const FRAME_CLASS = "SUMATRA_PDF_FRAME";

const args = process.argv.slice(2);
let out = "";
let file = "";
let exe = resolve("out/rel64/SumatraPDF.exe");
let size = "1400x900";
let waitMs = 1500;
const actions: { kind: "cmd" | "click"; arg: string }[] = [];
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === "--exe") exe = resolve(args[++i]);
  else if (a === "--size") size = args[++i];
  else if (a === "--cmd") actions.push({ kind: "cmd", arg: args[++i] });
  else if (a === "--click-toolbar") actions.push({ kind: "click", arg: args[++i] });
  else if (a === "--wait") waitMs = Number(args[++i]);
  else if (!out) out = resolve(a);
  else file = resolve(a);
}
if (!out) {
  console.error("usage: bun tools/swiftleaf/shot.ts <out.png> [file.pdf] [--exe path] [--cmd CmdName]...");
  process.exit(2);
}

setProcessDpiAware();
const [w, h] = size.split("x").map(Number);
const procArgs = [exe, "-for-testing"];
if (file) procArgs.push(file);
const proc = spawn(procArgs, { stdout: "ignore", stderr: "ignore" });
try {
  const hwnd = await waitForTopWindow(proc.pid, FRAME_CLASS, 20000);
  if (!hwnd) throw new Error("frame window not found");
  moveWindow(hwnd, 40, 40, w, h);
  await sleep(waitMs);
  for (const act of actions) {
    if (act.kind === "cmd") {
      postMessage(hwnd, WM_COMMAND, cmdId(act.arg), 0);
    } else {
      const tb = findVisibleChildWindow(hwnd, "SUMATRA_VIRT_TOOLBAR");
      const [x, y] = act.arg.split(",").map(Number);
      postMessage(tb, WM_LBUTTONDOWN, 1, packCoords(x, y));
      postMessage(tb, WM_LBUTTONUP, 0, packCoords(x, y));
    }
    await sleep(600);
  }
  await sleep(400);
  if (!captureWindowToPng(hwnd, out)) throw new Error("capture failed");
  console.log(`saved ${out}`);
} finally {
  proc.kill();
}
