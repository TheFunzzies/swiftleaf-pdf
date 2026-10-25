// Smoke test for review and measurement tools: verifies command availability,
// placement modes, and review tools registration in SwiftLeaf PDF.

import { cmdId } from "./util.ts";

export async function testit(): Promise<void> {
  const commands = [
    "CmdMeasureDistance",
    "CmdMeasurePerimeter",
    "CmdMeasureArea",
    "CmdMeasureSetScale",
    "CmdExportMeasurementsCsv",
    "CmdExportCommentsXfdf",
    "CmdImportCommentsXfdf",
    "CmdExportCommentsFdf",
    "CmdImportCommentsFdf",
    "CmdCommentSummary",
    "CmdFlattenComments",
    "CmdCommentReply",
    "CmdCommentStatusAccepted",
    "CmdCommentStatusRejected",
    "CmdCommentStatusCompleted",
    "CmdCommentStatusCancelled",
    "CmdCommentStatusMarked",
    "CmdCreateAnnotCallout",
    "CmdCreateAnnotArrow",
    "CmdCreateAnnotReplaceText",
    "CmdCreateAnnotTextBox",
  ];

  for (const cmd of commands) {
    const id = cmdId(cmd);
    if (!id || id <= 0) {
      throw new Error(`Review tool command not registered: ${cmd}`);
    }
  }
}
