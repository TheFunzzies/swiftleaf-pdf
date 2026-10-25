import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlClient, ControlCommand, withControlledApp } from "./control";
import { EXE, ROOT, cmdId, makeOnePagePdf, runStandalone, tmpPath } from "./util";
import { findCanvas, waitForFrame } from "./win-automation";
import {
  clientToScreen,
  setCursorPos,
  findTopWindow,
  getDlgItem,
  packCoords,
  postMessage,
  sendMessage,
  sendText,
  sleep,
  setProcessDpiAware,
  WM_COMMAND,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  WM_MOUSEMOVE,
  MK_LBUTTON,
} from "./winapi";

const resources = readFileSync(join(ROOT, "src/resource.h"), "utf8");
function id(name: string): number {
  const match = new RegExp(`^#define ${name} (\\d+)\\s*$`, "m").exec(resources);
  if (!match) throw new Error(`Missing resource ${name}`);
  return +match[1]!;
}

async function until<T>(get: () => T, accept: (value: T) => boolean): Promise<T> {
  const deadline = Date.now() + 8000;
  do {
    const value = get();
    if (accept(value)) return value;
    await sleep(30);
  } while (Date.now() < deadline);
  throw new Error("Form dialog did not reach the expected state");
}

async function dump(client: ControlClient): Promise<string> {
  const result = await client.request(ControlCommand.TestMarkupAnnots, []);
  if (result[0] !== 0) throw new Error(String(result[1]));
  return String(result[1]);
}

function rect(raw: string, name: string): number[] {
  const m = new RegExp(`widget type=\\d name=${name} .*screen=(-?\\d+),(-?\\d+),(\\d+),(\\d+)`).exec(raw);
  if (!m) throw new Error(`Missing widget ${name}: ${raw}`);
  return m.slice(1).map(Number);
}

function drag(canvas: number, x: number, y: number, endX: number, endY: number): void {
  const end = clientToScreen(canvas, endX, endY);
  setCursorPos(end.x, end.y);
  sendMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(x, y));
  sendMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(endX, endY));
  sendMessage(canvas, WM_LBUTTONUP, 0, packCoords(endX, endY));
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const source = tmpPath("form-authoring-source.pdf");
  const saved = tmpPath("form-authoring-saved.pdf");
  writeFileSync(source, makeOnePagePdf());
  const names = ["Text", "Checkbox", "Radio", "Combo", "Signature", "ListBox", "PushButton", "Image", "Date"];
  await withControlledApp(
    EXE,
    async (client, proc) => {
      const frame = await waitForFrame(proc.pid!);
      await client.waitForRenderIdle();
      await client.setNotificationsEnabled(false);
      const canvas = findCanvas(frame);
      for (let i = 0; i < names.length; i++) {
        // Exercise the tools-panel/palette entry for text, and each direct tool.
        sendMessage(frame, WM_COMMAND, cmdId(i === 0 ? "CmdPrepareForm" : `CmdCreateForm${names[i]}`), 0);
        await client.waitForRenderIdle();
        const layout = await client.layout();
        const page = /page n=1 shown=1 .*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(layout.raw);
        if (!page) throw new Error(layout.raw);
        const [px, py, pw, ph] = page.slice(1).map(Number) as [number, number, number, number];
        const x = Math.round(px + pw * 0.3);
        const y = Math.round(py + ph * (0.15 + i * 0.075));
        sendMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(x, y));
        sendMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(x + pw * 0.4, y + ph * 0.08));
        postMessage(canvas, WM_LBUTTONUP, 0, packCoords(x + pw * 0.4, y + ph * 0.08));
        const dlg = await until(() => findTopWindow(proc.pid!, "#32770"), Boolean).catch(async (e) => {
          throw new Error(`${e}: creating ${names[i]} at ${x},${y}:\n${layout.raw}\n${await dump(client)}`);
        });
        sendText(getDlgItem(dlg, id("IDC_FIELD_NAME")), `Smoke${i}`);
        if (i === 0) sendText(getDlgItem(dlg, id("IDC_FIELD_DEFAULT")), "Persisted");
        postMessage(dlg, WM_COMMAND, 1, 0);
        await until(
          () => findTopWindow(proc.pid!, "#32770"),
          (h) => !h,
        );
        await client.waitForRenderIdle();
        const raw = await dump(client);
        if (!raw.includes(`formFields=${i + 1} widgets=${i < 2 ? i + 1 : i + 2}`)) throw new Error(raw);
      }
      const before = rect(await dump(client), "Smoke0");
      sendMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(before[0]! + 8, before[1]! + 8));
      sendMessage(canvas, WM_LBUTTONUP, 0, packCoords(before[0]! + 8, before[1]! + 8));
      drag(
        canvas,
        before[0]! + before[2]! / 2,
        before[1]! + before[3]! / 2,
        before[0]! + before[2]! / 2 + 12,
        before[1]! + before[3]! / 2 + 8,
      );
      await client.waitForRenderIdle();
      const moved = rect(await dump(client), "Smoke0");
      if (moved[0] === before[0] && moved[1] === before[1]) throw new Error("Widget did not move");
      drag(
        canvas,
        moved[0]! + moved[2]!,
        moved[1]! + moved[3]! / 2,
        moved[0]! + moved[2]! + 14,
        moved[1]! + moved[3]! / 2,
      );
      await client.waitForRenderIdle();
      const resized = rect(await dump(client), "Smoke0");
      if (resized[2]! <= moved[2]!) throw new Error("Widget did not resize");
      const raw = await dump(client);
      const chip = /contents:(-?\d+),(-?\d+),(\d+),(\d+):Field properties/.exec(raw);
      const toolbar = findTopWindow(proc.pid!, "SumatraAnnotEditToolbar");
      if (!chip || !toolbar) throw new Error(`Missing field property toolbar: ${raw}`);
      const origin = clientToScreen(toolbar, 0, 0);
      const cx = +chip[1]! + +chip[3]! / 2 - origin.x;
      const cy = +chip[2]! + +chip[4]! / 2 - origin.y;
      sendMessage(toolbar, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(cx, cy));
      postMessage(toolbar, WM_LBUTTONUP, 0, packCoords(cx, cy));
      const properties = await until(() => findTopWindow(proc.pid!, "#32770"), Boolean);
      sendText(getDlgItem(properties, id("IDC_FIELD_DEFAULT")), "Edited");
      sendText(getDlgItem(properties, id("IDC_FIELD_SIZE")), "17");
      postMessage(properties, WM_COMMAND, 1, 0);
      await until(
        () => findTopWindow(proc.pid!, "#32770"),
        (h) => !h,
      );
      await client.resolveUnsavedChanges("save-as", saved);
    },
    ["-view", "single page", "-zoom", "fit page", source],
  );

  await withControlledApp(
    EXE,
    async (client, proc) => {
      await client.waitForRenderIdle();
      const raw = await dump(client);
      if (
        !raw.includes("formFields=9 widgets=10") ||
        !raw.includes("name=Smoke0 value=Edited default=Edited required=0 readonly=0 size=17")
      ) {
        throw new Error(raw);
      }
      for (let i = 0; i < names.length; i++) {
        if (!raw.includes(`widget type=${i} name=Smoke${i}`)) throw new Error(raw);
      }
      if (!(await client.toggleFormButton(1, 0)).toggled) throw new Error("Reopened checkbox is not fillable");
      if (!(await client.toggleFormButton(1, 2)).toggled) throw new Error("Reopened radio is not fillable");
      const frame = await waitForFrame(proc.pid!);
      sendMessage(frame, WM_COMMAND, cmdId("CmdToggleEditPDF"), 0);
      await client.waitForRenderIdle();
      const canvas = findCanvas(frame);
      const checkbox = rect(await dump(client), "Smoke1");
      sendMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(checkbox[0]! + 4, checkbox[1]! + 4));
      sendMessage(canvas, WM_LBUTTONUP, 0, packCoords(checkbox[0]! + 4, checkbox[1]! + 4));
      sendMessage(frame, WM_COMMAND, cmdId("CmdDeleteAnnotation"), 0);
      if (!(await dump(client)).includes("formFields=8 widgets=9")) throw new Error("Widget was not deleted");
    },
    [saved],
  );
}

if (import.meta.main) await runStandalone(testit);
