// Crash recovery: real PDF edits, killed process, live-instance exclusion, restore/discard.
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand, type ControlClient } from "./control.ts";
import { SETTINGS_FILE_NAME, assemblePdf, cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import {
  enumChildWindows,
  enumWindows,
  getClassName,
  getClientRect,
  getWindowPid,
  isWindowVisible,
  packCoords,
  sendMessage,
  sendText,
  WM_CHAR,
  WM_COMMAND,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

function check(ok: boolean, message: string): asserts ok {
  if (!ok) throw new Error(message);
}
async function recovery(client: ControlClient, action: number, dest = ""): Promise<string> {
  const result = await client.request(ControlCommand.TestRecovery, [action, dest]);
  check(result[0] === 0, `recovery command failed: ${result}`);
  if (action === 0) {
    await pollUntil(
      async () => String((await client.request(ControlCommand.TestRecovery, [4, ""]))[1] ?? ""),
      (pending) => pending === "0",
    );
  }
  return String(result[1] ?? "");
}
async function markup(client: ControlClient): Promise<string> {
  return String((await client.request(ControlCommand.TestMarkupAnnots, []))[1] ?? "");
}
async function edit(frame: number, client: ControlClient): Promise<void> {
  await client.waitForRenderIdle();
  const canvas = findCanvas(frame);
  const size = getClientRect(canvas);
  sendMessage(frame, WM_COMMAND, cmdId("CmdCreateAnnotFreeText"), packCoords(size.right / 2, size.bottom / 2));
  const box = await pollUntil(async () => {
    let found = 0;
    enumChildWindows(canvas, (hwnd) => {
      if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) found = hwnd;
      return true;
    });
    return found;
  }, Boolean);
  sendText(box, "Recovered edit");
  sendMessage(box, WM_CHAR, 0x0a, 0);
  await pollUntil(
    () => markup(client),
    (raw) => /annotations=1/.test(raw) && /modified=1/.test(raw),
  );
}

export async function testit(): Promise<void> {
  const root = tmpPath(`edit-reliability-${Date.now()}`);
  const appdata = join(root, "appdata");
  mkdirSync(appdata, { recursive: true });
  writeFileSync(
    join(appdata, SETTINGS_FILE_NAME),
    "UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\n",
  );
  const original = join(root, "original.pdf");
  writeFileSync(
    original,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
    ]),
    "latin1",
  );
  const before = readFileSync(original);
  const launch = (pdf = original) =>
    launchControlled(["-appdata", appdata, "-view", "single page", "-zoom", "fit page", pdf]);
  let app = await launch();
  let stage = "first edit";
  try {
    await app.client.waitForRenderIdle();
    check((await recovery(app.client, 0)) === "", "clean documents must not create recovery files");
    await edit(app.frame, app.client);
    const session = await recovery(app.client, 0);
    const snapshots = readdirSync(session).filter((name) => name.endsWith(".pdf"));
    check(snapshots.length === 1, "dirty document needs exactly one snapshot");
    const snapshot = join(session, snapshots[0]!);
    const savedTime = statSync(snapshot).mtimeMs;
    await recovery(app.client, 0);
    check(statSync(snapshot).mtimeMs === savedTime, "unchanged dirty documents must not be rewritten");
    check(readFileSync(original).equals(before), "autosave must leave original bytes untouched");
    sendCommandSync(app.frame, cmdId("CmdUndo"));
    await pollUntil(
      () => markup(app.client),
      (raw) => /annotations=0/.test(raw),
    );
    sendCommandSync(app.frame, cmdId("CmdRedo"));
    await pollUntil(
      () => markup(app.client),
      (raw) => /annotations=1/.test(raw),
    );
    await recovery(app.client, 0);

    stage = "live-instance exclusion";
    const other = await launch();
    try {
      check((await recovery(other.client, 1)) === "0", "live instance snapshots must not be offered");
      await other.client.quit();
      await other.proc.exited;
    } finally {
      other.client.close();
      await killAndWait(other.proc);
    }
    app.client.close();
    await killAndWait(app.proc);
    check(existsSync(snapshot), "process termination must retain snapshot");

    stage = "restore crashed document";
    app = await launch();
    check((await recovery(app.client, 1)) === "1", "next launch must find the crashed document");
    const restored = join(root, "restored.pdf");
    await recovery(app.client, 2, restored);
    check(existsSync(restored) && !existsSync(snapshot), "restore must publish a copy and remove the snapshot");
    check(readFileSync(original).equals(before), "restore must not overwrite original");
    await app.client.quit();
    await app.proc.exited;
    app.client.close();
    stage = "read restored annotations";
    app = await launch(restored);
    await pollUntil(
      () => markup(app.client),
      (raw) => /annotations=1/.test(raw),
    );
    await app.client.quit();
    await app.proc.exited;
    app.client.close();

    stage = "second edit";
    app = await launch();
    await edit(app.frame, app.client);
    const secondSession = await recovery(app.client, 0);
    stage = "save cleanup";
    await app.client.resolveUnsavedChanges("save");
    check(!readdirSync(secondSession).some((p) => p.endsWith(".pdf")), "normal save must clean recovery");
    await app.client.quit();
    await app.proc.exited;
    app.client.close();

    // An independent leftover validates Discard without a modal dialog.
    const stale = join(appdata, "recovery", "discard-test");
    mkdirSync(stale, { recursive: true });
    writeFileSync(join(stale, "document.pdf"), before);
    writeFileSync(join(stale, "document.pdf.path"), original);
    stage = "discard leftover";
    // Control-only launches may save settings, but must never offer modal recovery.
    app = await launchControlled(["-appdata", appdata, original], { saveSettings: true });
    await app.client.waitForRenderIdle();
    let dialog = false;
    enumWindows((hwnd) => {
      if (getWindowPid(hwnd) === app.proc.pid && getClassName(hwnd) === "#32770" && isWindowVisible(hwnd))
        dialog = true;
      return true;
    });
    check(!dialog, "controlled startup must not show a recovery dialog");
    check((await recovery(app.client, 1)) === "1", "discard fixture must be discoverable");
    await recovery(app.client, 3);
    check(!existsSync(join(stale, "document.pdf")), "Discard must remove leftover recovery files");
    await app.client.quit();
    await app.proc.exited;
  } catch (error) {
    throw new Error(`${stage}: ${(error as Error).message}`);
  } finally {
    app.client.close();
    await killAndWait(app.proc);
  }
}
if (import.meta.main) await runStandalone(testit);
