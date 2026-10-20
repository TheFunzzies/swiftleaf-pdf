import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, runStandalone, tmpPath } from "./util.ts";
import {
  enumChildWindows,
  getClassName,
  getClientRect,
  isWindowVisible,
  packCoords,
  sendMessage,
  sendText,
  sleep,
  WM_COMMAND,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

export async function testit(): Promise<void> {
  const dir = tmpPath("editor-lifecycle");
  mkdirSync(dir, { recursive: true });
  const content = "BT /F1 12 Tf 72 700 Td (Original document text) Tj ET";
  const pdf = assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
    `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
  ]);
  const a = join(dir, "a.pdf");
  const b = join(dir, "b.pdf");
  writeFileSync(a, pdf, "latin1");
  writeFileSync(b, pdf, "latin1");
  const { proc, client, frame } = await launchControlled([a, b]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const canvas = findCanvas(frame);
    const r = getClientRect(canvas);
    sendMessage(
      frame,
      WM_COMMAND,
      cmdId("CmdCreateAnnotFreeText"),
      packCoords(Math.floor(r.right / 2), Math.floor(r.bottom / 2)),
    );
    let editor = 0;
    enumChildWindows(canvas, (hwnd) => {
      if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) editor = hwnd;
      return true;
    });
    if (!editor) throw new Error("Draft did not open");
    sendText(editor, "Keep draft on tab switch");
    sendCommandSync(frame, cmdId("CmdPrevTab"));
    await client.waitForRenderIdle();
    let result = await client.request(ControlCommand.TestMarkupAnnots, []);
    if (!/annotations=0/.test(String(result[1]))) throw new Error("Draft was written into the other document");
    sendCommandSync(frame, cmdId("CmdNextTab"));
    await client.waitForRenderIdle();
    result = await client.request(ControlCommand.TestMarkupAnnots, []);
    if (!/page1text=.*Keep draft on tab switch/.test(String(result[1])))
      throw new Error(`Tab switch discarded draft\n${result[1]}`);

    sendCommandSync(frame, cmdId("CmdUndo"));
    await client.seedTextSelection(1);
    sendCommandSync(frame, cmdId("CmdEditText"));
    editor = 0;
    enumChildWindows(canvas, (hwnd) => {
      if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) editor = hwnd;
      return true;
    });
    if (!editor) throw new Error("Edit Text did not open");
    sendText(editor, "Changed document text");
    sendCommandSync(frame, cmdId("CmdPrevTab"));
    await client.waitForRenderIdle();
    await sleep(50);
    enumChildWindows(canvas, (hwnd) => {
      if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) throw new Error("Edit Text survived tab switch");
      return true;
    });
    sendCommandSync(frame, cmdId("CmdNextTab"));
    await client.waitForRenderIdle();
    result = await client.request(ControlCommand.TestMarkupAnnots, []);
    if (!/page1text=.*Changed document text/.test(String(result[1])))
      throw new Error(`Tab switch discarded Edit Text\n${result[1]}`);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) {
  await runStandalone(testit);
}
