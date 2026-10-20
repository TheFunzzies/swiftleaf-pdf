import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, EXE, runStandalone, tmpPath } from "./util.ts";
import {
  enumChildWindows,
  getClassName,
  isWindowVisible,
  MK_LBUTTON,
  packCoords,
  sendMessage,
  sendText,
  sleep,
  VK_ESCAPE,
  WM_KEYDOWN,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

function fixture(content: string, annots = "", font = "Helvetica"): string {
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> /Contents 4 0 R ${annots ? "/Annots [7 0 R]" : ""} >>`,
    `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
    `<< /Type /Font /Subtype /Type1 /BaseFont /${font} >>`,
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>",
    ...(annots ? [annots] : []),
  ]);
}

export async function testit(): Promise<void> {
  const dir = tmpPath("edit-text-roundtrip");
  mkdirSync(dir, { recursive: true });
  const cases = [
    {
      name: "clicked-plain",
      content: "BT /F1 12 Tf 72 700 Td (Original paragraph with a long first line) Tj 0 -15 Td (Second line) Tj ET",
      annots: "<< /Type /Annot /Subtype /Square /Rect [0 0 612 792] /C [] /BS << /W 0 >> >>",
      click: true,
      replacement: "Cancelled",
      cancel: true,
      reject: false,
      unchanged: true,
      original: "Original paragraph",
    },
    {
      name: "overlap",
      content:
        "BT /F1 12 Tf 72 700 Td (Original paragraph with a long first line) Tj 0 -15 Td (Second line) Tj ET BT /F2 20 Tf 0 1 -1 0 210 700 Tm (X) Tj ET",
      annots: "<< /Type /Annot /Subtype /Square /Rect [0 0 612 792] /C [] /BS << /W 0 >> >>",
      click: true,
      reject: true,
      keep: "X",
    },
    {
      name: "overflow",
      content: "BT /F1 12 Tf 72 700 Td (Original text) Tj ET",
      replacement: "A very long replacement ".repeat(40),
      reject: false,
      unchanged: true,
    },
    { name: "invisible", content: "BT /F1 12 Tf 3 Tr 72 700 Td (OCR text) Tj ET", reject: true },
    { name: "clip", content: "BT /F1 12 Tf 7 Tr 72 700 Td (Clipping text) Tj ET", reject: true },
    { name: "mixed", content: "BT /F1 12 Tf 72 700 Td (Plain ) Tj /F2 12 Tf (bold) Tj ET", reject: true },
    {
      name: "rollback",
      content: "BT /F1 12 Tf 72 700 Td (Original text) Tj ET",
      replacement: "New text",
      reject: false,
      unchanged: true,
      fault: true,
    },
    {
      name: "link",
      content: "BT /F1 12 Tf 72 700 Td (Linked text) Tj ET",
      annots: "<< /Type /Annot /Subtype /Link /Rect [70 695 160 715] /A << /S /URI /URI (https://example.com) >> >>",
      reject: true,
    },
    {
      name: "freetext",
      content: "BT /F1 12 Tf 72 700 Td (Original text) Tj ET",
      annots:
        "<< /Type /Annot /Subtype /FreeText /Rect [70 695 160 715] /Contents (Keep note) /DA (/Helv 12 Tf 0 g) >>",
      reject: true,
    },
    {
      name: "cancel",
      content: "BT /F1 12 Tf 72 700 Td (Original text) Tj ET",
      replacement: "Cancelled",
      cancel: true,
      reject: false,
      unchanged: true,
    },
    {
      name: "tab",
      content: "BT /F1 12 Tf 72 700 Td (Original text) Tj ET",
      replacement: "New\ttext",
      reject: false,
      expected: "New text",
    },
    {
      name: "ligature",
      content: "BT /F1 12 Tf 72 700 Td (Original text) Tj ET",
      replacement: "ﬁle",
      reject: false,
      expected: "file",
    },
    {
      name: "replacement-character",
      content: "BT /F1 12 Tf 72 700 Td (Original text) Tj ET",
      replacement: "�",
      reject: false,
      unchanged: true,
    },
    {
      name: "serif",
      font: "Times-Roman",
      content: "BT /F1 14 Tf 72 700 Td (Original serif text) Tj ET",
      replacement: "New serif text",
      reject: false,
      expected: "New serif text",
    },
    {
      name: "mono",
      font: "Courier",
      content: "BT /F1 10 Tf 72 700 Td (Original mono text) Tj ET",
      replacement: "New mono text",
      reject: false,
      expected: "New mono text",
    },
    {
      name: "reflow",
      content: "BT /F1 12 Tf 72 700 Td (Original first line) Tj 0 -15 Td (Original next line) Tj ET",
      replacement: "New text goes on two lines",
      reject: false,
      expected: "New text goes on two lines",
    },
  ];
  const selectedCase = process.env.SWIFTLEAF_TEXT_CASE;
  for (const c of cases) {
    if (selectedCase && c.name !== selectedCase) continue;
    const pdf = join(dir, `${c.name}.pdf`);
    writeFileSync(pdf, fixture(c.content, c.annots, c.font), "latin1");
    const { proc, client, frame } = await launchControlled([pdf], {
      env: { SWIFTLEAF_TEST_EDIT_TEXT_FAIL: c.fault ? "after-redaction" : "" },
    });
    try {
      await client.waitForRenderIdle();
      await client.setNotificationsEnabled(false);
      if (c.click) {
        const before = String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]);
        const page = /type=Square[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(before);
        if (!page) throw new Error(`${c.name}: missing page coordinate marker\n${before}`);
        const x = Math.round(+page[1]! + (+page[3]! * 80) / 612);
        const y = Math.round(+page[2]! + (+page[4]! * 88) / 792);
        sendCommandSync(frame, cmdId("CmdEditText"));
        sendMessage(findCanvas(frame), WM_LBUTTONDOWN, MK_LBUTTON, packCoords(x, y));
        sendMessage(findCanvas(frame), WM_LBUTTONUP, 0, packCoords(x, y));
      } else {
        await client.seedTextSelection(1);
        sendCommandSync(frame, cmdId("CmdEditText"));
      }
      const editor = () => {
        let found = 0;
        enumChildWindows(findCanvas(frame), (hwnd) => {
          if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) found = hwnd;
          return true;
        });
        return found;
      };
      if (c.reject) {
        if (editor()) throw new Error(`${c.name}: unsafe text was editable`);
      } else {
        const hwnd = editor();
        if (!hwnd) throw new Error(`${c.name}: editor did not open`);
        sendText(hwnd, c.replacement!);
        sendMessage(hwnd, WM_KEYDOWN, c.cancel ? VK_ESCAPE : 13, 0);
        for (let i = 0; i < 100 && editor(); i++) await sleep(20);
      }
      const result = await client.request(ControlCommand.TestMarkupAnnots, []);
      const raw = String(result[1]);
      if ((c.reject || c.unchanged) && !/undo canUndo=0 canRedo=0 modified=0/.test(raw)) {
        throw new Error(`${c.name}: rejected/cancelled edit changed the document\n${raw}`);
      }
      if (c.unchanged && !raw.includes(`page1text=${c.original ?? "Original text"}`))
        throw new Error(`${c.name}: original text was lost\n${raw}`);
      if (c.keep && !raw.includes(c.keep)) throw new Error(`${c.name}: unselected text was lost\n${raw}`);
      if (c.expected) {
        if (editor()) throw new Error(`${c.name}: supported text was rejected`);
        sendCommandSync(frame, cmdId("CmdSaveAnnotations"));
        await client.waitForRenderIdle();
        const output = join(dir, `${c.name}.txt`);
        const extract = Bun.spawnSync([
          join(dirname(EXE), "sumatrapdf-tool.exe"),
          "draw",
          "-F",
          "txt",
          "-o",
          output,
          pdf,
          "1",
        ]);
        if (!extract.success || !readFileSync(output, "utf8").replace(/\s+/g, " ").includes(c.expected))
          throw new Error(`${c.name}: saved text did not survive reopening`);
      }
    } finally {
      client.close();
      await killAndWait(proc);
    }
  }
}

if (import.meta.main) {
  await runStandalone(testit);
}
