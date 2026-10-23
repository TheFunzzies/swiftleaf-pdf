import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import type { ControlClient } from "./control.ts";
import { panelRows, selectToolTab, toolsDump } from "./contextual-panel.ts";
import { assemblePdf, pollUntil, runStandalone, SETTINGS_FILE_NAME, tmpPath } from "./util.ts";
import { killAndWait, launchControlled } from "./win-automation.ts";
import {
  clientToScreen,
  getCursorPos,
  hasInteractiveDesktop,
  moveWindow,
  packCoords,
  sendMessage,
  setCursorPos,
  setProcessDpiAware,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  WM_MOUSEMOVE,
} from "./winapi.ts";

function want(ok: boolean, message: string, raw = ""): void {
  if (!ok) throw new Error(`${message}\n${raw}`);
}

function layout(raw: string) {
  const m =
    /panel-layout rail=(\d) toolsPref=(\d+) commentsPref=(\d+) toolsWidth=(\d+) commentsWidth=(\d+) canvasWidth=(\d+) dpi=(\d+)/.exec(
      raw,
    );
  if (!m) throw new Error(`Missing panel layout: ${raw}`);
  return {
    rail: +m[1]!,
    toolsPref: +m[2]!,
    commentsPref: +m[3]!,
    tools: +m[4]!,
    comments: +m[5]!,
    canvas: +m[6]!,
    dpi: +m[7]!,
  };
}

async function settled(client: ControlClient): Promise<string> {
  return pollUntil(
    () => toolsDump(client),
    (s) => /motion .* active=0/.test(s),
    {
      error: (s) => `Animation timer did not become idle: ${s}`,
    },
  );
}

async function drag(client: ControlClient, frame: number, side: "tools" | "comments", delta: number) {
  const raw = await settled(client);
  const m = new RegExp(`panel-handle side=${side} rect=(-?\\d+),(-?\\d+),(-?\\d+),(-?\\d+)`).exec(raw)!;
  want(!!m, `Missing ${side} drag handle`, raw);
  const x = Math.floor((+m[1]! + +m[3]!) / 2);
  const y = +m[2]! + 100;
  const before = getCursorPos();
  try {
    const start = clientToScreen(frame, x, y);
    want(setCursorPos(start.x, start.y), "Cannot position resize cursor");
    sendMessage(frame, WM_LBUTTONDOWN, 1, packCoords(x, y));
    const end = clientToScreen(frame, x + delta, y);
    want(setCursorPos(end.x, end.y), "Cannot move resize cursor");
    sendMessage(frame, WM_MOUSEMOVE, 1, packCoords(x + delta, y));
    const next = await pollUntil(
      () => toolsDump(client),
      (s) => {
        const a = layout(raw),
          b = layout(s);
        return side === "tools" ? a.toolsPref !== b.toolsPref : a.commentsPref !== b.commentsPref;
      },
    );
    want(/toolsRunning=0 commentsRunning=0/.test(next), "Resize drag animated", next);
    sendMessage(frame, WM_LBUTTONUP, 0, packCoords(x + delta, y));
  } finally {
    sendMessage(frame, WM_LBUTTONUP, 0, packCoords(x + delta, y));
    setCursorPos(before.x, before.y);
  }
  return layout(await settled(client));
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  for (const percent of [100, 150, 200]) {
    const dir = tmpPath(`layout-animation-${percent}`);
    mkdirSync(dir, { recursive: true });
    const settingsPath = join(dir, SETTINGS_FILE_NAME);
    writeFileSync(
      settingsPath,
      "UiLanguage = en\nShowToolsPanel = true\nShowCommentsPanel = true\nRestoreSession = false\nCheckForUpdates = false\n",
    );
    const pdf = join(dir, "panel.pdf");
    writeFileSync(
      pdf,
      assemblePdf([
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
      ]),
      "latin1",
    );
    const env = { SUMATRA_DPI_OVERRIDE: String(percent) };
    const { proc, client, frame } = await launchControlled(["-appdata", dir, pdf], { saveSettings: true, env });
    let savedTools = 240,
      savedComments = 300;
    const scale = (n: number) => Math.round((n * percent) / 100);
    try {
      for (const width of [1250, 850, 700, 600, 1250]) {
        // Suppress motion for the whole native resize gesture.
        sendMessage(frame, 0x0231, 0, 0); // WM_ENTERSIZEMOVE
        moveWindow(frame, 0, 0, scale(width), scale(800));
        const raw = await pollUntil(
          () => toolsDump(client),
          (s) => {
            const p = layout(s);
            if (width === 1250) return p.tools === scale(240) && p.comments === scale(300);
            if (width === 850) return p.tools < scale(240) && p.tools >= scale(180) && !p.rail;
            if (width === 700) return p.rail === 1 && p.comments === scale(220);
            return p.rail === 1 && p.comments === 0;
          },
        );
        const p = layout(raw);
        want(p.dpi === Math.round((96 * percent) / 100), "DPI override was not applied", raw);
        want(p.canvas >= scale(320), "Panels consumed the document minimum", raw);
        want(p.toolsPref === 240 && p.commentsPref === 300, "Auto-collapse overwrote preferences", raw);
        want(/toolsRunning=0 commentsRunning=0/.test(raw), "Native resize animated", raw);
        if (p.rail)
          want(
            panelRows(raw).every((r) => r.tip.length > 0),
            "Icon rail lost tooltips",
            raw,
          );
        sendMessage(frame, 0x0232, 0, 0); // WM_EXITSIZEMOVE
      }
      for (const mode of [1, 2, 3, 4, 0]) {
        await selectToolTab(client, frame, mode);
        await settled(client);
      }
      if (percent === 100 && hasInteractiveDesktop()) {
        savedTools = (await drag(client, frame, "tools", 45)).toolsPref;
        savedComments = (await drag(client, frame, "comments", -45)).commentsPref;
        want(
          savedTools >= 180 && savedTools <= 480 && savedComments >= 220 && savedComments <= 480,
          "Drag escaped width limits",
        );
      } else if (percent === 100) {
        console.log(
          "layout-animation: live cursor drag requires an interactive desktop; settings round-trip is unit-tested",
        );
      }
      await client.quit();
      await proc.exited;
      const saved = readFileSync(settingsPath, "utf8");
      want(new RegExp(`ToolsPanelWidth = ${savedTools}\\b`).test(saved), "Tools resize was not saved", saved);
      want(new RegExp(`CommentsPanelWidth = ${savedComments}\\b`).test(saved), "Comments resize was not saved", saved);
    } finally {
      client.close();
      await killAndWait(proc);
    }
    const reopened = await launchControlled(["-appdata", dir, pdf], { env });
    try {
      moveWindow(reopened.frame, 0, 0, scale(1250), scale(800));
      const p = layout(await settled(reopened.client));
      want(p.toolsPref === savedTools && p.commentsPref === savedComments, "Saved panel widths did not reload");
    } finally {
      await reopened.client.quit();
      reopened.client.close();
      await killAndWait(reopened.proc);
    }
  }
}

if (import.meta.main) await runStandalone(testit);
