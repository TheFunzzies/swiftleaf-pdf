import { expect, test } from "bun:test";
import { matchesKnown } from "./run-swiftleaf-ci.ts";

function background(blue: unknown, total: unknown): string {
  return `Typing obscures page background: ${JSON.stringify({ blue, total })}\n`;
}

const baselineRect = "477,284,60,7";
const beforeRect = "477,277,60,14";
function drag(rect: string, before = beforeRect): string {
  return `Accepted text did not move by the drag delta\ntype=FreeText page=1 screen=${rect}\nannotations=1\nbeforeScreen=${before}\n`;
}

test("accepts the observed background sample", () => {
  expect(matchesKnown("free-text-transparent", background(115, 390))).toBe(true);
});

test("rejects opaque and materially worse backgrounds", () => {
  expect(matchesKnown("free-text-transparent", background(0, 390))).toBe(false);
  expect(matchesKnown("free-text-transparent", background(90, 390))).toBe(false);
});

test("rejects invalid or non-failing background samples", () => {
  for (const sample of [
    background(0, 0),
    background("115", 390),
    background(117, 390),
    "Typing obscures page background: not JSON",
  ]) {
    expect(matchesKnown("free-text-transparent", sample)).toBe(false);
  }
});

test("accepts the observed FreeText rectangle transition", () => {
  expect(matchesKnown("free-text-draft", drag(baselineRect))).toBe(true);
  expect(matchesKnown("free-text-draft", drag(baselineRect).replaceAll("\n", "\r\n"))).toBe(true);
});

test("rejects disappearing annotations and missing before rectangles", () => {
  expect(matchesKnown("free-text-draft", "Accepted text did not move by the drag delta\nannotations=0\n")).toBe(false);
  expect(matchesKnown("free-text-draft", drag(baselineRect).replace(/^beforeScreen=.*\n/m, ""))).toBe(false);
});

test("rejects changed positions, sizes and annotation counts", () => {
  for (const rect of ["478,284,60,7", "477,285,60,7", "477,284,61,7", "477,284,60,0"]) {
    expect(matchesKnown("free-text-draft", drag(rect))).toBe(false);
  }
  expect(matchesKnown("free-text-draft", drag(baselineRect).replace("annotations=1", "annotations=0"))).toBe(false);
  expect(matchesKnown("free-text-draft", drag(baselineRect, baselineRect))).toBe(false);
});
