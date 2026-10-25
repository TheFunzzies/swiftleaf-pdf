// Existing page text: preserve failed drafts and IME composition, then save/reopen Unicode.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, pollUntil, runStandalone, SETTINGS_FILE_NAME, tmpPath } from "./util.ts";
import {
  enumChildWindows,
  getClassName,
  getControlText,
  isWindowVisible,
  sendMessage,
  sendText,
  setProcessDpiAware,
  VK_ESCAPE,
  VK_RETURN,
  WM_KEYDOWN,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

const WM_IME_STARTCOMPOSITION = 0x010d;
const WM_IME_ENDCOMPOSITION = 0x010e;

function findEditor(canvas: number, text: string): number {
  let found = 0;
  enumChildWindows(canvas, (hwnd) => {
    if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd) && getControlText(hwnd) === text) {
      found = hwnd;
      return false;
    }
    return true;
  });
  return found;
}

async function openEditor(frame: number, canvas: number, text: string): Promise<number> {
  sendCommandSync(frame, cmdId("CmdSelectAll"));
  sendCommandSync(frame, cmdId("CmdEditText"));
  return await pollUntil(
    async () => findEditor(canvas, text),
    (hwnd) => hwnd !== 0,
    { error: () => `Editor did not open with text ${JSON.stringify(text)}` },
  );
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath("existing-text-edit");
  const appdata = join(dir, "appdata");
  mkdirSync(appdata, { recursive: true });
  writeFileSync(
    join(appdata, SETTINGS_FILE_NAME),
    "UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\n",
  );
  const path = join(dir, "existing.pdf");
  const content = "BT /F1 12 Tf 72 700 Td (Original text) Tj ET\n";
  writeFileSync(
    path,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
      `<< /Length ${content.length} >>\nstream\n${content}endstream`,
    ]),
    "latin1",
  );

  const args = ["-appdata", appdata, path];
  const { proc, client, frame } = await launchControlled(args);
  try {
    await client.waitForRenderIdle();
    const canvas = await findCanvas(frame);
    const edit = await openEditor(frame, canvas, "Original text");
    sendText(edit, "ع");
    sendMessage(edit, WM_KEYDOWN, VK_RETURN, 0);
    // Requests run on the UI queue after the posted commit/cancel callback.
    await client.request(ControlCommand.TestMarkupAnnots, []);
    if (findEditor(canvas, "ع") !== edit) throw new Error("Rejected replacement lost its draft");
    sendMessage(edit, WM_KEYDOWN, VK_ESCAPE, 0);
    await pollUntil(
      async () => findEditor(canvas, "ع"),
      (hwnd) => hwnd === 0,
      { error: () => "Escape did not cancel the failed draft" },
    );
    const next = await openEditor(frame, canvas, "Original text");
    sendText(next, "中文");
    sendMessage(next, WM_IME_STARTCOMPOSITION, 0, 0);
    for (const key of [VK_RETURN, VK_ESCAPE]) {
      sendMessage(next, WM_KEYDOWN, key, 0);
      await client.request(ControlCommand.TestMarkupAnnots, []);
      if (findEditor(canvas, "中文") !== next) throw new Error("IME key committed or cancelled the PDF edit");
    }
    sendMessage(next, WM_IME_ENDCOMPOSITION, 0, 0);
    sendMessage(next, WM_KEYDOWN, VK_RETURN, 0);
    await pollUntil(
      async () => findEditor(canvas, "中文"),
      (hwnd) => hwnd === 0,
      { error: () => "Enter did not commit the CJK draft" },
    );
    sendCommandSync(frame, cmdId("CmdSaveAnnotations"));
    await client.request(ControlCommand.TestMarkupAnnots, []);
  } finally {
    client.close();
    await killAndWait(proc);
  }

  const reopened = await launchControlled(args);
  try {
    await reopened.client.waitForRenderIdle();
    const canvas = await findCanvas(reopened.frame);
    const edit = await openEditor(reopened.frame, canvas, "中文");
    sendMessage(edit, WM_KEYDOWN, VK_ESCAPE, 0);
    await pollUntil(
      async () => findEditor(canvas, "中文"),
      (hwnd) => hwnd === 0,
      { error: () => "Escape did not cancel the reopened draft" },
    );
  } finally {
    reopened.client.close();
    await killAndWait(reopened.proc);
  }
}

if (import.meta.main) await runStandalone(testit);
