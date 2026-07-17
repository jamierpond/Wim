// Run the palette UI's vite dev server (pair with -DEACP_WEBVIEW_DEV=ON).
import { join } from 'node:path';
import { ROOT, main, run } from './lib.mjs';

await main(() => run('npm', ['run', 'dev'], { cwd: join(ROOT, 'web') }));
