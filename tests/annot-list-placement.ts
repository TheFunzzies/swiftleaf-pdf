// Comments dock beside the document, reserve canvas space, and restore their
// placement after toggling, resizing, and leaving fullscreen.
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { waitCommentsPanel } from "./comments-panel.ts";
import { assemblePdf, cmdId, runStandalone, tmpPath } from "./util.ts";
import { getWindowRect, getWorkArea, moveWindow, setProcessDpiAware } from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

function makePdf(): string {
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [4 0 R] >>",
    "<< /Type /Annot /Subtype /Text /P 3 0 R /Rect [72 50 92 70] /Contents (n) >>",
  ]);
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath("annot-list-placement");
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "a.pdf");
  writeFileSync(pdf, makePdf(), "latin1");
  const { proc, client, frame } = await launchControlled(["-view", "single page", "-zoom", "fit page", pdf]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const wa = getWorkArea();
    moveWindow(frame, wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top);
    sendCommandSync(frame, cmdId("CmdToggleEditPDF"));
    const canvas = findCanvas(frame);
    const toggle = () => sendCommandSync(frame, cmdId("CmdFindAnnotation"));
    const docked = async () => {
      const panel = await waitCommentsPanel(client, frame, true);
      // Visibility precedes the deferred positioning; wait for a stable layout.
      await client.waitForRenderIdle();
      const p = getWindowRect(panel);
      const c = getWindowRect(canvas);
      const f = getWindowRect(frame);
      if (
        p.left < c.right ||
        Math.abs(p.top - c.top) > 2 ||
        Math.abs(p.bottom - c.bottom) > 2 ||
        p.right > f.right ||
        p.bottom > f.bottom ||
        p.right <= p.left ||
        c.right <= c.left
      ) {
        throw new Error(
          `annot-list-placement: panel must dock to the right without covering the canvas: ${JSON.stringify({ p, c, f })}`,
        );
      }
      return { p, c };
    };
    toggle();
    const first = await docked();
    toggle();
    await waitCommentsPanel(client, frame, false);
    await client.waitForRenderIdle();
    const expanded = getWindowRect(canvas);
    if (expanded.right - expanded.left < first.c.right - first.c.left + first.p.right - first.p.left - 2) {
      throw new Error("annot-list-placement: hiding Comments did not return its space to the document");
    }
    toggle();
    const again = await docked();
    if (JSON.stringify(first) !== JSON.stringify(again)) {
      throw new Error(`annot-list-placement: toggle changed dock placement: ${JSON.stringify({ first, again })}`);
    }

    // Narrow windows collapse Comments while retaining the user's open state.
    const fr = getWindowRect(frame);
    moveWindow(frame, fr.left, fr.top, 650, fr.bottom - fr.top);
    await waitCommentsPanel(client, frame, false);
    moveWindow(frame, fr.left, fr.top, fr.right - fr.left, fr.bottom - fr.top);
    await docked();

    // Fullscreen hides chrome; leaving it must restore the docked list.
    sendCommandSync(frame, cmdId("CmdToggleFullscreen"));
    await waitCommentsPanel(client, frame, false);
    sendCommandSync(frame, cmdId("CmdToggleFullscreen"));
    await docked();
    const raw = String((await client.request(ControlCommand.TestAnnotFilter, []))[1] ?? "");
    if (!/nAll=1 nVisible=1/.test(raw)) throw new Error(`Comments lost its annotation after relayout\n${raw}`);
  } finally {
    client.close();
    await killAndWait(proc);
  }
  console.log("annot-list-placement: OK");
}

if (import.meta.main) await runStandalone(testit);
