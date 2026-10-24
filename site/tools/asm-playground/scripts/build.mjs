import { build } from 'vite';
import vue from '@vitejs/plugin-vue';
import { resolve } from 'node:path';
import { copyFile, mkdir, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { prepare } from './prepare.mjs';
await build({ configFile: false, plugins: [vue()], build: { outDir: resolve('dist'), emptyOutDir: true, sourcemap: true,
  lib: { entry: resolve('src/index.ts'), formats: ['es'], fileName: () => 'index.js', cssFileName: 'asm-playground' },
  rollupOptions: { external: ['vue'] },
} });
await prepare('dist/engine');
execFileSync(process.execPath, ['node_modules/vue-tsc/bin/vue-tsc.js', '-p', 'tsconfig.build.json'], { stdio: 'inherit' });
await copyFile('src/engine/engine.d.ts', 'dist/types/src/engine/engine.d.ts');
await writeFile('dist/index.d.ts', "export * from './types/src/index';\n");
await mkdir('dist/documents', { recursive: true });
for (const name of ['payload.schema.json', 'payload-format.md', 'typescript-style.md']) await copyFile(`documents/${name}`, `dist/documents/${name}`);
for (const name of ['LICENSE', 'README.md']) await copyFile(name, `dist/${name}`);
