// Delete the build directory.
import { rm } from 'node:fs/promises';
import { BUILD_DIR, main } from './lib.mjs';

await main(() => rm(BUILD_DIR, { recursive: true, force: true }));
