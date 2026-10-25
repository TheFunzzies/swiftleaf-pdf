import { expect, test } from "bun:test";
import { readFileSync } from "node:fs";

// BuildMenuFromDef walks until an empty title, including for nested menus.
test("menu definitions end with an empty sentinel", () => {
  const source = readFileSync(new URL("../src/Menu.cpp", import.meta.url), "utf8");
  const definitions = [...source.matchAll(/static MenuDef (\w+)\[\] = \{([\s\S]*?)\n\};/g)];
  expect(definitions.length).toBeGreaterThan(0);
  const unterminated = definitions
    .filter(([, , body]) => !/\{\s*\{\},\s*0,?\s*\},?\s*$/.test(body!))
    .map(([, name]) => name);
  expect(unterminated).toEqual([]);
});
