// Build, then launch Wim.exe (multi-config VS layout, single-config fallback).
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { BUILD_DIR, ROOT, main, run, runNode } from './lib.mjs';

await main(async () => {
  await runNode(join(ROOT, 'Scripts', 'compile-windows.mjs'));

  const candidates = [join(BUILD_DIR, 'Debug', 'Wim.exe'), join(BUILD_DIR, 'Wim.exe')];
  const exe = candidates.find(existsSync);
  if (!exe) throw new Error(`Wim.exe not found in ${BUILD_DIR}`);

  await run(exe, [], { detached: true });
});
