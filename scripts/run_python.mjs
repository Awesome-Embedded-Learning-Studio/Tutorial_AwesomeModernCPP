import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const candidates = process.platform === 'win32'
  ? [resolve(repoRoot, '.venv', 'Scripts', 'python.exe')]
  : [resolve(repoRoot, '.venv', 'bin', 'python')];
const python = candidates.find((candidate) => existsSync(candidate));

if (!python) {
  const expected = process.platform === 'win32'
    ? '.venv\\Scripts\\python.exe'
    : '.venv/bin/python';
  console.error(`Python virtual environment not found. Create ${expected} first.`);
  process.exit(1);
}

const result = spawnSync(python, process.argv.slice(2), {
  cwd: repoRoot,
  env: {
    ...process.env,
    PYTHONIOENCODING: 'utf-8',
    PYTHONUTF8: '1',
  },
  stdio: 'inherit',
});

if (result.error) {
  console.error(`Failed to start ${python}: ${result.error.message}`);
  process.exit(1);
}

process.exit(result.status ?? 1);
