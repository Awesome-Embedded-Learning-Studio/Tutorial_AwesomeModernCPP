import { exec } from 'child_process'
import {
  cpSync, mkdirSync, rmSync, writeFileSync,
  readdirSync, readFileSync, existsSync,
  symlinkSync, statSync,
} from 'fs'
import { join, resolve, relative, basename } from 'path'
import { createHash } from 'crypto'
import { createRequire } from 'module'
const require = createRequire(import.meta.url)

// ── CLI Flags ───────────────────────────────────────────────

const FORCE_REBUILD = process.argv.includes('--force') || process.argv.includes('--clean')
const CONCURRENCY = parseInt(process.env.BUILD_CONCURRENCY || '4', 10)

// ── Configuration ───────────────────────────────────────────

interface Volume {
  name: string
  srcDir: string
  urlPrefix: string
}

const VOLUMES: Volume[] = [
  { name: 'vol1', srcDir: 'vol1-fundamentals', urlPrefix: '/vol1-fundamentals' },
  { name: 'vol2', srcDir: 'vol2-modern-features', urlPrefix: '/vol2-modern-features' },
  { name: 'vol3', srcDir: 'vol3-standard-library', urlPrefix: '/vol3-standard-library' },
  { name: 'vol4', srcDir: 'vol4-advanced', urlPrefix: '/vol4-advanced' },
  { name: 'vol5', srcDir: 'vol5-concurrency', urlPrefix: '/vol5-concurrency' },
  { name: 'vol6', srcDir: 'vol6-performance', urlPrefix: '/vol6-performance' },
  { name: 'vol7', srcDir: 'vol7-engineering', urlPrefix: '/vol7-engineering' },
  { name: 'vol8', srcDir: 'vol8-domains', urlPrefix: '/vol8-domains' },
  { name: 'vol9', srcDir: 'vol9-open-source-project-learn', urlPrefix: '/vol9-open-source-project-learn' },
  { name: 'compilation', srcDir: 'compilation', urlPrefix: '/compilation' },
  { name: 'cpp-reference', srcDir: 'cpp-reference', urlPrefix: '/cpp-reference' },
  { name: 'projects', srcDir: 'projects', urlPrefix: '/projects' },
  { name: 'appendix', srcDir: 'appendix', urlPrefix: '/appendix' },
  { name: 'team', srcDir: 'team', urlPrefix: '/team' },
]

const PROJECT_ROOT = resolve(import.meta.dirname, '..')
const SITE_DIR = join(PROJECT_ROOT, 'site')
const MAIN_VP = join(SITE_DIR, '.vitepress')
const BUILD_TMP = join(MAIN_VP, '.build-tmp')
const CACHE_DIR = join(MAIN_VP, '.build-cache')
const MANIFEST_PATH = join(CACHE_DIR, 'manifest.json')
const DIST_FINAL = join(MAIN_VP, 'dist')
const DOCUMENTS = join(PROJECT_ROOT, 'documents')

// ── Logging ─────────────────────────────────────────────────

function ts(): string {
  return new Date().toISOString().substring(11, 19)
}

function log(msg: string) { console.log(`[${ts()}] ${msg}`) }
function logStep(msg: string) {
  console.log(`\n[${ts()}] ${'═'.repeat(60)}`)
  log(`  ${msg}`)
  console.log(`[${ts()}] ${'═'.repeat(60)}`)
}

function memMB(): string {
  const m = process.memoryUsage()
  return `RSS=${(m.rss / 1024 / 1024).toFixed(0)}MB Heap=${(m.heapUsed / 1024 / 1024).toFixed(0)}/${(m.heapTotal / 1024 / 1024).toFixed(0)}MB`
}

// ── Helpers ─────────────────────────────────────────────────

function ensureClean(dir: string) {
  if (existsSync(dir)) rmSync(dir, { recursive: true })
  mkdirSync(dir, { recursive: true })
}

function symlinkDir(target: string, link: string) {
  if (existsSync(link)) rmSync(link, { recursive: true })
  symlinkSync(target, link, 'dir')
}

function countMdFiles(dir: string): number {
  let count = 0
  try {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      if (e.name.startsWith('.')) continue
      const full = join(dir, e.name)
      if (e.isDirectory()) count += countMdFiles(full)
      else if (e.name.endsWith('.md')) count++
    }
  } catch { /* ignore */ }
  return count
}

/** Compute a fast hash of a directory's file mtimes + sizes for change detection */
function hashDir(dir: string): string {
  const h = createHash('sha256')
  function walk(d: string) {
    try {
      const entries = readdirSync(d, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))
      for (const e of entries) {
        if (e.name.startsWith('.')) continue
        const full = join(d, e.name)
        if (e.isDirectory()) { walk(full); continue }
        const s = statSync(full)
        h.update(`${relative(dir, full)}:${s.size}:${s.mtimeMs}\n`)
      }
    } catch { /* ignore */ }
  }
  walk(dir)
  return h.digest('hex').substring(0, 16)
}

// ── Manifest (incremental build state) ──────────────────────

interface ManifestEntry { hash: string; timestamp: string }
type Manifest = Record<string, ManifestEntry>

function readManifest(): Manifest {
  if (FORCE_REBUILD) {
    log('  --force: discarding build cache')
    if (existsSync(CACHE_DIR)) rmSync(CACHE_DIR, { recursive: true })
    return {}
  }
  if (!existsSync(MANIFEST_PATH)) return {}
  try { return JSON.parse(readFileSync(MANIFEST_PATH, 'utf-8')) } catch { return {} }
}

function writeManifest(manifest: Manifest) {
  mkdirSync(CACHE_DIR, { recursive: true })
  writeFileSync(MANIFEST_PATH, JSON.stringify(manifest, null, 2))
}

// ── Config Generators ───────────────────────────────────────

function generateVolumeConfig(vol: Volume, lang: 'zh' | 'en', absSiteDir: string, absSrcDir: string): string {
  const relSrc = relative(absSiteDir, absSrcDir)
  const outDirName = lang === 'en' ? `${vol.name}-en` : vol.name
  const relOut = relative(absSiteDir, join(BUILD_TMP, 'output', outDirName))
  const prefix = lang === 'en' ? `/en${vol.urlPrefix}` : vol.urlPrefix
  const locale = lang === 'en'
    ? `locales: { root: { label: 'English', lang: 'en-US', title: 'Modern C++ Tutorial', description: 'A systematic modern C++ tutorial' } },`
    : `locales: { root: { label: '中文', lang: 'zh-CN', title: '现代 C++ 教程', description: '系统化的现代 C++ 教程' } },`
  const vpDir = join(absSiteDir, '.vitepress')
  const relShared = relative(vpDir, join(MAIN_VP, 'config', 'shared')).replace(/\\/g, '/')
  const relSidebar = relative(vpDir, join(MAIN_VP, 'config', 'sidebar')).replace(/\\/g, '/')

  return `import { defineConfig } from 'vitepress'
import { sharedBase, ${lang === 'en' ? 'sharedEnThemeConfig' : 'sharedThemeConfig'} } from '${relShared}'
import { volumeSidebar } from '${relSidebar}'

export default defineConfig({
  ...sharedBase,
  srcDir: '${relSrc.replace(/\\/g, '/')}',
  outDir: '${relOut.replace(/\\/g, '/')}',
  ignoreDeadLinks: true,
  title: '${lang === 'en' ? 'Modern C++ Tutorial' : '现代 C++ 教程'}',
  lang: '${lang === 'en' ? 'en-US' : 'zh-CN'}',
  ${locale}
  themeConfig: {
    ...${lang === 'en' ? 'sharedEnThemeConfig' : 'sharedThemeConfig'}(),
    sidebar: { '${prefix}': volumeSidebar('${vol.srcDir}', '${prefix}') },
  },
})
`
}

function generateRootConfig(absSiteDir: string, absSrcDir: string): string {
  const relSrc = relative(absSiteDir, absSrcDir)
  const relOut = relative(absSiteDir, join(BUILD_TMP, 'output', 'root'))
  const vpDir = join(absSiteDir, '.vitepress')
  const relShared = relative(vpDir, join(MAIN_VP, 'config', 'shared')).replace(/\\/g, '/')
  const relNav = relative(vpDir, join(MAIN_VP, 'config', 'nav')).replace(/\\/g, '/')
  const relSidebar = relative(vpDir, join(MAIN_VP, 'config', 'sidebar')).replace(/\\/g, '/')

  return `import { defineConfig } from 'vitepress'
import { sharedBase, sharedThemeConfig, sharedEnThemeConfig } from '${relShared}'
import { navZh, navEn } from '${relNav}'
import { buildSidebar } from '${relSidebar}'

export default defineConfig({
  ...sharedBase,
  srcDir: '${relSrc.replace(/\\/g, '/')}',
  outDir: '${relOut.replace(/\\/g, '/')}',
  ignoreDeadLinks: true,
  title: '现代 C++ 教程',
  description: '系统化的现代 C++ 教程 — 从基础入门到领域实战',
  lang: 'zh-CN',
  locales: {
    root: { label: '中文', lang: 'zh-CN', title: '现代 C++ 教程', description: '系统化的现代 C++ 教程 — 从基础入门到领域实战' },
    en: { label: 'English', lang: 'en-US', title: 'Modern C++ Tutorial', description: 'A systematic modern C++ tutorial', link: '/en/',
      themeConfig: { nav: navEn, editLink: { pattern: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/edit/main/documents/en/:path', text: 'Edit this page on GitHub' } } },
  },
  themeConfig: {
    nav: navZh, sidebar: buildSidebar(), search: { provider: 'local' },
    editLink: { pattern: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/edit/main/documents/:path', text: '在 GitHub 上编辑此页' },
    footer: { message: '基于 VitePress 构建', copyright: 'Copyright 2025-2026 Charliechen' },
    socialLinks: [{ icon: 'github', link: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP' }],
  },
})
`
}

// ── Build Tasks ─────────────────────────────────────────────

interface BuildTask {
  id: string                // e.g. "vol1-zh", "vol1-en"
  vol: Volume
  lang: 'zh' | 'en'
  cacheKey: string          // hash of source dir
  cached: boolean           // can skip build?
}

function prepareVolume(vol: Volume, lang: 'zh' | 'en', manifest: Manifest): BuildTask {
  const volDocDir = lang === 'en' ? join(DOCUMENTS, 'en', vol.srcDir) : join(DOCUMENTS, vol.srcDir)
  const id = lang === 'en' ? `${vol.name}-en` : vol.name
  const cacheKey = existsSync(volDocDir) ? hashDir(volDocDir) : ''
  const prev = manifest[id]
  const cached = !FORCE_REBUILD && prev && prev.hash === cacheKey && existsSync(join(CACHE_DIR, 'output', id))
  return { id, vol, lang, cacheKey, cached }
}

function execAsync(cmd: string, opts?: { cwd?: string }): Promise<void> {
  return new Promise((resolve, reject) => {
    exec(cmd, { cwd: opts?.cwd ?? PROJECT_ROOT }, (err, stdout, stderr) => {
      if (stdout) process.stdout.write(stdout)
      if (stderr) process.stderr.write(stderr)
      if (err) reject(err)
      else resolve()
    })
  })
}

async function buildVolume(task: BuildTask): Promise<string> {
  const { id, vol, lang } = task
  const volDocDir = lang === 'en' ? join(DOCUMENTS, 'en', vol.srcDir) : join(DOCUMENTS, vol.srcDir)
  const srcDirName = id
  const volSrcDir = join(BUILD_TMP, `src-${srcDirName}`)
  const tmpSite = join(BUILD_TMP, `site-${srcDirName}`)
  const volOutput = join(BUILD_TMP, 'output', srcDirName)
  const cachedOutput = join(CACHE_DIR, 'output', srcDirName)

  // If cached, copy from cache and skip build
  if (task.cached) {
    log(`  ${id}: ✓ cached (unchanged)`)
    mkdirSync(volOutput, { recursive: true })
    cpSync(cachedOutput, volOutput, { recursive: true })
    return volOutput
  }

  const mdCount = countMdFiles(volDocDir)
  log(`  ${id}: building ${mdCount} files...`)

  // Prepare source
  if (lang === 'en') {
    mkdirSync(join(volSrcDir, 'en'), { recursive: true })
    cpSync(volDocDir, join(volSrcDir, 'en', vol.srcDir), { recursive: true })
  } else {
    mkdirSync(volSrcDir, { recursive: true })
    cpSync(volDocDir, join(volSrcDir, vol.srcDir), { recursive: true })
  }

  // Prepare site
  mkdirSync(join(tmpSite, '.vitepress'), { recursive: true })
  writeFileSync(join(tmpSite, '.vitepress', 'config.ts'), generateVolumeConfig(vol, lang, tmpSite, volSrcDir))
  symlinkDir(join(MAIN_VP, 'theme'), join(tmpSite, '.vitepress', 'theme'))
  symlinkDir(join(MAIN_VP, 'plugins'), join(tmpSite, '.vitepress', 'plugins'))
  symlinkDir(join(MAIN_VP, 'public'), join(tmpSite, '.vitepress', 'public'))

  const t0 = Date.now()
  await execAsync(`npx vitepress build ${relative(PROJECT_ROOT, tmpSite)}`)
  const elapsed = ((Date.now() - t0) / 1000).toFixed(1)

  if (!existsSync(volOutput)) throw new Error(`${id}: output dir not found after build`)
  log(`  ${id}: ✓ built in ${elapsed}s (${mdCount} files, ${memMB()})`)

  // Save to cache
  mkdirSync(join(CACHE_DIR, 'output'), { recursive: true })
  if (existsSync(cachedOutput)) rmSync(cachedOutput, { recursive: true })
  cpSync(volOutput, cachedOutput, { recursive: true })

  return volOutput
}

/** Run tasks with limited concurrency */
async function runParallel<T>(tasks: T[], fn: (t: T) => Promise<void>, limit: number): Promise<void> {
  let idx = 0
  const workers: Promise<void>[] = []
  for (let i = 0; i < Math.min(limit, tasks.length); i++) {
    workers.push((async () => {
      while (idx < tasks.length) {
        const task = tasks[idx++]
        if (task) await fn(task)
      }
    })())
  }
  await Promise.all(workers)
}

// ── Cross-Volume Data Unification ────────────────────────────

function unifyCrossVolumeData(distDir: string) {
  logStep('Step 3.5/4: Unifying cross-volume hash maps & site data')

  const htmlFiles: string[] = []
  function walk(d: string) {
    for (const e of readdirSync(d, { withFileTypes: true })) {
      const full = join(d, e.name)
      if (e.isDirectory()) walk(full)
      else if (e.name.endsWith('.html')) htmlFiles.push(full)
    }
  }
  walk(distDir)
  log(`  Found ${htmlFiles.length} HTML files`)

  // 1. Collect all hash map entries and find the root site data
  const mergedHashMap: Record<string, string> = {}
  let rootSiteDataExpr = ''

  for (const f of htmlFiles) {
    const c = readFileSync(f, 'utf-8')

    // Extract hash map — captured content is JS string literal (has \" escaping)
    const hmMatch = c.match(/__VP_HASH_MAP__\s*=\s*JSON\.parse\("(.+?)"\)/)
    if (hmMatch) {
      try {
        const mapObj: Record<string, string> = JSON.parse(new Function(`return "${hmMatch[1]}"`)())
        Object.assign(mergedHashMap, mapObj)
      } catch { /* skip */ }
    }

    // Extract site data expression — use root's (has full sidebar/nav)
    if (f === join(distDir, 'index.html')) {
      const sdMatch = c.match(/__VP_SITE_DATA__\s*=\s*JSON\.parse\("(.+?)"\)/)
      if (sdMatch) rootSiteDataExpr = sdMatch[1]
    }
  }

  const totalEntries = Object.keys(mergedHashMap).length
  log(`  Merged hash map: ${totalEntries} entries`)
  log(`  Root site data: ${rootSiteDataExpr ? 'found' : 'MISSING'}`)

  // 2. Build replacement expressions using JSON.stringify for proper JS string literal escaping
  const hmJsLiteral = JSON.stringify(JSON.stringify(mergedHashMap))

  let patched = 0
  for (const f of htmlFiles) {
    let c = readFileSync(f, 'utf-8')
    let changed = false

    // Replace hash map
    const hmReplace = c.replace(
      /__VP_HASH_MAP__\s*=\s*JSON\.parse\(".+?"\)/,
      `__VP_HASH_MAP__=JSON.parse(${hmJsLiteral})`
    )
    if (hmReplace !== c) { c = hmReplace; changed = true }

    // Replace site data with root's (full nav/sidebar) — skip root itself
    if (rootSiteDataExpr && f !== join(distDir, 'index.html')) {
      const sdReplace = c.replace(
        /__VP_SITE_DATA__\s*=\s*JSON\.parse\(".+?"\)/,
        `__VP_SITE_DATA__=JSON.parse("${rootSiteDataExpr}")`
      )
      if (sdReplace !== c) { c = sdReplace; changed = true }
    }

    if (changed) {
      writeFileSync(f, c)
      patched++
    }
  }
  log(`  Patched ${patched} files with unified data`)
}

// ── Search Index Merge ──────────────────────────────────────

function findSearchIndexFiles(dir: string): Map<string, string> {
  const result = new Map<string, string>()
  const chunksDir = join(dir, 'assets', 'chunks')
  if (!existsSync(chunksDir)) return result
  for (const f of readdirSync(chunksDir)) {
    const m = f.match(/^@localSearchIndex(root|en)\.[^.]+\.js$/)
    if (m) result.set(m[1], join(chunksDir, f))
  }
  return result
}

function extractSearchDocs(indexPath: string): Array<Record<string, unknown>> {
  const content = readFileSync(indexPath, 'utf-8')
  const m = content.match(/^const \w+=(.+);export\{/)
  if (!m) { log(`  ⚠ Could not parse: ${relative(PROJECT_ROOT, indexPath)}`); return [] }
  const jsonStr: string = new Function(`return ${m[1]}`)()
  const data = JSON.parse(jsonStr)
  const docs: Array<Record<string, unknown>> = []
  for (const [idStr, url] of Object.entries<string>(data.documentIds)) {
    const fields = data.storedFields[idStr]
    if (!fields) continue
    docs.push({ id: url, title: fields.title || '', titles: fields.titles || [] })
  }
  return docs
}

async function buildSearchIndexJs(docs: Array<Record<string, unknown>>): Promise<string> {
  const MiniSearch = require('minisearch')
  const ms = new MiniSearch({ fields: ['title', 'titles', 'text'], storeFields: ['title', 'titles'] })
  ms.addAll(docs)
  const json = JSON.stringify(ms.toJSON())
  // Double-stringify to get a properly escaped JS string literal (handles backticks, quotes, etc.)
  return `const e=${JSON.stringify(json)};export{e as default};`
}

function findAllSearchIndexFiles(dir: string): Map<string, string[]> {
  const result = new Map<string, string[]>()
  const chunksDir = join(dir, 'assets', 'chunks')
  if (!existsSync(chunksDir)) return result
  for (const f of readdirSync(chunksDir)) {
    const m = f.match(/^@localSearchIndex(root|en)\.[^.]+\.js$/)
    if (m) {
      const list = result.get(m[1]) || []
      list.push(join(chunksDir, f))
      result.set(m[1], list)
    }
  }
  return result
}

async function mergeSearchIndexes(outputDirs: string[], finalDist: string) {
  logStep('Step 3/4: Merging search indexes')
  for (const locale of ['root', 'en']) {
    const allDocs: Array<Record<string, string>> = []
    for (const dir of outputDirs) {
      const f = findSearchIndexFiles(dir).get(locale)
      if (!f) continue
      const docs = extractSearchDocs(f)
      log(`  ${locale}: ${docs.length} docs from ${relative(PROJECT_ROOT, dir)}`)
      allDocs.push(...docs)
    }
    if (allDocs.length === 0) { log(`  ${locale}: no docs, skipping`); continue }
    log(`  ${locale}: merging ${allDocs.length} total docs...`)
    const js = await buildSearchIndexJs(allDocs)
    // Replace ALL search index files for this locale (one per volume page set)
    const allTargets = findAllSearchIndexFiles(finalDist).get(locale) || []
    if (allTargets.length === 0) {
      log(`  ⚠ ${locale}: no target index files in final dist!`)
      continue
    }
    // Write canonical index to the first file, re-export stubs for the rest
    writeFileSync(allTargets[0], js)
    const canonicalName = basename(allTargets[0])
    const stub = `export{default}from"./${canonicalName}";`
    for (let i = 1; i < allTargets.length; i++) {
      writeFileSync(allTargets[i], stub)
    }
    const savedMB = ((js.length - stub.length) * (allTargets.length - 1) / 1024 / 1024).toFixed(1)
    log(`  ${locale}: ✓ 1 canonical + ${allTargets.length - 1} stubs (saved ${savedMB} MB)`)
  }
}

// ── Main ────────────────────────────────────────────────────

async function main() {
  logStep('Split Build — VitePress per-volume build')
  log(`  Project:     ${PROJECT_ROOT}`)
  log(`  Concurrency: ${CONCURRENCY}`)
  log(`  Force:       ${FORCE_REBUILD}`)
  log(`  Memory:      ${memMB()}`)
  const start = Date.now()

  // ── Prepare ─────────────────────────────────────────────
  ensureClean(BUILD_TMP)
  ensureClean(DIST_FINAL)
  mkdirSync(join(BUILD_TMP, 'output'), { recursive: true })

  const manifest = readManifest()

  // ── Step 1: Build root ──────────────────────────────────
  logStep('Step 1/4: Building root site (index, tags)')

  const rootSrcDir = join(BUILD_TMP, 'root-src')
  mkdirSync(rootSrcDir, { recursive: true })
  for (const f of ['index.md', 'tags.md']) {
    const s = join(DOCUMENTS, f)
    if (existsSync(s)) cpSync(s, join(rootSrcDir, f))
  }
  for (const asset of ['images', 'stylesheets', 'javascripts', 'robots.txt', 'Awesome-Embedded.png', 'Awesome-Embedded.ico']) {
    const s = join(DOCUMENTS, asset)
    if (existsSync(s)) cpSync(s, join(rootSrcDir, asset), statSync(s).isDirectory() ? { recursive: true } : undefined)
  }
  if (existsSync(join(DOCUMENTS, 'en'))) {
    mkdirSync(join(rootSrcDir, 'en'), { recursive: true })
    for (const f of ['index.md', 'tags.md']) {
      const s = join(DOCUMENTS, 'en', f)
      if (existsSync(s)) cpSync(s, join(rootSrcDir, 'en', f))
    }
  }

  const rootTmpSite = join(BUILD_TMP, 'site-root')
  mkdirSync(join(rootTmpSite, '.vitepress'), { recursive: true })
  writeFileSync(join(rootTmpSite, '.vitepress', 'config.ts'), generateRootConfig(rootTmpSite, rootSrcDir))
  symlinkDir(join(MAIN_VP, 'theme'), join(rootTmpSite, '.vitepress', 'theme'))
  symlinkDir(join(MAIN_VP, 'plugins'), join(rootTmpSite, '.vitepress', 'plugins'))
  symlinkDir(join(MAIN_VP, 'public'), join(rootTmpSite, '.vitepress', 'public'))

  const rootT0 = Date.now()
  await execAsync('npx vitepress build .', { cwd: rootTmpSite })
  const rootOutput = join(BUILD_TMP, 'output', 'root')
  if (existsSync(rootOutput)) cpSync(rootOutput, DIST_FINAL, { recursive: true })
  log(`  Root: ${((Date.now() - rootT0) / 1000).toFixed(1)}s`)

  // ── Step 2: Build volumes in parallel ────────────────────
  logStep('Step 2/4: Building volumes (parallel)')

  // Collect build tasks
  const tasks: BuildTask[] = []
  for (const vol of VOLUMES) {
    for (const lang of ['zh', 'en'] as const) {
      const volDocDir = lang === 'en' ? join(DOCUMENTS, 'en', vol.srcDir) : join(DOCUMENTS, vol.srcDir)
      if (!existsSync(volDocDir)) continue
      tasks.push(prepareVolume(vol, lang, manifest))
    }
  }

  const cachedCount = tasks.filter(t => t.cached).length
  const buildCount = tasks.length - cachedCount
  log(`  Tasks: ${tasks.length} total, ${cachedCount} cached, ${buildCount} to build`)
  log(`  Concurrency: ${CONCURRENCY}\n`)

  const outputDirs: string[] = [rootOutput]
  const newManifest: Manifest = {}

  await runParallel(tasks, async (task) => {
    const volOutput = await buildVolume(task)
    outputDirs.push(volOutput)
    // Copy to final dist
    cpSync(volOutput, DIST_FINAL, { recursive: true })
    newManifest[task.id] = { hash: task.cacheKey, timestamp: new Date().toISOString() }
  }, CONCURRENCY)

  // ── Step 3: Merge search indexes ────────────────────────
  await mergeSearchIndexes(outputDirs, DIST_FINAL)

  // ── Step 3.5: Unify hash maps and site data ─────────────
  unifyCrossVolumeData(DIST_FINAL)

  // ── Step 4: Finalize ────────────────────────────────────
  logStep('Step 4/4: Finalizing')
  rmSync(BUILD_TMP, { recursive: true })
  writeManifest(newManifest)

  let outputFiles = 0
  function countFiles(d: string) { for (const e of readdirSync(d, { withFileTypes: true })) { if (e.isDirectory()) countFiles(join(d, e.name)); else outputFiles++ } }
  countFiles(DIST_FINAL)

  const elapsed = ((Date.now() - start) / 1000).toFixed(1)
  log(`\n  ═══ Build Summary ═══`)
  log(`  Status:   ✓ SUCCESS`)
  log(`  Time:     ${elapsed}s (${cachedCount} cached, ${buildCount} built)`)
  log(`  Output:   ${relative(PROJECT_ROOT, DIST_FINAL)} (${outputFiles} files)`)
  log(`  Memory:   ${memMB()}`)
  log(`  Tip:      Use --force for full rebuild, BUILD_CONCURRENCY=N to adjust parallelism`)
}

main().catch((err) => {
  log('\n  BUILD FAILED')
  console.error(err)
  process.exit(1)
})
