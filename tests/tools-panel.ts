import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, pollUntil, runStandalone, SETTINGS_FILE_NAME, tmpPath } from "./util.ts";
import { clickAt, findChildByClass, killAndWait, launchControlled } from "./win-automation.ts";
import {
  moveWindow,
  getFocusedHwnd,
  getWindowRect,
  isWindowVisible,
  sendMessage,
  sendText,
  setProcessDpiAware,
  WM_KEYDOWN,
} from "./winapi.ts";
import { TOOLS_PANEL_CLASS, panelRows, selectToolTab, toolsDump } from "./contextual-panel.ts";

export * from "./contextual-panel.ts";

function want(condition: boolean, message: string, raw = ""): void {
  if (!condition) throw new Error(`${message}\n${raw}`);
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath("tools-panel");
  mkdirSync(dir, { recursive: true });
  writeFileSync(
    join(dir, SETTINGS_FILE_NAME),
    "UiLanguage = en\nShowToolsPanel = true\nRestoreSession = false\nCheckForUpdates = false\n",
  );
  const pdf = join(dir, "doc.pdf");
  writeFileSync(
    pdf,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
    ]),
    "latin1",
  );
  const { proc, client, frame } = await launchControlled(["-appdata", dir, pdf]);
  try {
    moveWindow(frame, 30, 30, 1200, 800);
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const panel = findChildByClass(frame, TOOLS_PANEL_CLASS);
    want(panel !== 0, "Tools panel window must exist");

    // 1. Structure of tools panel
    const raw = await selectToolTab(client, frame, 0);
    const rows = panelRows(raw);
    want(rows.length > 0, "Tools panel must have rows", raw);
    want(
      rows.every((r) => r.tip.length > 0),
      "Every tool row must have a tooltip",
      raw,
    );
    const categories = new Set(rows.map((r) => r.category));
    want(categories.has("Recommended"), "Tools panel must include Recommended category", raw);
    want(categories.has("Export"), "Tools panel must include Export category", raw);
    want(categories.has("Edit"), "Tools panel must include Edit category", raw);

    // 2. Search by tool name and description
    const search = findChildByClass(panel, "Edit");
    sendText(search, "compress");
    let state = await pollUntil(
      () => toolsDump(client),
      (s) => panelRows(s).length >= 1 && panelRows(s).every((r) => r.text === "Compress a PDF"),
      { error: (s) => `Tool search did not filter by name: ${s}` },
    );
    sendText(search, "reduce the document file size");
    state = await pollUntil(
      () => toolsDump(client),
      (s) => panelRows(s).length >= 1 && panelRows(s).every((r) => r.text === "Compress a PDF"),
      { error: (s) => `Tool search did not filter by description: ${s}` },
    );
    sendText(search, "");
    state = await pollUntil(
      () => toolsDump(client),
      (s) => panelRows(s).length > 5,
      { error: (s) => `Clearing tool search did not restore rows: ${s}` },
    );

    // 3. Favorites toggle
    let favorite = /panel-favorite cmd=(\d+) active=0 rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)/.exec(state);
    want(favorite !== null, "Tool favorite button should be exposed", state);
    await clickAt(panel, (+favorite![2]! + +favorite![4]!) / 2, (+favorite![3]! + +favorite![5]!) / 2);
    state = await pollUntil(
      () => toolsDump(client),
      (s) => /panel-favorite cmd=\d+ active=1/.test(s),
      { error: (s) => `Favoriting a tool did not update its state: ${s}` },
    );
    favorite = /panel-favorite cmd=(\d+) active=1 rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)/.exec(state);
    await clickAt(panel, (+favorite![2]! + +favorite![4]!) / 2, (+favorite![3]! + +favorite![5]!) / 2);
    state = await pollUntil(
      () => toolsDump(client),
      (s) => /panel-favorite cmd=\d+ active=0/.test(s),
      { error: (s) => `Unfavoriting a tool did not update its state: ${s}` },
    );

    // 4. Category collapse and expand
    let category = /panel-category=Export expanded=1 rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)/.exec(state);
    want(category !== null, "Export category header should be exposed", state);
    await clickAt(panel, (+category![1]! + +category![3]!) / 2, (+category![2]! + +category![4]!) / 2);
    state = await pollUntil(
      () => toolsDump(client),
      (s) => /panel-category=Export expanded=0/.test(s),
      { error: (s) => `Category did not collapse: ${s}` },
    );
    category = /panel-category=Export expanded=0 rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)/.exec(state);
    await clickAt(panel, (+category![1]! + +category![3]!) / 2, (+category![2]! + +category![4]!) / 2);
    await pollUntil(
      () => toolsDump(client),
      (s) => /panel-category=Export expanded=1/.test(s),
      { error: (s) => `Category did not expand` },
    );

    moveWindow(frame, 0, 0, 800, 600);
    await client.waitForRenderIdle();
    const panelRect = getWindowRect(panel);
    const searchRect = getWindowRect(search);
    if (isWindowVisible(search)) {
      want(
        searchRect.left >= panelRect.left && searchRect.right <= panelRect.right,
        "Tools search must fit the 800x600 panel",
      );
    } else {
      const narrow = await toolsDump(client);
      want(/panel-layout rail=1/.test(narrow), "Narrow panel must expose the tool rail when search is hidden");
      const dpi = Number(/dpi=(\d+)/.exec(narrow)?.[1] ?? 96);
      want(
        panelRows(narrow).every((row) => row.dx >= (20 * dpi) / 96),
        "Rail tool icons must retain visible width",
        narrow,
      );
    }
    moveWindow(frame, 0, 0, 1200, 800);
    await client.waitForRenderIdle();
    await clickAt(search, 8, 8);
    await pollUntil(
      () => getFocusedHwnd(frame),
      (hwnd) => hwnd === search,
      { error: (hwnd) => `Search did not take focus: ${hwnd}` },
    );
    sendMessage(search, WM_KEYDOWN, 9, 0);
    await pollUntil(
      () => getFocusedHwnd(frame),
      (hwnd) => hwnd === panel,
      { error: (hwnd) => `Search Tab did not enter tools: focus=${hwnd}, panel=${panel}` },
    );
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
