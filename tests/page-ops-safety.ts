// Page changes, Undo Page Change, Split and Compress on real PDFs (B1 review findings
// E1, E7, E9, S1, U9 and Q3). Each part checks the file on disk with a separate engine.
import assert from "node:assert/strict";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlClient, ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, runStandalone, SLOW_BUILD_FACTOR, tmpPath, writeAppdata } from "./util.ts";
import {
  enumChildWindows,
  enumWindows,
  getClassName,
  getWindowPid,
  getWindowText,
  isWindowVisible,
  packCoords,
  postMessage,
  sendCopyDataW,
  sendMessage,
  sendText,
  sleep,
  VK_RETURN,
  WM_CHAR,
  WM_CLOSE,
  WM_COMMAND,
  WM_KEYDOWN,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

const SETTINGS = "UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\n";
const IDOK = 1;
const IDCANCEL = 2;
const kCopyDataCmd = 0x44646557;

async function until<T>(probe: () => T | Promise<T>, message: string): Promise<T> {
  const deadline = Date.now() + 8000 * SLOW_BUILD_FACTOR;
  while (Date.now() < deadline) {
    const result = await probe();
    if (result) return result;
    await sleep(40);
  }
  throw new Error(message);
}

// a visible window of the app with this title; prompt: only message boxes
function windowFor(pid: number, title: string, prompt = false): number {
  let found = 0;
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) !== pid || getWindowText(hwnd) !== title || !isWindowVisible(hwnd)) return true;
    if (prompt !== (getClassName(hwnd) === "#32770")) return true;
    found = hwnd;
    return false;
  });
  return found;
}

function edits(parent: number): number[] {
  const result: number[] = [];
  enumChildWindows(parent, (hwnd) => {
    if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) result.push(hwnd);
    return true;
  });
  return result;
}

// posts a command that asks first, then answers the prompt titled `title`
async function answerPrompt(pid: number, frame: number, cmd: string, title: string, answer: number): Promise<void> {
  postMessage(frame, WM_COMMAND, cmdId(cmd), 0);
  const box = await until(() => windowFor(pid, title, true), `${cmd} did not ask "${title}"`);
  postMessage(box, WM_COMMAND, answer, 0);
  await until(() => !windowFor(pid, title, true), `"${title}" did not close`);
}

// one page per word; the words find their page in the saved file
function pagesPdf(words: string[], extra: { annots?: string[]; objects?: string[] } = {}): string {
  const first = 3;
  const objects = [
    "<< /Type /Catalog /Pages 2 0 R >>",
    `<< /Type /Pages /Count ${words.length} /Kids [${words.map((_, i) => `${first + 2 * i} 0 R`).join(" ")}] >>`,
  ];
  const font = first + 2 * words.length;
  words.forEach((word, i) => {
    const annots = i === 0 && extra.annots ? ` /Annots [${extra.annots.join(" ")}]` : "";
    objects.push(
      `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 ${font} 0 R >> >> /Contents ${first + 2 * i + 1} 0 R${annots} >>`,
    );
    const content = `BT /F1 18 Tf 72 650 Td (${word}) Tj ET`;
    objects.push(`<< /Length ${content.length} >>\nstream\n${content}\nendstream`);
  });
  objects.push("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  objects.push(...(extra.objects ?? []));
  return assemblePdf(objects);
}

async function pageOf(client: ControlClient, pdf: string, word: string): Promise<number> {
  const raw = String((await client.request(ControlCommand.TestSearch, [pdf, word]))[1]);
  const m = /FOUND needle=\S+ page=(\d+)/.exec(raw);
  if (/^ERROR/.test(raw)) throw new Error(`search failed: ${raw}`);
  return m ? +m[1]! : 0;
}

async function order(client: ControlClient, pdf: string, words: string[]): Promise<number[]> {
  const pages: number[] = [];
  for (const w of words) pages.push(await pageOf(client, pdf, w));
  return pages;
}

async function waitOrder(client: ControlClient, pdf: string, words: string[], want: number[], what: string) {
  let last: number[] = [];
  try {
    await until(async () => {
      last = await order(client, pdf, words);
      return last.join() === want.join();
    }, what);
  } catch {
    throw new Error(`${what}: pages of ${words.join(",")} are ${last.join(",")}, want ${want.join(",")}`);
  }
  await client.waitForRenderIdle();
}

async function annotState(client: ControlClient): Promise<string> {
  return String((await client.request(ControlCommand.TestMarkupAnnots, []))[1]);
}

function closePrompts(pid: number) {
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) === pid && getClassName(hwnd) === "#32770") postMessage(hwnd, WM_COMMAND, IDCANCEL, 0);
    return true;
  });
}

// Q3, U9, E1, E7: change pages, undo back to the original bytes, keep annotations,
// and ask before deleting or before losing unsaved edits
async function testPageChanges(): Promise<void> {
  const dir = tmpPath("page-ops-safety-pages");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "pages.pdf");
  const words = ["ALPHA", "BRAVO", "CHARLIE"];
  const note = "<< /Type /Annot /Subtype /Text /Rect [500 700 520 720] /Contents (kept) >>";
  writeFileSync(pdf, pagesPdf(words, { annots: [note] }), "latin1");
  const original = readFileSync(pdf);
  const appdata = writeAppdata("page-ops-safety-pages-appdata", SETTINGS);
  const { proc, client, frame } = await launchControlled(["-appdata", appdata, "-view", "single page", pdf]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const pid = proc.pid!;

    sendCommandSync(frame, cmdId("CmdMovePageDown"));
    await waitOrder(client, pdf, words, [2, 1, 3], "Move Page Down");

    // Delete Page asks; cancel keeps the file, OK deletes the current page (ALPHA, page 2).
    // Go there explicitly: the reload doesn't reliably leave the moved page current
    sendCommandSync(frame, cmdId("CmdGoToFirstPage"));
    sendCommandSync(frame, cmdId("CmdGoToNextPage"));
    await client.waitForRenderIdle();
    const moved = readFileSync(pdf);
    await answerPrompt(pid, frame, "CmdDeleteCurrentPage", "Delete Page", IDCANCEL);
    await client.waitForRenderIdle();
    assert.deepEqual(readFileSync(pdf), moved, "cancelled Delete Page changed the PDF");
    await answerPrompt(pid, frame, "CmdDeleteCurrentPage", "Delete Page", IDOK);
    await waitOrder(client, pdf, words, [0, 1, 2], "Delete Page");

    // each undo steps back; the last one restores the original bytes
    postMessage(frame, WM_COMMAND, cmdId("CmdUndoPageChange"), 0);
    await waitOrder(client, pdf, words, [2, 1, 3], "first Undo Page Change");
    postMessage(frame, WM_COMMAND, cmdId("CmdUndoPageChange"), 0);
    await until(() => readFileSync(pdf).equals(original), "second Undo Page Change did not restore the original");
    await client.waitForRenderIdle();

    sendCommandSync(frame, cmdId("CmdGoToFirstPage"));
    sendCommandSync(frame, cmdId("CmdInsertBlankPage"));
    await waitOrder(client, pdf, words, [1, 3, 4], "Insert Blank Page");
    assert.ok(readFileSync(pdf, "latin1").includes("(kept)"), "Insert Blank Page lost the saved note");

    // an unsaved note: page changes and undo ask before writing or discarding it
    sendMessage(frame, WM_COMMAND, cmdId("CmdCreateAnnotFreeText"), packCoords(120, 250));
    const editor = await until(() => edits(findCanvas(frame))[0], "FreeText editor missing");
    sendText(editor, "Keep this unsaved note");
    sendMessage(editor, WM_CHAR, 0x0a, 0);
    await client.waitForRenderIdle();
    const inserted = readFileSync(pdf);
    assert.match(await annotState(client), /modified=1/, "the note should be unsaved");

    await answerPrompt(pid, frame, "CmdRotatePageRight", "Page Change", IDCANCEL);
    await client.waitForRenderIdle();
    assert.deepEqual(readFileSync(pdf), inserted, "cancelled rotate wrote the PDF");
    await answerPrompt(pid, frame, "CmdUndoPageChange", "Undo Page Change", IDCANCEL);
    await client.waitForRenderIdle();
    assert.deepEqual(readFileSync(pdf), inserted, "cancelled undo changed the PDF");
    const state = await annotState(client);
    assert.match(state, /modified=1/, "a cancelled prompt lost the unsaved edits");
    assert.match(state, /type=FreeText/, "a cancelled prompt lost the note");
  } finally {
    closePrompts(proc.pid!);
    client.close();
    await killAndWait(proc);
  }
}

// S1, E9 and the R2 note: Split and Compress export the live document of the tab
// they were opened for, unsaved redactions included, and ask before replacing files
async function testExports(): Promise<void> {
  const dir = tmpPath("page-ops-safety-export");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "source.pdf");
  const redact =
    "<< /Type /Annot /Subtype /Redact /Rect [60 620 310 700] /QuadPoints [60 700 310 700 60 620 310 620] >>";
  writeFileSync(pdf, pagesPdf(["SECRET", "KEEP"], { annots: [redact] }), "latin1");
  const original = readFileSync(pdf);
  const other = join(dir, "other.pdf");
  writeFileSync(other, pagesPdf(["PUBLIC", "ELSE"]), "latin1");
  const base = join(dir, `part-${Date.now()}`);
  const appdata = writeAppdata("page-ops-safety-export-appdata", SETTINGS);
  const { proc, client, frame } = await launchControlled(["-appdata", appdata, "-view", "single page", pdf]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const pid = proc.pid!;
    sendCommandSync(frame, cmdId("CmdApplyRedactions"));
    await client.waitForRenderIdle();
    const redacted = await annotState(client);
    assert.match(redacted, /annotations=0/, "the redaction was not applied");
    assert.match(redacted, /modified=1/, "the redaction should be unsaved");
    assert.deepEqual(readFileSync(pdf), original, "the redaction must stay unsaved");

    // open Split for source.pdf, then switch to another tab before running it
    sendCommandSync(frame, cmdId("CmdSplitPdf"));
    let dialog = await until(() => windowFor(pid, "Split PDF"), "Split dialog missing");
    let fields = edits(dialog);
    assert.equal(fields.length, 2, "Split dialog fields");
    sendText(fields[0]!, `${base}.pdf`);
    sendText(fields[1]!, "1");
    assert.ok(sendCopyDataW(frame, kCopyDataCmd, `[Open("${other}", 0, 1, 0)]`));
    await client.waitForRenderIdle();
    postMessage(dialog, WM_KEYDOWN, VK_RETURN, 0);
    await until(() => existsSync(`${base}-2.pdf`), "Split did not write both parts");
    await until(() => !windowFor(pid, "Split PDF"), "Split dialog did not close");
    const part1 = `${base}-1.pdf`;
    assert.equal(await pageOf(client, part1, "SECRET"), 0, "Split leaked the unsaved redacted text");
    assert.equal(await pageOf(client, part1, "PUBLIC"), 0, "Split exported the current tab, not its own");
    assert.equal(await pageOf(client, `${base}-2.pdf`, "KEEP"), 1, "Split lost the second page");
    assert.deepEqual(readFileSync(pdf), original, "Split must leave the source untouched");

    // splitting again onto the same names asks; cancel keeps the parts
    const written = readFileSync(part1);
    assert.ok(sendCopyDataW(frame, kCopyDataCmd, `[Open("${pdf}", 0, 1, 0)]`));
    await client.waitForRenderIdle();
    sendCommandSync(frame, cmdId("CmdSplitPdf"));
    dialog = await until(() => windowFor(pid, "Split PDF"), "second Split dialog missing");
    fields = edits(dialog);
    sendText(fields[0]!, `${base}.pdf`);
    sendText(fields[1]!, "1");
    postMessage(dialog, WM_KEYDOWN, VK_RETURN, 0);
    const box = await until(() => windowFor(pid, "Split PDF", true), "Split replaced files without asking");
    postMessage(box, WM_COMMAND, IDCANCEL, 0);
    await until(() => !windowFor(pid, "Split PDF", true), "the replace prompt did not close");
    assert.deepEqual(readFileSync(part1), written, "a cancelled Split replaced a part");
    postMessage(dialog, WM_CLOSE, 0, 0);
    await until(() => !windowFor(pid, "Split PDF"), "Split dialog did not close");

    // Compress reads the live document too
    const compressed = join(dir, `compressed-${Date.now()}.pdf`);
    sendCommandSync(frame, cmdId("CmdPdfCompress"));
    dialog = await until(() => windowFor(pid, "Compress PDF"), "Compress dialog missing");
    fields = edits(dialog);
    assert.ok(fields.length >= 1, "Compress dialog fields");
    sendText(fields[0]!, compressed);
    postMessage(dialog, WM_KEYDOWN, VK_RETURN, 0);
    await until(() => existsSync(compressed), "Compress wrote nothing");
    await client.waitForRenderIdle();
    assert.equal(await pageOf(client, compressed, "SECRET"), 0, "Compress leaked the unsaved redacted text");
    assert.equal(await pageOf(client, compressed, "KEEP"), 2, "Compress lost the second page");
  } finally {
    closePrompts(proc.pid!);
    client.close();
    await killAndWait(proc);
  }
}

export async function testit(): Promise<void> {
  await testPageChanges();
  await testExports();
}

if (import.meta.main) {
  await runStandalone(testit);
}
