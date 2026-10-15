import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, runStandalone, tmpPath } from "./util.ts";
import {
  captureWindowPixels,
  captureWindowToPng,
  findVisibleChildWindow,
  packCoords,
  sendMessage,
  sendText,
  setProcessDpiAware,
  WM_CHAR,
  WM_KEYDOWN,
  WM_LBUTTONDBLCLK,
  MK_LBUTTON,
  VK_ESCAPE,
} from "./winapi.ts";
import { findCanvas, launchControlled, killAndWait, sendCommandSync } from "./win-automation.ts";

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath("free-text-custom-style");
  mkdirSync(dir, { recursive: true });
  const theme = process.env.SWIFTLEAF_TEST_THEME ?? "Light";
  const appdata = join(dir, "appdata");
  mkdirSync(appdata, { recursive: true });
  writeFileSync(
    join(appdata, "Swiftleaf-settings.txt"),
    `Theme = ${theme}\nDocumentColorsFollowTheme = ${theme === "Dark" ? "smart" : "off"}\nCheckForUpdates = false\n`,
  );
  for (const opacity of [1, 0.5]) {
    const pdf = join(dir, `style-${opacity}.pdf`);
    const ap =
      "q /GS gs 1 .8 0 rg 0 0 220 70 re f 0 G 2 w 1 1 218 68 re S BT /Helv 16 Tf 0 g 8 40 Td (User styled text) Tj ET Q";
    writeFileSync(
      pdf,
      assemblePdf([
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [5 0 R] >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        `<< /Type /Annot /Subtype /FreeText /Rect [200 340 420 410] /Contents (User styled text) /DA (/Helv 16 Tf 0 g) /C [1 .8 0] /CA ${opacity} /BS << /W 2 >> /AP << /N 6 0 R >> >>`,
        `<< /Type /XObject /Subtype /Form /BBox [0 0 220 70] /Resources << /Font << /Helv 4 0 R >> /ExtGState << /GS << /ca ${opacity} /CA ${opacity} >> >> >> /Length ${ap.length} >>\nstream\n${ap}\nendstream`,
      ]),
      "latin1",
    );
    const { proc, client, frame } = await launchControlled([
      "-appdata",
      appdata,
      "-view",
      "single page",
      "-zoom",
      "fit page",
      pdf,
    ]);
    try {
      await client.waitForRenderIdle();
      const canvas = findCanvas(frame);
      sendCommandSync(frame, cmdId("CmdToggleEditPDF"));
      const raw = String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]);
      const m = /type=FreeText[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw)!;
      if (!m) throw new Error(raw);
      const [x, y, w, h] = m.slice(1).map(Number) as [number, number, number, number];
      const before = captureWindowPixels(canvas)!;
      captureWindowToPng(canvas, join(dir, `before-${opacity}.png`));
      sendMessage(canvas, WM_LBUTTONDBLCLK, MK_LBUTTON, packCoords(x + Math.floor(w / 2), y + Math.floor(h / 2)));
      await client.waitForRenderIdle();
      const editor = findVisibleChildWindow(canvas, "Edit");
      if (!editor) throw new Error("Styled editor did not open");
      sendText(editor, "");
      const after = captureWindowPixels(canvas)!;
      captureWindowToPng(canvas, join(dir, `editing-${opacity}.png`));
      const i = ((y + h - 20) * before.w + x + w - 20) * 4;
      const a = Array.from(before.data.slice(i, i + 3)),
        b = Array.from(after.data.slice(i, i + 3));
      if (a.some((v, j) => Math.abs(v - b[j]!) > 8))
        throw new Error(`Explicit fill opacity changed while editing at ${opacity}: ${a} -> ${b}`);
      const edge = ((y + Math.floor(h / 3)) * before.w + x + 2) * 4;
      const e = Array.from(before.data.slice(edge, edge + 3)),
        f = Array.from(after.data.slice(edge, edge + 3));
      const beforeContrast = e.reduce((n, v, j) => n + Math.abs(v - a[j]!), 0);
      const afterContrast = f.reduce((n, v, j) => n + Math.abs(v - b[j]!), 0);
      if (afterContrast < beforeContrast * 0.5)
        throw new Error(`Explicit PDF border vanished while editing: ${e} -> ${f}`);
      sendMessage(editor, WM_KEYDOWN, VK_ESCAPE, 0);
    } finally {
      client.close();
      await killAndWait(proc);
    }
  }
  console.log("free-text-custom-style: OK");
}
if (import.meta.main) await runStandalone(testit);
