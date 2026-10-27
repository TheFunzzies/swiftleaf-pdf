import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, pollUntil, runStandalone, SETTINGS_FILE_NAME, tmpPath } from "./util.ts";
import { clickAt, findChildByClass, killAndWait, launchControlled } from "./win-automation.ts";
import {
  moveWindow,
  getClientRect,
  getWindowRect,
  postMessage,
  VK_DOWN,
  WM_KEYDOWN,
  sendText,
  setProcessDpiAware,
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
    const names = [...raw.matchAll(/^panel-favorite cmd=\d+ active=\d rect=.*? name=(.*)$/gm)];
    want(
      names.length > 0 && names.every((m) => /^Add to favorites: .+/.test(m[1]!)),
      "Favorites need action and tool names",
      raw,
    );
    const categories = new Set(rows.map((r) => r.category));
    want(categories.has("Recommended"), "Tools panel must include Recommended category", raw);
    want(categories.has("Export"), "Tools panel must include Export category", raw);
    want(categories.has("Edit"), "Tools panel must include Edit category", raw);

    // 2. Search by tool name and description
    const search = findChildByClass(panel, "Edit");
    // Search remains inside the panel at the minimum supported window size.
    moveWindow(frame, 30, 30, 800, 600);
    await client.waitForRenderIdle();
    const searchRect = getWindowRect(search);
    const panelRect = getWindowRect(panel);
    want(searchRect.left >= panelRect.left && searchRect.right <= panelRect.right, "Search clips at 800x600");
    sendText(search, "compress");
    await pollUntil(
      () => toolsDump(client),
      (s) => panelRows(s).length > 0 && panelRows(s).every((r) => r.text === "Compress a PDF"),
    );
    postMessage(search, WM_KEYDOWN, VK_DOWN, 0);
    await pollUntil(
      () => toolsDump(client),
      (s) => /panel-focus text=Compress a PDF/.test(s),
      { error: (s) => `Down from search must focus a matching tool: ${s}` },
    );
    const bounds = getClientRect(panel);
    const focused = panelRows(await toolsDump(client)).find((r) => r.text === "Compress a PDF");
    want(!!focused && focused.y >= 0 && focused.y + focused.dy <= bounds.bottom, "Focused tool must be visible");
    moveWindow(frame, 30, 30, 1200, 800);
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
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
