// Dispatch to Scripts/run-<platform>.mjs for the current platform.
import { main, platformScript, runNode } from './lib.mjs';

await main(() => runNode(platformScript('run'), process.argv.slice(2)));
