import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, runStandalone, tmpPath } from "./util.ts";
import {
  enumChildWindows,
  findTopWindow,
  getClassName,
  getClientRect,
  getControlText,
  getWindowText,
  isWindowVisible,
  packCoords,
  postMessage,
  sendCopyDataW,
  sendMessage,
  sendText,
  sleep,
  VK_ESCAPE,
  VK_RETURN,
  WM_COMMAND,
  WM_KEYDOWN,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

function visibleEditor(canvas: number): number {
  let editor = 0;
  enumChildWindows(canvas, (hwnd) => {
    if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) editor = hwnd;
    return true;
  });
  return editor;
}

function writeFixture(path: string): void {
  const content = "BT /F1 12 Tf 72 700 Td (Original document text) Tj ET";
  writeFileSync(
    path,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]),
    "latin1",
  );
}

async function testReload(kind: "edit" | "freetext"): Promise<void> {
  const path = tmpPath(`editor-${kind}-reload.pdf`);
  writeFixture(path);
  const { proc, client, frame } = await launchControlled(["-lang", "en", path]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const canvas = findCanvas(frame);
    if (kind === "edit") {
      await client.seedTextSelection(1);
      sendCommandSync(frame, cmdId("CmdEditText"));
    } else {
      const r = getClientRect(canvas);
      sendMessage(frame, WM_COMMAND, cmdId("CmdCreateAnnotFreeText"), packCoords(r.right / 2, r.bottom / 2));
    }
    const editor = visibleEditor(canvas);
    if (!editor) throw new Error(`${kind}-reload: editor did not open`);
    sendText(editor, "Reload draft");
    postMessage(frame, WM_COMMAND, cmdId("CmdReloadDocument"), 0);

    let decision = 0;
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      const dialog = findTopWindow(proc.pid!, "#32770");
      if (dialog && getWindowText(dialog) === "Unsaved changes") {
        decision = dialog;
        break;
      }
      await sleep(20);
    }
    // TaskDialog's documented TDM_CLICK_BUTTON and the app's Discard button.
    const TDM_CLICK_BUTTON = 0x0400 + 102;
    const discardChanges = 100;
    if (decision) sendMessage(decision, TDM_CLICK_BUTTON, discardChanges, 0);
    await client.waitForRenderIdle();
    postMessage(isWindowVisible(editor) ? editor : canvas, WM_KEYDOWN, VK_RETURN, 0);
    await client.request(ControlCommand.Ping);
    const result = await client.request(ControlCommand.TestMarkupAnnots);
    const raw = String(result[1]);
    if (visibleEditor(canvas) || !/page1text=.*Original document text/.test(raw) || !/modified=0/.test(raw)) {
      throw new Error(`${kind}-reload: stale editor or write after reload\n${raw}`);
    }
    if (!decision) throw new Error(`${kind}-reload: no unsaved-changes decision before reload`);
    console.log(`${kind}-reload: old draft committed before discard; Enter left the reloaded document unchanged`);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

async function testRejectedOpen(): Promise<void> {
  const a = tmpPath("editor-rejected-a.pdf");
  const b = tmpPath("editor-rejected-b.pdf");
  writeFixture(a);
  writeFixture(b);
  const { proc, client, frame } = await launchControlled([a]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    await client.seedTextSelection(1);
    sendCommandSync(frame, cmdId("CmdEditText"));
    const editor = visibleEditor(findCanvas(frame));
    if (!editor) throw new Error("rejected-open: editor did not open");
    sendText(editor, "Keep rejected draft �");
    const copyDataDdeW = 0x44646557;
    sendCopyDataW(frame, copyDataDdeW, `[Open("${b}")]`);
    const result = await client.request(ControlCommand.TestCurrentTab);
    if (
      !isWindowVisible(editor) ||
      getControlText(editor) !== "Keep rejected draft �" ||
      !String(result[1]).includes(a)
    ) {
      throw new Error(`rejected-open: opening another file discarded the draft or changed tabs\n${result[1]}`);
    }
    postMessage(editor, WM_KEYDOWN, VK_ESCAPE, 0);
    await client.request(ControlCommand.Ping);
    console.log("rejected-open: second-document load aborted and typed draft survived");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

async function testTabs(): Promise<void> {
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
    sendText(editor, "Edited text");
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
    if (!/page1text=.*Edited text/.test(String(result[1])))
      throw new Error(`Tab switch discarded Edit Text\n${result[1]}`);

    await client.seedTextSelection(1);
    sendCommandSync(frame, cmdId("CmdEditText"));
    editor = 0;
    enumChildWindows(canvas, (hwnd) => {
      if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) editor = hwnd;
      return true;
    });
    if (!editor) throw new Error("Second Edit Text session did not open");
    sendText(editor, "�");
    sendCommandSync(frame, cmdId("CmdPrevTab"));
    result = await client.request(ControlCommand.TestMarkupAnnots, []);
    if (!isWindowVisible(editor) || !/page1text=.*Edited text/.test(String(result[1]))) {
      throw new Error(`Rejected draft did not abort the tab switch\n${result[1]}`);
    }
    sendMessage(editor, WM_KEYDOWN, VK_ESCAPE, 0);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

export async function testit(): Promise<void> {
  const cases: Record<string, () => Promise<void>> = {
    "rejected-open": testRejectedOpen,
    "edit-reload": () => testReload("edit"),
    "freetext-reload": () => testReload("freetext"),
    tabs: testTabs,
  };
  const only = process.env.SWIFTLEAF_LIFECYCLE_CASE;
  if (only) {
    if (!cases[only]) throw new Error(`Unknown lifecycle case: ${only}`);
    await cases[only]!();
    return;
  }
  for (const test of Object.values(cases)) await test();
}

if (import.meta.main) {
  await runStandalone(testit);
}
