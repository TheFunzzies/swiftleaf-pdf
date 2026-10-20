// Empty FreeText placement must not mutate the PDF or seed placeholder content.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, EXE, runStandalone, tmpPath } from "./util.ts";
import {
  enumChildWindows,
  getClassName,
  getClientRect,
  setProcessDpiAware,
  isWindowVisible,
  captureWindowToPng,
  clientToScreen,
  setCursorPos,
  WM_MOUSEMOVE,
  sleep,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  packCoords,
  sendMessage,
  sendText,
  WM_CHAR,
  WM_COMMAND,
  WM_KEYDOWN,
  WM_LBUTTONDBLCLK,
  MK_LBUTTON,
  VK_ESCAPE,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

const WM_NCLBUTTONDOWN = 0x00a1;
const HTBORDER = 18;

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath("free-text-draft");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "blank.pdf");
  const background = "q .85 .93 1 rg 200 300 250 200 re f .2 .4 .8 RG 1 w 200 390 m 450 390 l S Q";
  writeFileSync(
    pdf,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>",
      `<< /Length ${background.length} >>\nstream\n${background}\nendstream`,
    ]),
    "latin1",
  );
  const original = readFileSync(pdf);
  const { proc, client, frame } = await launchControlled(["-view", "single page", "-zoom", "fit page", pdf]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const canvas = findCanvas(frame);
    const dump = async () => {
      await client.waitForRenderIdle();
      const result = await client.request(ControlCommand.TestMarkupAnnots, []);
      if (result[0] !== 0) throw new Error(String(result));
      return String(result[1]);
    };
    const want = (raw: string, pattern: RegExp, message: string) => {
      if (!pattern.test(raw)) throw new Error(`${message}\n${raw}`);
    };
    const place = () => {
      const r = getClientRect(canvas);
      sendMessage(
        frame,
        WM_COMMAND,
        cmdId("CmdCreateAnnotFreeText"),
        packCoords(Math.floor(r.right / 2), Math.floor(r.bottom / 2)),
      );
    };
    const box = () => {
      let result = 0;
      enumChildWindows(canvas, (hwnd) => {
        if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) {
          result = hwnd;
          return false;
        }
        return true;
      });
      if (!result) throw new Error("No draft editor");
      return result;
    };
    place();
    let raw = await dump();
    want(raw, /freeTextEdit active=1[^\n]* text=\r?$/m, "New draft must be empty");
    want(raw, /annotations=0/, "Draft must not create an annotation");
    want(raw, /undo canUndo=0 canRedo=0 modified=0/, "Draft must not dirty the document");
    sendMessage(box(), WM_KEYDOWN, VK_ESCAPE, 0);
    raw = await dump();
    want(raw, /freeTextEdit active=0/, "Escape closes draft");
    want(raw, /annotations=0/, "Escape leaves no annotation");
    want(raw, /undo canUndo=0 canRedo=0 modified=0/, "Escape leaves no undo record");
    place();
    sendMessage(box(), WM_CHAR, 0x0a, 0);
    raw = await dump();
    want(raw, /annotations=0/, "Accepting empty text leaves no annotation");
    if (!readFileSync(pdf).equals(original)) throw new Error("Empty/cancelled drafts changed the file");
    place();
    sendText(box(), "A movable draft");
    sendMessage(box(), WM_CHAR, 0x0a, 0);
    raw = await dump();
    want(raw, /annotations=1/, "Nonempty accept creates one annotation");
    want(raw, /selected=1/, "Accepted text remains selected outside Edit mode");
    captureWindowToPng(canvas, join(dir, "accepted.png"));
    const rect = /type=FreeText[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw);
    if (!rect) throw new Error(`Missing accepted text rectangle\n${raw}`);
    const pos = packCoords(+rect[1]! + 5, +rect[2]! + 5);
    sendMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, pos);
    sendMessage(canvas, WM_LBUTTONUP, 0, pos);
    sendMessage(canvas, WM_LBUTTONDBLCLK, MK_LBUTTON, pos);
    sendMessage(canvas, WM_LBUTTONUP, 0, pos);
    raw = await dump();
    want(raw, /freeTextEdit active=1[^\n]*text=A movable draft/, "Accepted text can be reopened outside Edit mode");
    sendText(box(), "Discard this edit");
    sendMessage(box(), WM_KEYDOWN, VK_ESCAPE, 0);
    const beforeDrag = /type=FreeText[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(await dump());
    // Move the selected object in Read mode, using the existing canvas drag path.
    const start = { x: +rect[1]! + Math.floor(+rect[3]! / 2), y: +rect[2]! + Math.floor(+rect[4]! / 2) };
    const end = { x: start.x + 40, y: start.y + 30 };
    const screen = clientToScreen(canvas, end.x, end.y);
    setCursorPos(screen.x, screen.y);
    sendMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(start.x, start.y));
    sendMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(end.x, end.y));
    await sleep(40);
    sendMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(end.x, end.y));
    sendMessage(canvas, WM_LBUTTONUP, 0, packCoords(end.x, end.y));
    raw = await dump();
    const moved = /type=FreeText[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw);
    if (!moved || Math.abs(+moved[1]! - +rect[1]! - 40) > 2 || Math.abs(+moved[2]! - +rect[2]! - 30) > 2) {
      const beforeScreen = beforeDrag?.slice(1, 5).join(",") ?? "missing";
      throw new Error(`Accepted text did not move by the drag delta\n${raw}\nbeforeScreen=${beforeScreen}`);
    }
    sendCommandSync(frame, cmdId("CmdUndo"));
    raw = await dump();
    want(raw, /annotations=1/, "Undoing movement keeps the annotation");
    const restored = /type=FreeText[^\n]*screen=(-?\d+),(-?\d+),/.exec(raw);
    if (!restored || +restored[1]! !== +rect[1]! || +restored[2]! !== +rect[2]!) {
      throw new Error(`Undo did not restore the text position\n${raw}`);
    }
    sendCommandSync(frame, cmdId("CmdUndo"));
    raw = await dump();
    want(raw, /annotations=0/, "One undo removes the accepted draft");
    want(raw, /undo canUndo=0/, "Creation is one undo step");
    sendCommandSync(frame, cmdId("CmdRedo"));
    sendCommandSync(frame, cmdId("CmdRedo"));
    raw = await dump();
    want(raw, /annotations=1/, "Redo restores text and movement");
    if (!readFileSync(pdf).equals(original)) throw new Error("Unsaved text changed the file");
    sendCommandSync(frame, cmdId("CmdSaveAnnotations"));
    await client.waitForRenderIdle();
    const tool = join(dirname(EXE), "sumatrapdf-tool.exe");
    const textPath = join(dir, "saved.txt");
    const textResult = Bun.spawnSync([tool, "draw", "-F", "txt", "-o", textPath, pdf, "1"]);
    if (!textResult.success || !readFileSync(textPath, "utf8").includes("A movable draft")) {
      throw new Error(`Saved text did not survive fresh-process extraction: ${textResult.stderr}`);
    }
    const annotResult = Bun.spawnSync([tool, "show", pdf, "pages/1/Annots/1"]);
    const annot = annotResult.stdout.toString();
    if (!annotResult.success || !/\/C\s*\[\s*\]/.test(annot) || !/\/W\s+0\b/.test(annot)) {
      throw new Error(`Default text must have no PDF fill or border\n${annot}`);
    }
    captureWindowToPng(canvas, join(dir, "saved.png"));
    // Grabbing an active editor's frame must move, rather than resize, the text.
    place();
    sendText(box(), "Move by frame");
    raw = await dump();
    const draft = /freeTextEdit active=1 rect=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw);
    if (!draft) throw new Error(`Missing frame-drag draft\n${raw}`);
    const grip = { x: +draft[1]! + Math.floor(+draft[3]! / 2), y: +draft[2]! };
    const gripScreen = clientToScreen(canvas, grip.x, grip.y);
    const target = { x: grip.x + 30, y: grip.y + 20 };
    const targetScreen = clientToScreen(canvas, target.x, target.y);
    setCursorPos(targetScreen.x, targetScreen.y);
    sendMessage(box(), WM_NCLBUTTONDOWN, HTBORDER, packCoords(gripScreen.x, gripScreen.y));
    sendMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(target.x, target.y));
    await sleep(40);
    sendMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(target.x, target.y));
    sendMessage(canvas, WM_LBUTTONUP, 0, packCoords(target.x, target.y));
    raw = await dump();
    const frameMoved = [...raw.matchAll(/type=FreeText[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/g)].at(-1);
    if (
      !frameMoved ||
      Math.abs(+frameMoved[1]! - +draft[1]! - 30) > 2 ||
      Math.abs(+frameMoved[2]! - +draft[2]! - 20) > 2 ||
      Math.abs(+frameMoved[3]! - +draft[3]!) > 2 ||
      Math.abs(+frameMoved[4]! - +draft[4]!) > 2
    ) {
      throw new Error(`Dragging the editor frame must preserve size and move the text\n${raw}`);
    }
    console.log("free-text-draft: OK");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}
if (import.meta.main) await runStandalone(testit);
