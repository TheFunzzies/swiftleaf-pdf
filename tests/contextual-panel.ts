import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlClient, ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, pollUntil, runStandalone, SETTINGS_FILE_NAME, tmpPath } from "./util.ts";
import {
  clickAt,
  findChildByClass,
  killAndWait,
  launchControlled,
  pressKey,
  waitForContextMenu,
} from "./win-automation.ts";
import {
  getClientRect,
  getMenuItemCount,
  getMenuItemDisabled,
  getMenuItemId,
  getPopupMenuHandle,
  getWindowText,
  getWindowPid,
  enumWindows,
  enumChildWindows,
  isWindowVisible,
  moveWindow,
  packCoords,
  postMessage,
  sendMessage,
  setProcessDpiAware,
  VK_ESCAPE,
  VK_RETURN,
  VK_RIGHT,
  WM_KEYDOWN,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  WM_CLOSE,
} from "./winapi.ts";

export const TOOLS_PANEL_CLASS = "SWIFTLEAF_TOOLS_PANEL";
export const RIBBON_CLASS = "SUMATRA_VIRT_TOOLBAR";

export type PanelRow = {
  idx: number;
  cmd: number;
  enabled: boolean;
  checked: boolean;
  x: number;
  y: number;
  dx: number;
  dy: number;
  text: string;
  tip: string;
};

export async function toolsDump(client: ControlClient): Promise<string> {
  return String((await client.request(ControlCommand.TestToolbarButtons, []))[1] ?? "");
}

export function panelRows(raw: string): PanelRow[] {
  return [
    ...raw.matchAll(
      /^panel-row=(\d+) cmd=(\d+) enabled=(\d) checked=(\d) rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+) text=(.*?) tip=(.*)$/gm,
    ),
  ].map((m) => ({
    idx: +m[1]!,
    cmd: +m[2]!,
    enabled: m[3] === "1",
    checked: m[4] === "1",
    x: +m[5]!,
    y: +m[6]!,
    dx: +m[7]! - +m[5]!,
    dy: +m[8]! - +m[6]!,
    text: m[9]!,
    tip: m[10]!.trim(),
  }));
}

export async function selectToolTab(client: ControlClient, frame: number, mode: number): Promise<string> {
  const raw = await toolsDump(client);
  const m = new RegExp(`ribbon-tab=${mode} active=\\d rect=(-?\\d+),(-?\\d+),(-?\\d+),(-?\\d+)`).exec(raw);
  if (!m) throw new Error(`Missing ribbon tab ${mode}\n${raw}`);
  await clickAt(findChildByClass(frame, RIBBON_CLASS), (+m[1]! + +m[3]!) / 2, (+m[2]! + +m[4]!) / 2, 0);
  return pollUntil(
    () => toolsDump(client),
    (s) => new RegExp(`ribbon-tab=${mode} active=1`).test(s),
    { error: (s) => `Tab ${mode} did not activate: ${s}` },
  );
}

export async function panelButton(client: ControlClient, frame: number, cmd: number): Promise<PanelRow | null> {
  const panel = findChildByClass(frame, TOOLS_PANEL_CLASS);
  const bounds = getClientRect(panel);
  const height = bounds.bottom - bounds.top;
  const WM_VSCROLL = 0x0115;
  sendMessage(panel, WM_VSCROLL, 6, 0); // SB_TOP
  for (let i = 0; i < 100; i++) {
    const row = panelRows(await toolsDump(client)).find((r) => r.cmd === cmd);
    if (!row) return null;
    if (row.y >= 0 && row.y + row.dy <= height) return row;
    sendMessage(panel, WM_VSCROLL, row.y < 0 ? 0 : 1, 0);
  }
  throw new Error(`Could not scroll to tool ${cmd}`);
}

function want(condition: boolean, message: string, raw = ""): void {
  if (!condition) throw new Error(`${message}\n${raw}`);
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const dir = tmpPath("contextual-panel");
  mkdirSync(dir, { recursive: true });
  writeFileSync(
    join(dir, SETTINGS_FILE_NAME),
    "UiLanguage = en\nShowToolsPanel = true\nRestoreSession = false\nCheckForUpdates = false\n",
  );
  const pdf = join(dir, "pages.pdf");
  writeFileSync(
    pdf,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 2 /Kids [3 0 R 4 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
    ]),
    "latin1",
  );
  const { proc, client, frame } = await launchControlled(["-appdata", dir, pdf]);
  try {
    moveWindow(frame, 30, 30, 1400, 950);
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const panel = findChildByClass(frame, TOOLS_PANEL_CLASS);
    const ribbon = findChildByClass(frame, RIBBON_CLASS);
    let height = 0;
    for (const theme of [0, 1]) {
      if (theme) await client.request(ControlCommand.TestInvokeCommand, ["CmdToggleLightDarkTheme"]);
      for (const [mode, title, command] of [
        [0, "All tools", "CmdConvertPdfToImages"],
        [1, "Read", "CmdSinglePageView"],
        [2, "Edit", "CmdEditText"],
        [3, "Convert", "CmdInsertBlankPage"],
        [4, "E-Sign", "CmdCreateSignature"],
      ] as const) {
        const raw = await selectToolTab(client, frame, mode);
        const rows = panelRows(raw);
        want(getWindowText(panel) === title, `Wrong panel title for ${title}`, raw);
        want(
          rows.some((r) => r.cmd === cmdId(command)),
          `Missing ${command}`,
          raw,
        );
        want(
          rows.every((r) => r.tip.length > 0),
          "Every tool needs a tooltip",
          raw,
        );
        if (!mode) {
          want(rows.length === 33, "All tools must retain its 33 rows", raw);
          for (const command of ["CmdPdfSanitize", "CmdPdfRedactSearch"]) {
            want(
              rows.some((r) => r.cmd === cmdId(command) && r.enabled),
              `Missing enabled ${command}`,
              raw,
            );
          }
        } else want(!rows.some((r) => r.text === "Export a PDF"), "Fixed All tools rows leaked into a mode", raw);
        const dy = getClientRect(ribbon).bottom - getClientRect(ribbon).top;
        if (!height) height = dy;
        want(dy === height, "Tab switching changed slim ribbon height", raw);
      }
    }

    for (const [command, title, checkboxes] of [
      ["CmdPdfSanitize", "Sanitize Document", 7],
      ["CmdPdfRedactSearch", "Redact All Search Matches", 1],
    ] as const) {
      await client.request(ControlCommand.TestInvokeCommand, [command]);
      const dialog = await pollUntil(
        async () => {
          let found = 0;
          enumWindows((hwnd) => {
            if (getWindowPid(hwnd) === proc.pid && getWindowText(hwnd) === title && isWindowVisible(hwnd)) found = hwnd;
            return true;
          });
          return found;
        },
        Boolean,
        { error: () => `${title} dialog did not open` },
      );
      let checks = 0;
      enumChildWindows(dialog, (hwnd) => {
        const text = getWindowText(hwnd);
        if (
          text === "Metadata" ||
          text.startsWith("Hidden text/layers") ||
          text.startsWith("Embedded JavaScript") ||
          text === "Attachments" ||
          text === "Comments" ||
          text === "Form data and fields" ||
          text === "Thumbnails" ||
          text.startsWith("Regular expression")
        )
          checks++;
        return true;
      });
      want(checks === checkboxes, `${title} lost selectable options: ${checks}`);
      postMessage(dialog, WM_CLOSE, 0, 0);
      await pollUntil(
        async () => isWindowVisible(dialog),
        (visible) => !visible,
      );
    }

    // Read tab: assert a row triggers its command (CmdFacingView)
    await selectToolTab(client, frame, 1);
    const facingRow = await panelButton(client, frame, cmdId("CmdFacingView"));
    want(facingRow !== null, "CmdFacingView row should exist");
    await clickAt(panel, facingRow!.x + Math.floor(facingRow!.dx / 2), facingRow!.y + Math.floor(facingRow!.dy / 2));
    await pollUntil(
      async () => String((await client.request(ControlCommand.TestDisplayMode, ["get"]))[1] ?? ""),
      (m) => /facing/.test(m),
      { error: (m) => `Clicking Facing View row did not trigger command: ${m}` },
    );

    // Keyboard navigation: Arrow and Enter select the next tab
    await selectToolTab(client, frame, 1);
    await pressKey(ribbon, VK_RIGHT);
    await pressKey(ribbon, VK_RETURN);
    await pollUntil(
      () => toolsDump(client),
      (s) => /toolsPanel page=2 .*title=Edit/.test(s),
      { error: (s) => `Keyboard did not activate Edit: ${s}` },
    );

    // Convert tab: document state disables CmdInsertBlankPage on close
    await selectToolTab(client, frame, 3);
    const before = panelRows(await toolsDump(client));
    want(
      before.find((r) => r.cmd === cmdId("CmdInsertBlankPage"))?.enabled === true,
      "Blank page should be enabled on PDF",
    );
    await client.request(ControlCommand.TestInvokeCommand, ["CmdClose"]);
    const raw = await pollUntil(
      () => toolsDump(client),
      (s) => panelRows(s).find((r) => r.cmd === cmdId("CmdInsertBlankPage"))?.enabled === false,
    );
    want(/title=Convert/.test(raw), "Closing a document changed the active tab", raw);
    want(panelRows(raw).length === before.length, "Disabled tools disappeared", raw);

    // Narrow window (<700px): panel collapses and tab click opens fallback menu
    const dpi = +(await toolsDump(client)).match(/panel-layout .* dpi=(\d+)/)![1]!;
    moveWindow(frame, 30, 30, Math.round((480 * dpi) / 96), 900);
    await pollUntil(
      () => toolsDump(client),
      (s) => /panel-layout rail=1/.test(s),
      {
        error: (s) => `Narrow tools panel did not become an icon rail: ${s}`,
      },
    );
    moveWindow(frame, 30, 30, Math.round((350 * dpi) / 96), 900);
    await pollUntil(
      async () => isWindowVisible(panel),
      (v) => !v,
    );
    const tabs = await toolsDump(client);
    const m = /ribbon-tab=3 active=1 rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)/.exec(tabs)!;
    const lp = packCoords((+m[1]! + +m[3]!) / 2, (+m[2]! + +m[4]!) / 2);
    postMessage(ribbon, WM_LBUTTONDOWN, 1, lp);
    postMessage(ribbon, WM_LBUTTONUP, 0, lp);
    const popup = await waitForContextMenu(5000);
    const menu = getPopupMenuHandle(popup);
    let found = false;
    for (let i = 0; i < getMenuItemCount(menu); i++)
      if (getMenuItemId(menu, i) === cmdId("CmdInsertBlankPage")) {
        found = true;
        want(getMenuItemDisabled(menu, i), "Fallback lost disabled state");
      }
    want(found, "Narrow fallback omitted the tab's tools");
    postMessage(popup, WM_KEYDOWN, VK_ESCAPE, 0);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
