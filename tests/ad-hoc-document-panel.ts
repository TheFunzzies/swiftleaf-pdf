// Keyboard/lifetime regression for the floating form banner. Run with --no-build
// after a Debug build. Screenshots and generated PDF sources stay in tests/tmp.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { cmdId, runStandalone, tmpPath } from "./util.ts";
import {
  captureWindowToPng,
  findChildWindow,
  getFocusedHwnd,
  isWindowVisible,
  postMessage,
  sleep,
  setProcessDpiAware,
  WM_KEYDOWN,
  VK_RETURN,
  VK_TAB,
} from "./winapi.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

function pdf(withForm: boolean): string {
  const objects = [
    `<< /Type /Catalog /Pages 2 0 R ${withForm ? "/AcroForm << /Fields [4 0 R] >>" : ""} >>`,
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] ${withForm ? "/Annots [4 0 R]" : ""} >>`,
    "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [70 650 300 680] /P 3 0 R >>",
  ];
  let data = "%PDF-1.4\n";
  const offsets = objects.map((object, i) => {
    const offset = data.length;
    data += `${i + 1} 0 obj\n${object}\nendobj\n`;
    return offset;
  });
  const xref = data.length;
  data += `xref\n0 ${objects.length + 1}\n0000000000 65535 f \n`;
  data += offsets.map((offset) => `${String(offset).padStart(10, "0")} 00000 n \n`).join("");
  return `${data}trailer\n<< /Root 1 0 R /Size ${objects.length + 1} >>\nstartxref\n${xref}\n%%EOF\n`;
}

async function waitFor(check: () => boolean, message: string): Promise<void> {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    if (check()) return;
    await sleep(30);
  }
  throw new Error(message);
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath("document-panel");
  mkdirSync(dir, { recursive: true });
  writeFileSync(join(dir, "Swiftleaf-settings.txt"), "UiLanguage = en\nTheme = Light\nCheckForUpdates = false\n");
  const form = join(dir, "form.pdf");
  const plain = join(dir, "plain.pdf");
  writeFileSync(form, pdf(true));
  writeFileSync(plain, pdf(false));
  const { proc, client, frame } = await launchControlled([
    "-appdata",
    dir,
    "-window-pos",
    "1400x900@20x20",
    plain,
    form,
  ]);
  try {
    await client.waitForRenderIdle();
    const banner = findChildWindow(frame, "SWIFTLEAF_FORM_BANNER");
    if (!banner) throw new Error("form banner was not created");
    await waitFor(() => isWindowVisible(banner), "form banner did not appear");
    for (let i = 0; i < 8 && getFocusedHwnd(frame) !== banner; i++) {
      sendCommandSync(frame, cmdId("CmdMoveFrameFocus"));
    }
    await waitFor(() => getFocusedHwnd(frame) === banner, "F6 cycle did not reach the banner");
    captureWindowToPng(frame, join(dir, "banner-light.png"));
    // First stop highlights, Tab reaches dismiss; Enter must close the banner.
    postMessage(banner, WM_KEYDOWN, VK_RETURN, 0);
    await sleep(100);
    postMessage(banner, WM_KEYDOWN, VK_TAB, 0);
    postMessage(banner, WM_KEYDOWN, VK_RETURN, 0);
    await waitFor(() => !isWindowVisible(banner), "keyboard dismiss failed");
    sendCommandSync(frame, cmdId("CmdPrevTab"));
    await client.waitForRenderIdle();
    if (isWindowVisible(banner)) throw new Error("banner leaked into a document with no fields");
    sendCommandSync(frame, cmdId("CmdNextTab"));
    await client.waitForRenderIdle();
    if (isWindowVisible(banner)) throw new Error("tab switch lost the dismissal");
    sendCommandSync(frame, cmdId("CmdReloadDocument"));
    await waitFor(() => isWindowVisible(banner), "reload did not reset the form cache/dismissal");
    sendCommandSync(frame, cmdId("CmdToggleLightDarkTheme"));
    await client.waitForRenderIdle();
    captureWindowToPng(frame, join(dir, "banner-dark.png"));
    console.log(`Banner keyboard/tab/reload checks passed; screenshots: ${dir}`);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
