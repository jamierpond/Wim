// Build, then launch Wim.app.
import { join } from 'node:path';
import { BUILD_DIR, ROOT, main, run, runNode } from './lib.mjs';

await main(async () => {
  await runNode(join(ROOT, 'Scripts', 'compile-macos.mjs'));
  await run('open', [join(BUILD_DIR, 'Wim.app')]);
});
