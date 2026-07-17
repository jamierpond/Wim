import { spawn } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
export const BUILD_DIR = join(ROOT, 'build');

const PLATFORMS = { darwin: 'macos', win32: 'windows' };

// Resolve `Scripts/<name>-<platform>.mjs` for the current platform.
export function platformScript(name) {
  const platform = PLATFORMS[process.platform];
  if (!platform) throw new Error(`Unsupported platform: ${process.platform}`);
  return join(ROOT, 'Scripts', `${name}-${platform}.mjs`);
}

export function run(command, args = [], options = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: ROOT,
      stdio: 'inherit',
      shell: process.platform === 'win32',
      ...options,
    });
    child.on('exit', (code) =>
      code === 0
        ? resolve()
        : reject(new Error(`${command} ${args.join(' ')} exited with ${code}`)),
    );
    child.on('error', reject);
  });
}

export function runNode(script, args = []) {
  return run(process.execPath, [script, ...args]);
}

// Entry-point wrapper: run `fn`, exit non-zero on failure.
export async function main(fn) {
  try {
    await fn();
  } catch (error) {
    console.error(error.message);
    process.exit(1);
  }
}
