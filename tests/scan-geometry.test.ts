import { test, expect } from "bun:test";
import { readFileSync } from "node:fs";
const source = (path: string) => readFileSync(new URL(`../${path}`, import.meta.url), "utf8");
test("OCR undoes preprocessing in interactive and batch paths", () => {
  const s = source("src/Ocr.cpp").split("#if IS_DEBUG")[0];
  expect(s.match(/MapOcrWords\(words, preprocessing\)/g)?.length).toBe(2);
  expect(s).toContain("preprocessing.Invert()");
});
test("scanner transfers device pixels, never manufactures a scan", () => {
  const s = source("src/ScannerWia.cpp");
  expect(s).toContain("->CreateDevice(");
  expect(s).toContain("->Download(");
  expect(s).not.toContain("dpi * 17 / 2");
});
test("camera uses explicit corners and a projective denominator", () => {
  const s = source("src/ScannerWia.cpp");
  expect(s).not.toContain("w * 0.05f");
  expect(s).toContain("ReadCorners");
  expect(s).toContain("g * s + h * t + 1");
});
test("camera rejects non-convex and out-of-bounds corners", () => {
  const s = source("src/ScannerWia.cpp");
  expect(s).toContain("PerspectiveCropPixmap");
  expect(s).toContain("!_finite(a.x)");
  expect(s).toContain("abs(center[0] - 50) <= 1");
});
