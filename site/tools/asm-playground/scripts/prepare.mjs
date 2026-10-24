import { build } from 'vite';
import { resolve } from 'node:path';

export async function prepare(destination = 'dist/engine') {
  await build({ configFile: false, logLevel: 'warn', build: { outDir: resolve(destination), emptyOutDir: false, minify: true,
    lib: { entry: resolve('src/engine/worker.ts'), name: 'AsmExecutorWorker', formats: ['iife'], fileName: () => 'executor-worker.js' },
  } });
}
if (process.argv[1] === new URL(import.meta.url).pathname) await prepare();
