// asm-playground 装配：构建 site/tools/asm-playground 的组件与 worker，
// 拷入 theme/vendor 与 documents/public，并把 code/volumn_codes/vol10/asm-payloads
// 下各场景生成 payload。所有产物均不入库（见 .gitignore），重复执行安全。
import { execFileSync } from 'child_process'
import { cpSync, existsSync, mkdirSync, readdirSync, rmSync } from 'fs'
import { join, resolve } from 'path'

const ROOT = resolve(import.meta.dirname, '..')
const TOOL = join(ROOT, 'site/tools/asm-playground')
const VENDOR = join(ROOT, 'site/.vitepress/theme/vendor/asm-playground')
// 静态产物走 documents/public：VitePress（dev/单体/分卷的 root 构建）只认 <srcDir>/public，
// 直接落到别处线上会 404（build.ts 对 favicon 404 的教训同源）。
const ENGINE_DIR = join(ROOT, 'documents/public/asm-engine')
const PAYLOADS_DIR = join(ROOT, 'documents/public/payloads')
const SCENES_DIR = join(ROOT, 'code/volumn_codes/vol10/asm-payloads')

function run(command: string, args: string[], cwd: string) {
  execFileSync(command, args, { cwd, stdio: 'inherit' })
}

// 1) 组件与 worker：node_modules 缺失时先 ci，随后构建并跑产物自检
if (!existsSync(join(TOOL, 'node_modules'))) run('npm', ['ci', '--ignore-scripts'], TOOL)
run('npm', ['run', 'build'], TOOL)
run('npm', ['run', 'verify:dist'], TOOL)

// 2) vendor 装配：清旧拷新，保留动态导入的 chunk、CSS 与 LICENSE
rmSync(VENDOR, { recursive: true, force: true })
mkdirSync(VENDOR, { recursive: true })
cpSync(join(TOOL, 'dist'), VENDOR, { recursive: true })

// 3) worker 布置到站点同源路径 /asm-engine/
mkdirSync(ENGINE_DIR, { recursive: true })
cpSync(join(TOOL, 'dist/engine/executor-worker.js'), join(ENGINE_DIR, 'executor-worker.js'))

// 4) 负载：每个带 manifest.json 的场景目录经统一构建器生成 payload.json
const venvPython = join(ROOT, '.venv/bin/python')
const python = existsSync(venvPython) ? venvPython : 'python3'
mkdirSync(PAYLOADS_DIR, { recursive: true })
for (const entry of readdirSync(SCENES_DIR, { withFileTypes: true })) {
  if (!entry.isDirectory() || entry.name.startsWith('_')) continue
  const scene = join(SCENES_DIR, entry.name)
  if (!existsSync(join(scene, 'manifest.json'))) continue
  run(python, [join(SCENES_DIR, 'build_scene.py'), entry.name], ROOT)
  cpSync(join(scene, 'payload.json'), join(PAYLOADS_DIR, `${entry.name}.json`))
}

console.log('[asm-playground] vendor / asm-engine / payloads assembled.')
