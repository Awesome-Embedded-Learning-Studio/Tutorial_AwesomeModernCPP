import { execFile } from 'child_process'
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
  { name: 'vol10', srcDir: 'vol10-open-lecture-notes', urlPrefix: '/vol10-open-lecture-notes' },
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
const CODE_EXAMPLES = join(PROJECT_ROOT, 'code', 'examples')
const VITEPRESS_BIN = join(resolve(require.resolve('vitepress/package.json'), '..'), 'bin', 'vitepress.js')

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
import withDrawio from '@dhlx/vitepress-plugin-drawio'
import { sharedBase, ${lang === 'en' ? 'sharedEnThemeConfig' : 'sharedThemeConfig'} } from '${relShared}'
import { volumeSidebar } from '${relSidebar}'

export default withDrawio(defineConfig({
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
}), {
  width: '100%',
  height: '600px',
  darkMode: 'auto',
  resize: true,
  zoom: true,
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
import withDrawio from '@dhlx/vitepress-plugin-drawio'
import { sharedBase, sharedThemeConfig, sharedEnThemeConfig } from '${relShared}'
import { navZh, navEn } from '${relNav}'
import { buildSidebar } from '${relSidebar}'

export default withDrawio(defineConfig({
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
}), {
  width: '100%',
  height: '600px',
  darkMode: 'auto',
  resize: true,
  zoom: true,
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

interface SearchIndexSource {
  dir: string
  lang: 'zh' | 'en' | 'mixed'
}

function prepareVolume(vol: Volume, lang: 'zh' | 'en', manifest: Manifest): BuildTask {
  const volDocDir = lang === 'en' ? join(DOCUMENTS, 'en', vol.srcDir) : join(DOCUMENTS, vol.srcDir)
  const id = lang === 'en' ? `${vol.name}-en` : vol.name
  const cacheKey = existsSync(volDocDir) ? hashDir(volDocDir) : ''
  const prev = manifest[id]
  const cached = !FORCE_REBUILD && prev && prev.hash === cacheKey && existsSync(join(CACHE_DIR, 'output', id))
  return { id, vol, lang, cacheKey, cached }
}

function execFileAsync(file: string, args: string[], opts?: { cwd?: string }): Promise<void> {
  return new Promise((resolve, reject) => {
    execFile(file, args, { cwd: opts?.cwd ?? PROJECT_ROOT }, (err, stdout, stderr) => {
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
  await execFileAsync(process.execPath, [VITEPRESS_BIN, 'build', relative(PROJECT_ROOT, tmpSite)])
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

function findSearchIndexFiles(dir: string): Map<'root' | 'en', string> {
  const result = new Map<'root' | 'en', string>()
  const chunksDir = join(dir, 'assets', 'chunks')
  if (!existsSync(chunksDir)) return result
  for (const f of readdirSync(chunksDir)) {
    const m = f.match(/^@localSearchIndex(root|en)\.[^.]+\.js$/)
    if (m) result.set(m[1] as 'root' | 'en', join(chunksDir, f))
  }
  return result
}

type SerializedSearchIndex = {
  documentCount: number
  nextId: number
  documentIds: Record<string, string>
  fieldIds: Record<string, number>
  fieldLength: Record<string, number[]>
  averageFieldLength: number[]
  storedFields: Record<string, Record<string, unknown>>
  dirtCount: number
  index: Array<[string, Record<string, Record<string, number>>]>
  serializationVersion: number
}

function findSearchIndexExportStart(content: string): number {
  let match: RegExpExecArray | null
  let exportStart = -1
  const exportPattern = /;?\s*export\s*\{/g
  while ((match = exportPattern.exec(content)) !== null) {
    exportStart = match.index
  }
  return exportStart
}

function extractSearchIndex(indexPath: string): SerializedSearchIndex | null {
  const content = readFileSync(indexPath, 'utf-8')
  const assignment = content.match(/^const\s+\w+\s*=\s*/)
  const exportStart = findSearchIndexExportStart(content)
  if (!assignment || exportStart === -1) {
    log(`  ⚠ Could not parse: ${relative(PROJECT_ROOT, indexPath)}`)
    return null
  }
  let expr = content.slice(assignment[0].length, exportStart).trim()
  if (expr.endsWith(';')) expr = expr.slice(0, -1).trim()
  const jsonStr: string = new Function(`return (${expr})`)()
  return JSON.parse(jsonStr)
}

function mergeSerializedSearchIndexes(indexes: SerializedSearchIndex[]): SerializedSearchIndex {
  if (indexes.length === 0) throw new Error('No search indexes to merge')

  const fieldIds = indexes[0].fieldIds
  const fieldCount = Object.keys(fieldIds).length
  const merged: SerializedSearchIndex = {
    documentCount: 0,
    nextId: 0,
    documentIds: {},
    fieldIds,
    fieldLength: {},
    averageFieldLength: Array(fieldCount).fill(0),
    storedFields: {},
    dirtCount: 0,
    index: [],
    serializationVersion: indexes[0].serializationVersion,
  }

  const termIndex = new Map<string, Record<string, Record<string, number>>>()
  const fieldLengthSums = Array(fieldCount).fill(0)

  for (const data of indexes) {
    const localToGlobal = new Map<string, string>()
    const fieldMap = new Map<string, string>()

    for (const [fieldName, localFieldId] of Object.entries(data.fieldIds)) {
      const targetFieldId = fieldIds[fieldName]
      if (targetFieldId === undefined) {
        throw new Error(`Incompatible search field: ${fieldName}`)
      }
      fieldMap.set(String(localFieldId), String(targetFieldId))
    }

    for (const [localId, url] of Object.entries(data.documentIds)) {
      const globalId = String(merged.nextId++)
      localToGlobal.set(localId, globalId)
      merged.documentIds[globalId] = url
      merged.storedFields[globalId] = data.storedFields[localId] || {}
      const lengths = data.fieldLength[localId] || []
      merged.fieldLength[globalId] = Array(fieldCount).fill(0)
      for (const [localFieldId, targetFieldId] of fieldMap) {
        const len = lengths[Number(localFieldId)] || 0
        const targetIndex = Number(targetFieldId)
        merged.fieldLength[globalId][targetIndex] = len
        fieldLengthSums[targetIndex] += len
      }
    }

    merged.dirtCount += data.dirtCount || 0

    for (const [term, postings] of data.index) {
      const mergedPostings = termIndex.get(term) || {}
      for (const [localFieldId, docs] of Object.entries(postings)) {
        const targetFieldId = fieldMap.get(localFieldId)
        if (targetFieldId === undefined) continue
        const fieldPostings = mergedPostings[targetFieldId] || {}
        for (const [localId, frequency] of Object.entries(docs)) {
          const globalId = localToGlobal.get(localId)
          if (globalId === undefined) continue
          fieldPostings[globalId] = (fieldPostings[globalId] || 0) + frequency
        }
        mergedPostings[targetFieldId] = fieldPostings
      }
      termIndex.set(term, mergedPostings)
    }
  }

  merged.documentCount = Object.keys(merged.documentIds).length
  merged.averageFieldLength = fieldLengthSums.map((sum) => merged.documentCount > 0 ? sum / merged.documentCount : 0)
  merged.index = [...termIndex.entries()]
  return merged
}

function buildSearchIndexJs(index: SerializedSearchIndex): string {
  const json = JSON.stringify(index)
  // Double-stringify to get a properly escaped JS string literal (handles backticks, quotes, etc.)
  return `const e=${JSON.stringify(json)};export{e as default};`
}

async function mergeSearchIndexes(sources: SearchIndexSource[], finalDist: string) {
  logStep('Step 3/4: Merging search indexes')

  const indexesByLang: Record<'zh' | 'en', SerializedSearchIndex[]> = { zh: [], en: [] }
  const targetsByLang: Record<'zh' | 'en', Set<string>> = { zh: new Set(), en: new Set() }

  for (const source of sources) {
    for (const [locale, indexPath] of findSearchIndexFiles(source.dir)) {
      const lang = source.lang === 'mixed'
        ? (locale === 'en' ? 'en' : 'zh')
        : source.lang
      const index = extractSearchIndex(indexPath)
      if (!index) continue
      log(`  ${lang}: ${index.documentCount} docs from ${relative(PROJECT_ROOT, source.dir)} (${locale})`)
      indexesByLang[lang].push(index)

      const target = join(finalDist, 'assets', 'chunks', basename(indexPath))
      if (existsSync(target)) {
        targetsByLang[lang].add(target)
      } else {
        log(`  ⚠ ${lang}: target missing for ${basename(indexPath)}`)
      }
    }
  }

  for (const lang of ['zh', 'en'] as const) {
    const indexes = indexesByLang[lang]
    if (indexes.length === 0) { log(`  ${lang}: no indexes, skipping`); continue }
    const mergedIndex = mergeSerializedSearchIndexes(indexes)
    log(`  ${lang}: merging ${mergedIndex.documentCount} total docs...`)
    const js = buildSearchIndexJs(mergedIndex)
    const allTargets = [...targetsByLang[lang]]
    if (allTargets.length === 0) {
      log(`  ⚠ ${lang}: no target index files in final dist!`)
      continue
    }
    writeFileSync(allTargets[0], js)
    const canonicalName = basename(allTargets[0])
    const stub = `export{default}from"./${canonicalName}";`
    for (let i = 1; i < allTargets.length; i++) {
      writeFileSync(allTargets[i], stub)
    }
    const savedMB = ((js.length - stub.length) * (allTargets.length - 1) / 1024 / 1024).toFixed(1)
    log(`  ${lang}: ✓ 1 canonical + ${allTargets.length - 1} stubs (saved ${savedMB} MB)`)
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
  await execFileAsync(process.execPath, [VITEPRESS_BIN, 'build', '.'], { cwd: rootTmpSite })
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

  const searchSources: SearchIndexSource[] = [{ dir: rootOutput, lang: 'mixed' }]
  const newManifest: Manifest = {}

  await runParallel(tasks, async (task) => {
    const volOutput = await buildVolume(task)
    searchSources.push({ dir: volOutput, lang: task.lang })
    // Copy to final dist
    cpSync(volOutput, DIST_FINAL, { recursive: true })
    newManifest[task.id] = { hash: task.cacheKey, timestamp: new Date().toISOString() }
  }, CONCURRENCY)

  // ── Step 3: Merge search indexes ────────────────────────
  await mergeSearchIndexes(searchSources, DIST_FINAL)

  // ── Step 3.5: Unify hash maps and site data ─────────────
  unifyCrossVolumeData(DIST_FINAL)

  // Make code examples available to interactive client-side components.
  if (existsSync(CODE_EXAMPLES)) {
    const examplesOutput = join(DIST_FINAL, 'code', 'examples')
    mkdirSync(join(DIST_FINAL, 'code'), { recursive: true })
    if (existsSync(examplesOutput)) rmSync(examplesOutput, { recursive: true })
    cpSync(CODE_EXAMPLES, examplesOutput, { recursive: true })
  }

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
