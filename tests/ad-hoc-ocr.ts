// Native Windows OCR integration smoke. Requires an installed OCR language pack.
// Run: bun tests/ad-hoc-ocr.ts --no-build
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand, withControlledApp } from "./control.ts";
import { EXE, assemblePdf, runStandalone, tmpPath } from "./util.ts";
import {
  captureWindowToPng,
  enumWindows,
  enumChildWindows,
  getClassName,
  findChildWindow,
  getControlText,
  getClipboardText,
  getWindowPid,
  getWindowText,
  getWindowRect,
  WM_KEYDOWN,
  VK_RETURN,
  postMessage,
  sleep,
  WM_CLOSE,
} from "./winapi.ts";
import { pressEnter, pressTab, typeIntoInput } from "./win-automation.ts";

async function waitFor<T>(read: () => T, ready: (v: T) => boolean, label: string, timeoutMs = 30_000): Promise<T> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const value = read();
    if (ready(value)) return value;
    await sleep(50);
  }
  throw new Error(`OCR: timed out waiting for ${label}`);
}

function topWindow(pid: number, title: string): number {
  let found = 0;
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) === pid && getWindowText(hwnd) === title) found = hwnd;
    return true;
  });
  return found;
}

function fixture(): string {
  const first = "BT /F1 36 Tf 72 600 Td (12345) Tj ET";
  const second = "BT /F1 36 Tf 72 600 Td (67890) Tj ET";
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Count 2 /Kids [3 0 R 5 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 7 0 R >> >> /Contents 4 0 R >>",
    `<< /Length ${first.length} >>\nstream\n${first}\nendstream`,
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R >>",
    `<< /Length ${second.length} >>\nstream\n${second}\nendstream`,
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
  ]);
}

export async function testit(): Promise<void> {
  const dir = tmpPath("ad-hoc-ocr");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "two-pages.pdf");
  writeFileSync(pdf, fixture(), "latin1");
  const appdata = join(dir, "appdata");
  mkdirSync(appdata, { recursive: true });
  writeFileSync(join(appdata, "SumatraPDF-settings.txt"), "UiLanguage = en\nRestoreSession = false\n");

  await withControlledApp(
    EXE,
    async (client, proc) => {
      await client.waitForRenderIdle();
      await client.request(ControlCommand.TestInvokeCommand, ["CmdGoToLastPage"]);
      await client.waitForRenderIdle();
      const visible = await client.request(ControlCommand.TestCommandVisibility, ["CmdScanOcr", "palette", 0, 0]);
      if (!String(visible[1]).includes("vis=show")) throw new Error(`OCR missing from palette: ${visible}`);
      await client.request(ControlCommand.TestInvokeCommand, ["CmdScanOcr"]);
      const dlg = await waitFor(() => topWindow(proc.pid, "Scan & OCR"), Boolean, "OCR dialog");
      const edit = findChildWindow(dlg, "Edit");
      if (!edit) throw new Error("OCR result edit missing");
      await pressEnter(dlg);
      const current = await waitFor(
        () => getControlText(edit),
        (s) => s.includes("67890"),
        "current-page OCR",
      );
      if (current.includes("12345")) throw new Error(`OCR used the wrong page: ${current}`);

      // The initial focus is Current page; Tab selects All pages.
      await pressTab(dlg);
      await pressEnter(dlg);
      const all = await waitFor(
        () => getControlText(edit),
        (s) => s.includes("12345") && s.includes("67890"),
        "all-page OCR",
      );
      if (!all.includes("Page 1") || !all.includes("Page 2")) throw new Error(`Missing page separators: ${all}`);
      if (getWindowRect(edit).bottom > getWindowRect(dlg).bottom - 70) {
        throw new Error("OCR results leave no room for action buttons");
      }
      captureWindowToPng(dlg, join(dir, "results.png"));

      // All pages -> Selected pages -> Copy text -> Save PDF.
      await pressTab(dlg);
      await pressTab(dlg);
      await pressEnter(dlg);
      if ((await getClipboardText()) !== all) throw new Error("Clipboard OCR differs from result text");
      await pressTab(dlg);
      await pressEnter(dlg);
      const save = await waitFor(() => topWindow(proc.pid, "Save As"), Boolean, "PDF save dialog");
      const filename = await waitFor(
        () => {
          let field = 0;
          enumChildWindows(save, (h) => {
            if (getClassName(h) === "Edit" && /^Searchable(?:\.pdf)?$/i.test(getControlText(h))) field = h;
            return true;
          });
          return field;
        },
        Boolean,
        "initialized save filename edit",
      );
      const dest = join(dir, `ocr-${Date.now()}.pdf`);
      await typeIntoInput(filename, dest, false);
      if (getControlText(filename) !== dest) throw new Error("Save filename was not set");
      await pressEnter(save);
      await waitFor(
        () => topWindow(proc.pid, "Save As"),
        (h) => h === 0,
        "Save As submit",
        5_000,
      );
      let saved = "";
      try {
        saved = await waitFor(
          () => {
            try {
              return readFileSync(dest, "utf8");
            } catch {
              return "";
            }
          },
          (s) => s.startsWith("%PDF-"),
          "searchable PDF export",
        );
      } catch (e) {
        const controls: string[] = [];
        enumChildWindows(dlg, (hwnd) => {
          controls.push(`${getClassName(hwnd)}=${getWindowText(hwnd)}`);
          return true;
        });
        throw new Error(`${e}; dialog controls: ${controls.join(" | ")}`);
      }
      if (!saved.startsWith("%PDF-")) throw new Error("Saved output is not a PDF");
      postMessage(dlg, WM_CLOSE, 0, 0);
      await waitFor(
        () => topWindow(proc.pid, "Scan & OCR"),
        (h) => h === 0,
        "dialog close",
      );

      for (const needle of ["12345", "67890"]) {
        const [, found] = await client.request(ControlCommand.TestSearch, [dest, needle]);
        if (!String(found).startsWith("FOUND")) throw new Error(`Saved OCR search failed: ${found}`);
      }

      // Start another job, then close its document while it runs.
      await client.request(ControlCommand.TestInvokeCommand, ["CmdScanOcr"]);
      const closing = await waitFor(() => topWindow(proc.pid, "Scan & OCR"), Boolean, "second OCR dialog");
      postMessage(closing, WM_KEYDOWN, VK_RETURN, 0);
      await client.request(ControlCommand.TestInvokeCommand, ["CmdCloseCurrentDocument"]);
      postMessage(closing, WM_CLOSE, 0, 0);
      await client.request(ControlCommand.Ping);
      console.log(`OCR: current page, all pages, PDF save/reopen/search, and close-during-work passed (${dir})`);
    },
    ["-appdata", appdata, pdf],
  );
}

if (import.meta.main) await runStandalone(testit);
