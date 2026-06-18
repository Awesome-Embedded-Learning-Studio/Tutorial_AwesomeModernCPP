#!/usr/bin/env tsx
/**
 * check_bold_rendering.ts — 检测 markdown 粗体/强调(**)渲染失败。
 *
 * 背景：VitePress 用 markdown-it 渲染。按 CommonMark 的 flanking 规则，当 `**` 的
 * 一侧紧贴 Unicode 标点（全角 `）。、！、：` 或半角 `)` 等）、另一侧紧贴普通字
 * （中文/英文/数字）时，`**` 不构成 emphasis delimiter，会以字面 `**` 残留在页面
 * （加粗失效）。最典型：`**术语（english）**` —— 结尾全角 `）` 致闭合 `**` 失效。
 *
 * 原理：用 VitePress 同源 markdown-it 逐行 renderInline，剥掉 inline code 后，若
 * 仍含字面 `**` → 该行有渲染失败。这是与线上完全一致的「金标准」检测。
 *
 * 用法：
 *   tsx scripts/check_bold_rendering.ts          # 只查中文 documents/（排除 en/）
 *   tsx scripts/check_bold_rendering.ts --all    # 同时查英文
 *
 * 退出码：0 通过，1 有失败，2 环境异常（pre-commit/CI 友好）。
 */
import { createRequire } from 'node:module'
import { dirname, join, relative } from 'node:path'
import { readFileSync, readdirSync } from 'node:fs'

const require = createRequire(import.meta.url)

// 复用 VitePress 自带的 markdown-it：版本随 vitepress 自动一致、渲染零偏差，
// 且无需在 package.json 声明额外依赖。
let MarkdownIt: any
try {
  const vpPkg = require.resolve('vitepress/package.json')
  MarkdownIt = require(require.resolve('markdown-it', { paths: [dirname(vpPkg)] }))
} catch {
  console.error('✗ 找不到 vitepress / markdown-it，请先 `pnpm install`。')
  process.exit(2)
}

// 复刻 VitePress 默认 markdown 配置（html:true, typographer:false）。
// emphasis 行为不受 linkify / 项目自定义插件（cppTemplateEscape / kbd / mermaid）影响。
const md = new MarkdownIt({ html: true, typographer: false })

const ROOT = join(process.cwd(), 'documents')
const INCLUDE_EN = process.argv.includes('--all')

interface Hit { rel: string; line: number; text: string }

function walk(dir: string, acc: string[] = []): string[] {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, e.name)
    if (e.isDirectory()) {
      if (e.name === 'en' && !INCLUDE_EN) continue
      walk(p, acc)
    } else if (e.name.endsWith('.md')) {
      acc.push(p)
    }
  }
  return acc
}

function scanFile(file: string): Hit[] {
  const lines = readFileSync(file, 'utf8').split('\n')
  const hits: Hit[] = []
  const rel = relative(ROOT, file)
  let inFence = false, fenceCh: string | null = null, yaml = false, yd = false
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    // 跳过 YAML frontmatter（文件首部 --- ... ---）
    if (!yd) {
      if (i === 0 && line.trim() === '---') { yaml = true; continue }
      if (yaml) { if (line.trim() === '---') { yaml = false; yd = true } continue }
      else yd = true
    }
    // 跳过代码围栏 ``` / ~~~
    const fm = line.match(/^\s*(`{3,}|~{3,})/)
    if (fm) {
      const c = fm[1][0]
      if (!inFence) { inFence = true; fenceCh = c }
      else if (c === fenceCh) { inFence = false; fenceCh = null }
      continue
    }
    if (inFence) continue
    if (/^\t/.test(line)) continue // 缩进代码块
    // 整行渲染 → 剥 inline code → 若残留字面 ** 即失败
    const rendered = md.renderInline(line).replace(/<code\b[^>]*>[\s\S]*?<\/code>/g, '')
    if (rendered.includes('**')) hits.push({ rel, line: i + 1, text: line.slice(0, 120) })
  }
  return hits
}

const hits = walk(ROOT).sort().flatMap(scanFile)

if (hits.length === 0) {
  console.log('✓ 粗体渲染检查通过：无 `**` 残留失败。')
  process.exit(0)
}

console.error(`✗ 发现 ${hits.length} 行粗体渲染失败（\`**\` 因标点边界未渲染为 <strong>，字面残留）：\n`)
for (const h of hits) console.error(`  ${h.rel}:L${h.line}  ${h.text}`)
console.error(`
修法：让 \`**\` 边界落在文字字符上，把标点移到 \`**\` 外侧。例如：
  **术语（english）**     → **术语**（english）     （英文对照移出加粗）
  **…句末标点！**         → **…**！                 （句末标点移出加粗）
  **"导入库"（import）**  → "**导入库**"（import）  （包裹引号移出加粗）
详见 scripts/check_bold_rendering.ts 顶部说明。`)
process.exit(1)
