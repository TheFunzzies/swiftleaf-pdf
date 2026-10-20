import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, EXE, runStandalone, tmpPath } from "./util.ts";
import {
  enumChildWindows,
  getClassName,
  isWindowVisible,
  sendMessage,
  sendText,
  sleep,
  VK_ESCAPE,
  WM_KEYDOWN,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

function fixture(content: string, annots = ""): string {
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> /Contents 4 0 R ${annots ? "/Annots [7 0 R]" : ""} >>`,
    `<< /Length ${content.length} >>\nstream\n${content}\nendstream`,
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>",
    ...(annots ? [annots] : []),
  ]);
}

export async function testit(): Promise<void> {
  const dir = tmpPath("edit-text-roundtrip");
  mkdirSync(dir, { recursive: true });
  const cases = [
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
  ];
  for (const c of cases) {
    const pdf = join(dir, `${c.name}.pdf`);
    writeFileSync(pdf, fixture(c.content, c.annots), "latin1");
    const { proc, client, frame } = await launchControlled([pdf], {
      env: { SWIFTLEAF_TEST_EDIT_TEXT_FAIL: c.fault ? "after-redaction" : "" },
    });
    try {
      await client.waitForRenderIdle();
      await client.setNotificationsEnabled(false);
      await client.seedTextSelection(1);
      sendCommandSync(frame, cmdId("CmdEditText"));
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
      if (c.unchanged && !raw.includes("page1text=Original text"))
        throw new Error(`${c.name}: original text was lost\n${raw}`);
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
        if (!extract.success || !readFileSync(output, "utf8").includes(c.expected))
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
