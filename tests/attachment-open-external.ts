// Supported HTML attachments open in-app. Unsupported attachments are safely
// extracted and revealed in Explorer, never passed to a file association.
//
// A fresh temp directory and app log make accidental disk extraction visible.
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { cmdId, runStandalone, tmpPath } from "./util.ts";
import { WM_CLOSE, enumWindows, getClassName, getClientRect, getWindowText, postMessage, sleep } from "./winapi.ts";
import { clickAt, findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

const kAttachData = "hello attachment\n";

// One page with a FileAttachment icon in its center.
function attachmentPdf(fileName: string): string {
  const objs = [
    `<< /Type /Catalog /Pages 2 0 R >>`,
    `<< /Type /Pages /Kids [3 0 R] /Count 1 >>`,
    `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Annots [4 0 R] >>`,
    `<< /Type /Annot /Subtype /FileAttachment /Rect [90 90 110 110] /FS 5 0 R >>`,
    `<< /Type /Filespec /F (${fileName}) /UF (${fileName}) /EF << /F 6 0 R >> >>`,
    `<< /Type /EmbeddedFile /Length ${kAttachData.length} >>\nstream\n${kAttachData}\nendstream`,
  ];
  let out = "%PDF-1.4\n";
  const offsets: number[] = [];
  objs.forEach((o, i) => {
    offsets.push(out.length);
    out += `${i + 1} 0 obj\n${o}\nendobj\n`;
  });
  const xref = out.length;
  out += `xref\n0 ${objs.length + 1}\n0000000000 65535 f \n`;
  for (const off of offsets) {
    out += `${String(off).padStart(10, "0")} 00000 n \n`;
  }
  out += `trailer\n<< /Size ${objs.length + 1} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
  return out;
}

function explorerWindows(): Set<number> {
  const res = new Set<number>();
  enumWindows((hwnd) => {
    if (getClassName(hwnd) === "CabinetWClass") {
      res.add(hwnd);
    }
    return true;
  });
  return res;
}

// explorer windows opened since `before`
function newExplorerWindows(before: Set<number>): number[] {
  return [...explorerWindows()].filter((w) => !before.has(w));
}

async function testAttachment(root: string, fileName: string, inApp: boolean): Promise<void> {
  const dir = join(root, inApp ? "html" : "binary");
  rmSync(dir, { recursive: true, force: true });
  const tempDir = join(dir, `attach-tmp-${process.pid}`);
  mkdirSync(tempDir, { recursive: true });
  const pdf = join(dir, "attachment.pdf");
  writeFileSync(pdf, attachmentPdf(fileName));

  const logPath = join(dir, "log.txt");
  const env = { TMP: tempDir, TEMP: tempDir };
  const { proc, client, frame } = await launchControlled(["-log-to-file", logPath, pdf], { env });
  const before = explorerWindows();
  try {
    await client.waitForRenderIdle();
    sendCommandSync(frame, cmdId("CmdZoomFitPage"));
    await client.waitForRenderIdle();
    const canvas = findCanvas(frame);
    const rc = getClientRect(canvas);
    const click = () => clickAt(canvas, Math.floor(rc.right / 2), Math.floor(rc.bottom / 2));
    await click();

    const route = inApp
      ? /LaunchEmbeddedDestination: opened in app '[^']+'/
      : /LaunchEmbeddedDestination: showing '([^']+)'/;
    const deadline = Date.now() + 10_000;
    let lastClick = Date.now();
    let log = "";
    while (Date.now() < deadline) {
      log = existsSync(logPath) ? readFileSync(logPath, "utf8") : "";
      if (route.test(log)) {
        break;
      }
      // a click that didn't reach the link: try again
      if (!log.includes("GotoLink: opening file attachment") && Date.now() - lastClick > 2000) {
        await click();
        lastClick = Date.now();
      }
      await sleep(200);
    }
    const match = route.exec(log);
    if (!match) throw new Error(`attachment-open-external: ${fileName} followed the wrong open path`);
    if (inApp) {
      for (let i = 0; i < 50 && !getWindowText(frame).includes("payload.html"); i++) await sleep(100);
      if (!getWindowText(frame).includes("payload.html"))
        throw new Error("attachment-open-external: new tab is missing");
      if (readdirSync(tempDir, { recursive: true }).length !== 0) {
        throw new Error("attachment-open-external: HTML attachment created an extracted file");
      }
    } else {
      const extracted = match[1];
      if (!existsSync(extracted)) throw new Error(`attachment-open-external: click did not extract ${extracted}`);
      if (!extracted.startsWith(`${tempDir}\\SwiftLeaf-`) || !extracted.endsWith("\\attachment-payload.bin")) {
        throw new Error(`attachment-open-external: unsafe extraction path ${extracted}`);
      }
      if (readFileSync(extracted, "utf8") !== kAttachData) throw new Error("attachment-open-external: bad payload");
      if (log.includes("LaunchEmbeddedDestination: opened '")) {
        throw new Error("attachment-open-external: unsupported attachment was passed to its file association");
      }
    }
    console.log(`attachment-open-external: ${fileName} OK`);
  } finally {
    // Explorer opens its window a moment later
    await sleep(1500);
    for (const w of newExplorerWindows(before)) {
      postMessage(w, WM_CLOSE, 0, 0);
    }
    client.close();
    await killAndWait(proc);
  }
}

export async function testit(): Promise<void> {
  const root = tmpPath("attachment-open-external");
  await testAttachment(root, "../payload.html", true);
  await testAttachment(root, "payload.bin", false);
}

if (import.meta.main) {
  await runStandalone(testit);
}
