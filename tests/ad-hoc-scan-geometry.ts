// Native OCR geometry, camera sampling and WIA callback regressions.
import { runAppUnitTests, runStandalone } from "./util.ts";
export async function testit(): Promise<void> {
  await runAppUnitTests();
}
if (import.meta.main) await runStandalone(testit);
