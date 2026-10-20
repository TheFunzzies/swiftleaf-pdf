import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, runStandalone, SLOW_BUILD_FACTOR, tmpPath } from "./util.ts";
import {
  MK_LBUTTON,
  packCoords,
  postMessage,
  sendMessage,
  sleep,
  VK_ESCAPE,
  WM_KEYDOWN,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  WM_MOUSEMOVE,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

export async function testit(): Promise<void> {
  const dir = tmpPath("stamp-resize-lifecycle");
  mkdirSync(dir, { recursive: true });
  const png = join(dir, "signature.png");
  writeFileSync(
    png,
    Buffer.from(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==",
      "base64",
    ),
  );
  writeFileSync(
    join(dir, "Swiftleaf-settings.txt"),
    `RestoreSession = false\nCheckForUpdates = false\nAnnotations [\n SignatureImage = ${png}\n]\n`,
  );
  const pdf = assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
  ]);
  const a = join(dir, "a.pdf");
  const b = join(dir, "b.pdf");
  writeFileSync(a, pdf, "latin1");
  writeFileSync(b, pdf, "latin1");
  const { proc, client, frame } = await launchControlled(["-appdata", dir, a, b]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    sendCommandSync(frame, cmdId("CmdToggleEditPDF"));
    const canvas = findCanvas(frame);
    for (const interruption of ["escape", "tab"]) {
      await client.request(ControlCommand.TestInvokeCommand, ["CmdSignWithImage"]);
      await client.waitForRenderIdle();
      const dump = async () => String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]);
      const deadline = Date.now() + 5000 * SLOW_BUILD_FACTOR;
      let raw = "";
      let rect: RegExpExecArray | null = null;
      while (Date.now() < deadline) {
        raw = await dump();
        rect = /type=Stamp[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw);
        if (rect) break;
        await sleep(40);
      }
      if (!rect) throw new Error(`Missing stamp\n${raw}`);
      const x = +rect[1]! + +rect[3]!;
      const y = +rect[2]! + +rect[4]!;
      sendMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(x, y));
      sendMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(x + 30, y + 30));
      if (interruption === "escape") {
        postMessage(frame, WM_KEYDOWN, VK_ESCAPE, 0);
        await dump();
      } else {
        sendCommandSync(frame, cmdId("CmdPrevTab"));
        await client.waitForRenderIdle();
        sendCommandSync(frame, cmdId("CmdNextTab"));
        await client.waitForRenderIdle();
      }
      sendMessage(canvas, WM_LBUTTONUP, 0, packCoords(x + 30, y + 30));
      sendCommandSync(frame, cmdId("CmdUndo"));
      const after = await dump();
      if (!/annotations=0/.test(after)) throw new Error(`${interruption}: resize left a no-op undo step\n${after}`);
    }
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) {
  await runStandalone(testit);
}
