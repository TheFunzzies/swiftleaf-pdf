// The Comments list is docked under the frame, and opens on a scheduled relayout.
import { ControlClient, ControlCommand } from "./control.ts";
import { SLOW_BUILD_FACTOR } from "./util.ts";
import { isWindowVisible, sleep } from "./winapi.ts";
import { findChildByClass } from "./win-automation.ts";

export const COMMENTS_PANEL_CLASS = "SUMATRA_ANNOT_FILTER_WND";

export async function waitCommentsPanel(client: ControlClient, frame: number, visible: boolean): Promise<number> {
  const deadline = Date.now() + 5000 * SLOW_BUILD_FACTOR;
  let raw = "";
  for (;;) {
    const res = await client.request(ControlCommand.TestAnnotFilter, []);
    raw = String(res[1] ?? "");
    const panel = findChildByClass(frame, COMMENTS_PANEL_CLASS);
    // floatVisible is the historical diagnostic name; it now describes the child panel.
    if (
      res[0] === 0 &&
      new RegExp(`floatVisible=${visible ? 1 : 0}\\b`).test(raw) &&
      !!(panel && isWindowVisible(panel)) === visible
    )
      return panel;
    if (Date.now() > deadline) throw new Error(`Comments panel visible=${visible} never matched\n${raw}`);
    await sleep(40);
  }
}
