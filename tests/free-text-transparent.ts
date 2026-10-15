// The native text editor must preserve page imagery and suppress the old text.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, EXE, runStandalone, tmpPath } from "./util.ts";
import {
  captureWindowPixels,
  captureWindowToPng,
  getClientRect,
  findVisibleChildWindow,
  setProcessDpiAware,
  sendMessage,
  sendText,
  packCoords,
  WM_COMMAND,
  WM_CHAR,
  WM_KEYDOWN,
  WM_LBUTTONDBLCLK,
  MK_LBUTTON,
  VK_ESCAPE,
  VK_TAB,
  VK_SPACE,
  findTopWindow,
  getFocusedHwnd,
  getWindowFontHeight,
  getControlText,
  getScrollInfo,
} from "./winapi.ts";
import { findCanvas, launchControlled, killAndWait, sendCommandSync } from "./win-automation.ts";

type Rect = { x: number; y: number; dx: number; dy: number };

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath("free-text-transparent");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "background.pdf");
  const image = Buffer.alloc(32 * 32 * 3);
  for (let y = 0; y < 32; y++)
    for (let x = 0; x < 32; x++) {
      image.set([185 + 30 * (y % 2), 204 + 30 * (x % 2), 255], (y * 32 + x) * 3);
    }
  const raster = process.env.SWIFTLEAF_TEST_RASTER === "1";
  const photoPath = process.env.SWIFTLEAF_TEST_PHOTO_RGB;
  const rgb = photoPath ? readFileSync(photoPath) : image;
  const side = Math.sqrt(rgb.length / 3);
  if (!Number.isInteger(side)) throw new Error("Photo RGB fixture must be square");
  const colors = process.env.SWIFTLEAF_TEST_DOCUMENT_COLORS ?? "off";
  const page = raster
    ? `q 612 0 0 792 0 0 cm BI /W ${side} /H ${side} /CS /RGB /BPC 8 /F /AHx ID ${rgb.toString("hex")}> EI Q`
    : colors === "off"
      ? ".82 .9 1 rg 0 0 612 792 re f"
      : "1 g 0 0 612 792 re f";
  const content = `q ${page} .2 .4 .8 RG 1 w 0 390 m 612 390 l S Q`;
  const theme = process.env.SWIFTLEAF_TEST_THEME ?? "Light";
  const appdata = join(dir, "appdata");
  mkdirSync(appdata, { recursive: true });
  writeFileSync(
    join(appdata, "Swiftleaf-settings.txt"),
    `UiLanguage = en\nTheme = ${theme}\nDocumentColorsFollowTheme = ${colors}\nRestoreSession = false\nCheckForUpdates = false\n`,
  );
  writeFileSync(
    pdf,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>",
      `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
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
    await client.setNotificationsEnabled(false);
    const canvas = findCanvas(frame);
    const backgroundPixels = captureWindowPixels(canvas);
    if (!backgroundPixels) throw new Error("Could not capture the clean page background");
    const box = () => {
      const hwnd = findVisibleChildWindow(canvas, "Edit");
      if (!hwnd) throw new Error("No active FreeText editor");
      return hwnd;
    };
    const dump = async () => {
      await client.waitForRenderIdle();
      const res = await client.request(ControlCommand.TestMarkupAnnots, []);
      if (res[0] !== 0) throw new Error(String(res));
      return String(res[1]);
    };
    const editorRect = async (): Promise<Rect> => {
      const raw = await dump();
      const r = /freeTextEdit active=1 rect=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw);
      if (!r) throw new Error(`No editor rectangle\n${raw}`);
      return { x: +r[1]!, y: +r[2]!, dx: +r[3]!, dy: +r[4]! };
    };
    const counts = (r: Rect) => {
      const shot = captureWindowPixels(canvas);
      if (!shot) throw new Error("Could not capture editor");
      let blue = 0,
        dark = 0,
        bright = 0,
        total = 0;
      const histogram = new Map<string, number>();
      for (let y = r.y + 4; y < Math.min(shot.h, r.y + r.dy - 4); y++) {
        for (let x = r.x + 6; x < Math.min(shot.w, r.x + r.dx - 6); x++) {
          const i = (y * shot.w + x) * 4;
          const b = shot.data[i]!,
            g = shot.data[i + 1]!,
            red = shot.data[i + 2]!;
          const key = `${red},${g},${b}`;
          histogram.set(key, (histogram.get(key) ?? 0) + 1);
          const expectedB = backgroundPixels.data[i]!,
            expectedG = backgroundPixels.data[i + 1]!,
            expectedR = backgroundPixels.data[i + 2]!;
          // Compare flat page color; the printed rule can have different edge AA in a cropped render.
          if (
            photoPath ||
            colors !== "off" ||
            (expectedB > 240 && expectedR > 150 && expectedR < 240 && expectedG > 180 && expectedG < 245)
          ) {
            if (Math.abs(b - expectedB) < 8 && Math.abs(g - expectedG) < 8 && Math.abs(red - expectedR) < 8) blue++;
            total++;
          }
          if (red < 70 && g < 70 && b < 70) dark++;
          if (red > 170 && g > 170 && b > 170) bright++;
        }
      }
      if (!total) throw new Error("Empty editor sample");
      const i = ((r.y + Math.floor(r.dy / 2)) * shot.w + r.x + Math.floor(r.dx / 2)) * 4;
      return {
        blue,
        dark,
        bright,
        total,
        sample: Array.from(shot.data.slice(i, i + 4)),
        colors: [...histogram].sort((a, b) => b[1] - a[1]).slice(0, 4),
        r,
      };
    };
    const canvasRect = getClientRect(canvas);
    sendMessage(
      frame,
      WM_COMMAND,
      cmdId("CmdCreateAnnotFreeText"),
      packCoords(Math.floor(canvasRect.right / 2), Math.floor(canvasRect.bottom / 2) - 10),
    );
    let r = await editorRect();
    captureWindowToPng(canvas, join(dir, "empty-editor.png"));
    let c = counts(r);
    if (c.blue / c.total < 0.7) throw new Error(`Empty editor obscures page background: ${JSON.stringify(c)}`);
    sendText(box(), "Original editable text");
    r = await editorRect();
    c = counts(r);
    // Glyphs replace background pixels; the empty and cleared checks are stricter.
    if (c.blue / c.total < 0.3) throw new Error(`Typing obscures page background: ${JSON.stringify(c)}`);
    if (theme === "Dark" && colors !== "off" && !raster && c.bright < 10) {
      captureWindowToPng(canvas, join(dir, "unreadable-dark-text.png"));
      throw new Error(`Text is unreadable on the dark page: ${JSON.stringify(c)}`);
    }
    sendMessage(box(), WM_CHAR, 0x0a, 0);
    let raw = await dump();
    const placed = /type=FreeText[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw);
    if (!placed) throw new Error(`No accepted annotation\n${raw}`);
    r = { x: +placed[1]!, y: +placed[2]!, dx: +placed[3]!, dy: +placed[4]! };
    const original = counts(r);
    sendMessage(canvas, WM_LBUTTONDBLCLK, MK_LBUTTON, packCoords(r.x + 6, r.y + 6));
    await editorRect();
    sendText(box(), "");
    const empty = counts(await editorRect());
    captureWindowToPng(canvas, join(dir, "cleared-existing.png"));
    if (
      empty.blue / empty.total < 0.8 ||
      (!photoPath && colors === "off" && empty.dark > Math.max(25, original.dark * 0.25))
    ) {
      throw new Error(`Old glyphs or opaque background remain while editing: ${JSON.stringify({ original, empty })}`);
    }
    sendMessage(box(), WM_KEYDOWN, VK_ESCAPE, 0);
    sendCommandSync(frame, cmdId("CmdUndo"));
    raw = await dump();
    if (!/annotations=0/.test(raw) || !/undo canUndo=0 canRedo=1 modified=0/.test(raw)) {
      throw new Error(`Starting/cancelling an editor changed document history\n${raw}`);
    }
    sendCommandSync(frame, cmdId("CmdZoom100"));
    await client.waitForRenderIdle();
    sendMessage(frame, WM_COMMAND, cmdId("CmdCreateAnnotFreeText"), packCoords(100, 100));
    sendText(box(), "Zoom draft");
    const small = await editorRect();
    const smallFont = getWindowFontHeight(box());
    sendCommandSync(frame, cmdId("CmdZoom200"));
    await client.waitForRenderIdle();
    const large = await editorRect();
    if (large.dy < small.dy * 1.7 || getWindowFontHeight(box()) < smallFont * 1.8) {
      throw new Error(`Zoom did not scale the live editor: ${JSON.stringify({ small, large })}`);
    }
    sendMessage(box(), WM_KEYDOWN, VK_ESCAPE, 0);
    if (raster) {
      sendCommandSync(frame, cmdId("CmdZoom100"));
      await client.waitForRenderIdle();
      sendMessage(frame, WM_COMMAND, cmdId("CmdCreateAnnotFreeText"), packCoords(300, 300));
      const beforeScroll = await editorRect();
      const pos = getScrollInfo(canvas).pos;
      sendMessage(canvas, 0x0115, 1, 0); // WM_VSCROLL, SB_LINEDOWN
      const scrolled = await editorRect();
      if (scrolled.y === beforeScroll.y)
        throw new Error(`Scroll test did not move the draft (position ${pos} -> ${getScrollInfo(canvas).pos})`);
      const active = captureWindowPixels(canvas)!;
      sendMessage(box(), WM_KEYDOWN, VK_ESCAPE, 0);
      await client.waitForRenderIdle();
      const clean = captureWindowPixels(canvas)!;
      let same = 0,
        total = 0;
      for (let y = scrolled.y + 4; y < scrolled.y + scrolled.dy - 4; y++) {
        for (let x = scrolled.x + 6; x < scrolled.x + scrolled.dx - 6; x++) {
          const i = (y * active.w + x) * 4;
          if ([0, 1, 2].every((c) => Math.abs(active.data[i + c]! - clean.data[i + c]!) < 8)) same++;
          total++;
        }
      }
      if (!total || same / total < 0.8) throw new Error(`Scrolling left a stale raster backdrop: ${same}/${total}`);
    }
    sendCommandSync(frame, cmdId("CmdZoomFitPage"));
    await client.waitForRenderIdle();
    const place = () =>
      sendMessage(
        frame,
        WM_COMMAND,
        cmdId("CmdCreateAnnotFreeText"),
        packCoords(Math.floor(getClientRect(canvas).right / 2), Math.floor(getClientRect(canvas).bottom / 2)),
      );
    place();
    sendText(box(), "Unicode: café Ελληνικά 日本語\r\nsecond line");
    const tools = findTopWindow(proc.pid!, "SwiftLeafFreeTextTools");
    if (!tools) throw new Error("No contextual text controls");
    for (const index of [0, 3]) {
      // Font and Color dialogs must preserve the draft on cancel.
      sendMessage(box(), WM_KEYDOWN, VK_TAB, 0);
      for (let i = 0; i < index; i++) sendMessage(tools, WM_KEYDOWN, VK_TAB, 0);
      sendMessage(tools, WM_KEYDOWN, VK_SPACE, 0);
      let dialog = 0;
      const deadline = Date.now() + 3000;
      while (!dialog && Date.now() < deadline) {
        dialog = findTopWindow(proc.pid!, "#32770");
        if (!dialog) await Bun.sleep(20);
      }
      if (!dialog) throw new Error(`Formatting dialog ${index} did not open`);
      sendMessage(dialog, WM_COMMAND, 2, 0); // IDCANCEL
      await editorRect();
      if (!getControlText(box()).includes("日本語")) throw new Error("A formatting dialog lost draft text");
    }
    const beforeFont = getWindowFontHeight(box());
    sendMessage(box(), WM_KEYDOWN, VK_TAB, 0);
    await dump();
    if (getFocusedHwnd(frame) !== tools) throw new Error("Tab did not reach the formatting controls");
    sendMessage(tools, WM_KEYDOWN, VK_TAB, 0); // Smaller
    sendMessage(tools, WM_KEYDOWN, VK_TAB, 0); // Larger
    sendMessage(tools, WM_KEYDOWN, VK_SPACE, 0);
    sendMessage(tools, WM_CHAR, VK_SPACE, 0);
    await dump();
    if (getWindowFontHeight(box()) <= beforeFont || !getControlText(box()).includes("日本語")) {
      captureWindowToPng(tools, join(dir, "failed-toolbar.png"));
      throw new Error(
        `Formatting lost draft text or failed to change the native font: ${JSON.stringify({ beforeFont, afterFont: getWindowFontHeight(box()), text: getControlText(box()) })}\n${await dump()}`,
      );
    }
    captureWindowToPng(canvas, join(dir, "context-tools.png"));
    captureWindowToPng(tools, join(dir, "context-toolbar.png"));
    sendMessage(box(), WM_KEYDOWN, VK_TAB, 0);
    sendMessage(tools, WM_KEYDOWN, VK_ESCAPE, 0);
    if (/freeTextEdit active=1/.test(await dump()))
      throw new Error("Escape from formatting controls did not cancel the draft");
    if (!/annotations=0/.test(await dump())) throw new Error("Cancelling formatting created an annotation");
    const fontByRotation: number[] = [];
    for (let rotation = 0; rotation < 4; rotation++) {
      place();
      sendText(box(), "Rotation draft");
      fontByRotation.push(getWindowFontHeight(box()));
      captureWindowToPng(canvas, join(dir, `rotation-${rotation * 90}.png`));
      sendMessage(box(), WM_KEYDOWN, VK_ESCAPE, 0);
      sendCommandSync(frame, cmdId("CmdRotateRight"));
      await client.waitForRenderIdle();
    }
    if (Math.max(...fontByRotation) > Math.min(...fontByRotation) * 1.7) {
      throw new Error(`Rotation distorted font scaling: ${fontByRotation}`);
    }
    sendCommandSync(frame, cmdId("CmdRotateRight"));
    await client.waitForRenderIdle();
    place();
    sendText(box(), "Rotated saved text");
    sendMessage(box(), WM_CHAR, 0x0a, 0);
    const annotationRect = (raw: string) => {
      const m = /type=FreeText[^\n]*rect=([\d.e+-]+),([\d.e+-]+),([\d.e+-]+),([\d.e+-]+)/.exec(raw);
      if (!m) throw new Error(`No persisted text rectangle\n${raw}`);
      return m.slice(1).map(Number);
    };
    const initial = annotationRect(await dump());
    if (initial[2]! < initial[3]! * 2) {
      throw new Error(`Rotated single-line text swapped PDF dimensions: ${initial}`);
    }
    for (let i = 0; i < 3; i++) {
      const screen = /type=FreeText[^\n]*screen=(-?\d+),(-?\d+),/.exec(await dump());
      if (!screen) throw new Error("No rotated annotation bounds");
      sendMessage(canvas, WM_LBUTTONDBLCLK, MK_LBUTTON, packCoords(+screen[1]! + 6, +screen[2]! + 6));
      await editorRect();
      sendMessage(box(), WM_CHAR, 0x0a, 0);
    }
    const final = annotationRect(await dump());
    if (final.some((n, i) => Math.abs(n - initial[i]!) > 0.5)) {
      throw new Error(`Unchanged rotated editing grew or moved the annotation: ${JSON.stringify({ initial, final })}`);
    }
    const reopen = async () => {
      const screen = /type=FreeText[^\n]*screen=(-?\d+),(-?\d+),/.exec(await dump())!;
      sendMessage(canvas, WM_LBUTTONDBLCLK, MK_LBUTTON, packCoords(+screen[1]! + 6, +screen[2]! + 6));
      await editorRect();
    };
    const clickTool = async (index: number) => {
      sendMessage(box(), WM_KEYDOWN, VK_TAB, 0);
      const host = findTopWindow(proc.pid!, "SwiftLeafFreeTextTools");
      for (let i = 0; i < index; i++) sendMessage(host, WM_KEYDOWN, VK_TAB, 0);
      sendMessage(host, WM_KEYDOWN, VK_SPACE, 0);
      await dump();
    };
    await reopen();
    const unformattedFont = getWindowFontHeight(box());
    await clickTool(2); // Larger
    const formattedFont = getWindowFontHeight(box());
    sendMessage(box(), WM_CHAR, 0x0a, 0);
    sendCommandSync(frame, cmdId("CmdUndo"));
    // Undo clears object selection; Edit mode permits selecting restored text.
    sendCommandSync(frame, cmdId("CmdToggleEditPDF"));
    await reopen();
    if (getWindowFontHeight(box()) !== unformattedFont) throw new Error("One undo did not restore formatting");
    sendMessage(box(), WM_KEYDOWN, VK_ESCAPE, 0);
    sendCommandSync(frame, cmdId("CmdRedo"));
    await reopen();
    if (getWindowFontHeight(box()) !== formattedFont) throw new Error("Redo did not restore formatting");
    await clickTool(5); // Delete
    if (!/annotations=0/.test(await dump())) throw new Error("Contextual Delete did not remove the annotation");
    sendCommandSync(frame, cmdId("CmdUndo"));
    if (!/annotations=1/.test(await dump())) throw new Error("Undo did not restore deleted text");
    sendCommandSync(frame, cmdId("CmdSaveAnnotations"));
    await client.waitForRenderIdle();
    captureWindowToPng(canvas, join(dir, "saved.png"));
    const font = Bun.spawnSync([
      join(dirname(EXE), "sumatrapdf-tool.exe"),
      "show",
      pdf,
      "pages/1/Annots/1/AP/N/Resources/Font/Helv",
    ]);
    if (!font.success || !/\/Type\s+\/Font\b/.test(font.stdout.toString())) {
      throw new Error(`Saved text references a missing font after undo/recreate: ${font.stdout}\n${font.stderr}`);
    }
    console.log("free-text-transparent: OK");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}
if (import.meta.main) await runStandalone(testit);
