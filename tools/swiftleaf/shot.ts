// Launch the app with -for-testing, size the window, optionally send commands,
// capture the frame to a PNG and quit.
//
//   bun tools/swiftleaf/shot.ts <out.png> [file.pdf] [--exe path] [--size 1400x900]
//                               [--cmd CmdName]... [--click-toolbar x,y]... [--wait ms]
//                               [--click-canvas x,y] [--drag-canvas x1,y1,x2,y2] [--type-edit "text"]
//                               [--drag-in CLASS:x1,y1,x2,y2[:hold]] [--capture-title "Window title"]
// Actions run in the order given. --type-edit fills the canvas's child edit
// (e.g. Edit Text's) and presses Enter.
import { spawn } from "bun";
import { resolve } from "node:path";
import {
  captureWindowToPng,
  enumWindows,
  getWindowPid,
  getWindowText,
  isWindowVisible,
  findVisibleChildWindow,
  moveWindow,
  packCoords,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  WM_MOUSEMOVE,
  WM_KEYDOWN,
  MK_LBUTTON,
  VK_RETURN,
  sendText,
  postMessage,
  setProcessDpiAware,
  sleep,
  waitForTopWindow,
} from "../../tests/winapi";
import { cmdId } from "../../tests/util";

const WM_COMMAND = 0x0111;
const FRAME_CLASS = "SWIFTLEAF_PDF_FRAME";

const args = process.argv.slice(2);
let out = "";
let file = "";
let exe = resolve("out/rel64/Swiftleaf.exe");
let size = "1400x900";
let waitMs = 1500;
let frameClass = FRAME_CLASS;
let procArgs0: string[] = ["-for-testing"];
let noResize = false;
// capture this top-level window of the app (a dialog) instead of the frame
let captureTitle = "";
const actions: { kind: "cmd" | "click" | "drag" | "type" | "clickCanvas" | "dragIn"; arg: string }[] = [];
// a drag left held (":hold") is released after the capture
let heldDrag: { wnd: number; x: number; y: number } | null = null;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === "--exe") exe = resolve(args[++i]);
  else if (a === "--size") size = args[++i];
  else if (a === "--cmd") actions.push({ kind: "cmd", arg: args[++i] });
  else if (a === "--click-toolbar") actions.push({ kind: "click", arg: args[++i] });
  else if (a === "--click-canvas") actions.push({ kind: "clickCanvas", arg: args[++i] });
  else if (a === "--drag-canvas") actions.push({ kind: "drag", arg: args[++i] });
  else if (a === "--drag-in") actions.push({ kind: "dragIn", arg: args[++i] });
  else if (a === "--type-edit") actions.push({ kind: "type", arg: args[++i] });
  else if (a === "--wait") waitMs = Number(args[++i]);
  else if (a === "--class") frameClass = args[++i];
  else if (a === "--no-resize") noResize = true;
  else if (a === "--capture-title") captureTitle = args[++i];
  else if (a === "--no-testing") procArgs0 = [];
  else if (!out) out = resolve(a);
  else file = resolve(a);
}
if (!out) {
  console.error("usage: bun tools/swiftleaf/shot.ts <out.png> [file.pdf] [--exe path] [--cmd CmdName]...");
  process.exit(2);
}

setProcessDpiAware();
const [w, h] = size.split("x").map(Number);
const procArgs = [exe, ...procArgs0];
if (file) procArgs.push(file);
const proc = spawn(procArgs, { stdout: "ignore", stderr: "ignore" });
try {
  const hwnd = await waitForTopWindow(proc.pid, frameClass, 20000);
  if (!hwnd) throw new Error("frame window not found");
  if (!noResize) moveWindow(hwnd, 40, 40, w, h);
  await sleep(waitMs);
  for (const act of actions) {
    if (act.kind === "cmd") {
      postMessage(hwnd, WM_COMMAND, cmdId(act.arg), 0);
    } else if (act.kind === "drag") {
      const canvas = findVisibleChildWindow(hwnd, "SUMATRA_PDF_CANVAS");
      const [x1, y1, x2, y2] = act.arg.split(",").map(Number);
      postMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(x1, y1));
      for (let k = 1; k <= 8; k++) {
        const x = Math.round(x1 + ((x2 - x1) * k) / 8);
        const y = Math.round(y1 + ((y2 - y1) * k) / 8);
        postMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(x, y));
        await sleep(30);
      }
      postMessage(canvas, WM_LBUTTONUP, 0, packCoords(x2, y2));
    } else if (act.kind === "dragIn") {
      // a drag inside the frame's first visible child of a window class
      const [cls, coords, hold] = act.arg.split(":");
      const wnd = findVisibleChildWindow(hwnd, cls);
      if (!wnd) throw new Error(`no visible ${cls} window`);
      const [x1, y1, x2, y2] = coords.split(",").map(Number);
      postMessage(wnd, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(x1, y1));
      for (let k = 1; k <= 10; k++) {
        const x = Math.round(x1 + ((x2 - x1) * k) / 10);
        const y = Math.round(y1 + ((y2 - y1) * k) / 10);
        postMessage(wnd, WM_MOUSEMOVE, MK_LBUTTON, packCoords(x, y));
        await sleep(30);
      }
      if (hold === "hold") heldDrag = { wnd, x: x2, y: y2 };
      else postMessage(wnd, WM_LBUTTONUP, 0, packCoords(x2, y2));
    } else if (act.kind === "clickCanvas") {
      const canvas = findVisibleChildWindow(hwnd, "SUMATRA_PDF_CANVAS");
      const [x, y] = act.arg.split(",").map(Number);
      postMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(x, y));
      postMessage(canvas, WM_LBUTTONUP, 0, packCoords(x, y));
    } else if (act.kind === "type") {
      const canvas = findVisibleChildWindow(hwnd, "SUMATRA_PDF_CANVAS");
      const edit = findVisibleChildWindow(canvas, "Edit");
      if (!edit) throw new Error("no edit on the canvas");
      sendText(edit, act.arg);
      postMessage(edit, WM_KEYDOWN, VK_RETURN, 0);
    } else {
      const tb = findVisibleChildWindow(hwnd, "SUMATRA_VIRT_TOOLBAR");
      const [x, y] = act.arg.split(",").map(Number);
      postMessage(tb, WM_LBUTTONDOWN, 1, packCoords(x, y));
      postMessage(tb, WM_LBUTTONUP, 0, packCoords(x, y));
    }
    await sleep(600);
  }
  await sleep(400);
  let target = hwnd;
  if (captureTitle) {
    target = 0;
    enumWindows((w) => {
      if (getWindowPid(w) === proc.pid && isWindowVisible(w) && getWindowText(w) === captureTitle) {
        target = w;
        return false;
      }
      return true;
    });
    if (!target) throw new Error(`no window titled '${captureTitle}'`);
  }
  if (!captureWindowToPng(target, out)) throw new Error("capture failed");
  console.log(`saved ${out}`);
  if (heldDrag) postMessage(heldDrag.wnd, WM_LBUTTONUP, 0, packCoords(heldDrag.x, heldDrag.y));
} finally {
  proc.kill();
}
