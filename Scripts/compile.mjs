// Dispatch to Scripts/compile-<platform>.mjs for the current platform.
import { main, platformScript, runNode } from './lib.mjs';

await main(() => runNode(platformScript('compile'), process.argv.slice(2)));
