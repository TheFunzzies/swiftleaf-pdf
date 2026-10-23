import { expect, test } from "bun:test";
import { matchesKnown } from "./run-swiftleaf-ci.ts";

test("resolved FreeText failures must fail normally if they recur", () => {
  expect(
    matchesKnown(
      "free-text-draft",
      "Accepted text did not move by the drag delta\ntype=FreeText page=1 screen=477,284,60,7\nannotations=1\nbeforeScreen=477,277,60,14\n",
    ),
  ).toBe(false);
  expect(matchesKnown("free-text-transparent", 'Typing obscures page background: {"blue":115,"total":390}\n')).toBe(
    false,
  );
});

test("only the documented legacy ribbon failures are accepted", () => {
  for (const [name, message] of [
    ["annot-undo-redo", "annot-undo-redo: CmdUndo is not on the Edit PDF toolbar"],
    ["annot-color-dropdown", "annot-color-dropdown: no CmdCreateAnnotText button on the Edit PDF toolbar"],
    [
      "redact-annotations",
      "Apply Redactions is on the toolbar with nothing marked (the annotation toolbar never came up)",
    ],
  ]) {
    expect(matchesKnown(name!, message!)).toBe(true);
    expect(matchesKnown(name!, "app crashed")).toBe(false);
  }
});

test("the metadata exception requires the precise observed date row", () => {
  const baseline = "annotation metadata is wrong\nrow author=Ada\nrow date=2026-08-24 12:34\n";
  expect(matchesKnown("pdf-edit-toolbar-interaction", baseline)).toBe(true);
  expect(matchesKnown("pdf-edit-toolbar-interaction", baseline.replaceAll("\n", "\r\n"))).toBe(true);
  expect(matchesKnown("pdf-edit-toolbar-interaction", baseline.replace("12:34", "12:35"))).toBe(false);
  expect(matchesKnown("pdf-edit-toolbar-interaction", baseline.replace("author=Ada", "author=Bob"))).toBe(false);
});
