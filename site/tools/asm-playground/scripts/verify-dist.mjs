import assert from 'node:assert/strict';
import { readFile, readdir, access, mkdir, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { createSSRApp, h } from 'vue';
import { renderToString } from '@vue/server-renderer';
const entry = await readFile('dist/index.js', 'utf8');
const worker = await readFile('dist/engine/executor-worker.js', 'utf8');
assert(worker.includes('无法解码的指令') && worker.includes('除数为零'), 'the local interpreter must be bundled into the worker');
// 检查实际可执行产物与 source map，而非只检查组件入口。
async function checkCode(directory) {
  for (const item of await readdir(directory, { withFileTypes: true })) {
    const path = `${directory}/${item.name}`;
    if (item.isDirectory()) await checkCode(path);
    else if (/\.(?:js|map)$/.test(item.name)) {
      const code = await readFile(path, 'utf8');
      assert(!/unicorn|MUnicorn|jsdelivr|importScripts|GPL|QEMU/i.test(code), `unexpected remote or GPL engine code: ${path}`);
      assert(!/node_modules\/(?:ajv|fast-deep-equal|fast-uri|json-schema-traverse|require-from-string)\//.test(code), `unexpected validator dependency: ${path}`);
    }
  }
}
await checkCode('dist');
assert(/from ["']vue["']/.test(entry), 'Vue must be external');
await access('dist/index.d.ts'); await access('dist/types/src/engine/engine.d.ts'); await access('dist/asm-playground.css');
await mkdir('dist/examples', { recursive: true });
await writeFile('dist/examples/consumer.ts', `import { createSSRApp } from 'vue';
import { AsmExecutor, AsmExecutorPlugin, validatePayload, type PayloadV1, type Snapshot } from '../index.js';
const app = createSSRApp(AsmExecutor, { src: '/book/payloads/sample.json' });
app.use(AsmExecutorPlugin, { workerUrl: '/book/asm-engine/executor-worker.js' });
const validate: (input: unknown) => PayloadV1 = validatePayload;
const state: Snapshot['status'] = 'ready';
void [validate, state];
`);
execFileSync(process.execPath, ['node_modules/typescript/bin/tsc', '--noEmit', '--strict', '--target', 'ES2022', '--module', 'ESNext', '--moduleResolution', 'Bundler', '--skipLibCheck', 'false', 'dist/examples/consumer.ts'], { stdio: 'inherit' });
const { AsmExecutor, AsmExecutorPlugin } = await import('../dist/index.js');
const app = createSSRApp({ render: () => h(AsmExecutor, { src: '/book/payloads/sample.json' }) });
app.use(AsmExecutorPlugin, { workerUrl: '/book/asm-engine/executor-worker.js' });
assert((await renderToString(app)).includes('滚动到此处后加载'));
await mkdir('test-results', { recursive: true });
const sizes = {};
for (const name of await readdir('dist')) if (name.endsWith('.js') || name.endsWith('.css')) sizes[name] = (await readFile(`dist/${name}`)).length;
await writeFile('test-results/dist-report.json', JSON.stringify({ measuredAt: new Date().toISOString(), status: 'pass', checks: ['local-interpreter-in-worker', 'local-payload-validator', 'no-remote-or-gpl-engine', 'external-vue', 'production-ssr', 'vendored-typescript-import', 'declarations'], components: sizes, workerBytes: Buffer.byteLength(worker) }, null, 2) + '\n');
console.log('Distribution verified: local interpreter and payload validator, no remote or GPL engine, external Vue, production SSR, declarations.');
console.log(JSON.stringify({ components: sizes }, null, 2));
