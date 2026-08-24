import { execFileSync } from 'node:child_process'
import { mkdir, readFile, rename, rm, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import type MarkdownIt from 'markdown-it'
import { AssetManager } from './assets'
import { exportBookPdf } from './browser'
import { getLocale } from './books'
import {
  buildCanonicalSourceLookup,
  discoverBookDocuments,
  discoverRepositoryDocuments,
} from './catalog'
import { createBookLinkResolver } from './links'
import type {
  BookBuild,
  BookDefinition,
  BookLanguage,
  BuildMetadata,
  BuildReport,
  RenderedDocument,
  TransformStats,
} from './model'
import { assembleBookHtml } from './template'
import { emptyTransformStats, rewriteDocumentLinks, transformDocument } from './transform'
import { postflightPdf, validateRenderedDocuments } from './validate'

export interface BuildBookOptions {
  repositoryRoot: string
  definition: BookDefinition
  language: BookLanguage
  outputDir: string
  markdown: MarkdownIt
  htmlOnly?: boolean
  keepStaging?: boolean
  timeoutMs?: number
  executablePath?: string
  log?: (message: string) => void
}

export interface BuildBookResult {
  build: BookBuild
  report: BuildReport
}

function revision(repositoryRoot: string): string {
  return execFileSync('git', ['rev-parse', 'HEAD'], { cwd: repositoryRoot, encoding: 'utf8' }).trim()
}

function version(repositoryRoot: string): string {
  try {
    return execFileSync('git', ['describe', '--tags', '--always', '--dirty'], {
      cwd: repositoryRoot,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore'],
    }).trim()
  } catch {
    return revision(repositoryRoot).slice(0, 12)
  }
}

function generationDate(): string {
  const epoch = process.env.SOURCE_DATE_EPOCH
  if (epoch && /^\d+$/.test(epoch)) return new Date(Number(epoch) * 1000).toISOString()
  return new Date().toISOString()
}

async function cleanupPublicationOutputs(pdfPath: string, reportPath: string): Promise<void> {
  // Cleanup must never hide the original browser/postflight/report error.
  await Promise.allSettled([
    rm(pdfPath, { force: true }),
    rm(reportPath, { force: true }),
  ])
}

function sumStats(documents: readonly RenderedDocument[]): TransformStats {
  const result = emptyTransformStats()
  for (const document of documents) {
    for (const key of Object.keys(result) as Array<keyof TransformStats>) {
      result[key] += document.stats[key]
    }
  }
  return result
}

async function packageVersions(repositoryRoot: string): Promise<Record<string, string>> {
  const packageJson = JSON.parse(await readFile(join(repositoryRoot, 'package.json'), 'utf8')) as {
    devDependencies?: Record<string, string>
  }
  const dependencies = packageJson.devDependencies ?? {}
  return {
    node: process.version,
    vitepress: dependencies.vitepress ?? 'unknown',
    pagedjs: dependencies.pagedjs ?? 'unknown',
    puppeteer: dependencies.puppeteer ?? 'unknown',
    mermaid: dependencies.mermaid ?? 'unknown',
    chrome: 'not-run',
  }
}

function safeBuildDirectory(repositoryRoot: string, definition: BookDefinition, language: BookLanguage): string {
  const base = resolve(repositoryRoot, '.pdf-build')
  const selected = resolve(base, `${definition.id}-${language}`)
  if (!selected.startsWith(`${base}/`)) throw new Error(`Unsafe PDF staging path: ${selected}`)
  return selected
}

export async function buildBook(options: BuildBookOptions): Promise<BuildBookResult> {
  const startedAt = Date.now()
  const elapsedMs: Record<string, number> = {}
  const log = options.log ?? (() => undefined)
  const repositoryRoot = resolve(options.repositoryRoot)
  const outputDir = resolve(options.outputDir)
  const stagingDir = safeBuildDirectory(repositoryRoot, options.definition, options.language)
  await rm(stagingDir, { recursive: true, force: true })
  await mkdir(stagingDir, { recursive: true })
  await mkdir(outputDir, { recursive: true })

  const locale = getLocale(options.language)
  const metadata: BuildMetadata = {
    version: version(repositoryRoot),
    revision: revision(repositoryRoot),
    generatedAt: generationDate(),
  }
  const shortRevision = metadata.revision.slice(0, 12)
  const baseName = `awesome-modern-cpp-${options.definition.id}-${options.language}-${shortRevision}`
  const htmlPath = join(stagingDir, 'index.html')
  const pdfPath = join(outputDir, `${baseName}.pdf`)
  // HTML-only runs are diagnostics, not publication artifacts. Keeping their
  // report in staging prevents them from overwriting the report paired with an
  // existing PDF built from the same HEAD revision.
  const reportPath = options.htmlOnly
    ? join(stagingDir, 'build-report.json')
    : join(outputDir, `${baseName}.json`)
  const assetDir = join(stagingDir, 'assets')

  if (!options.htmlOnly) {
    // A failed rebuild must not leave a same-SHA PDF/report from an older dirty
    // worktree state looking current. These are exact, validated output paths.
    await Promise.all([
      rm(pdfPath, { force: true }),
      rm(reportPath, { force: true }),
    ])
  }

  let stageStarted = Date.now()
  log(`[catalog] ${options.definition.id}/${options.language}`)
  const sources = discoverBookDocuments({ repositoryRoot, book: options.definition, locale })
  const repositoryDocuments = discoverRepositoryDocuments({ repositoryRoot, locales: [locale] })
  const repositoryLookup = buildCanonicalSourceLookup(repositoryDocuments)
  const resolveLink = createBookLinkResolver({
    repositoryRoot,
    locale,
    bookDocuments: sources,
    repositoryLookup,
  })
  elapsedMs.catalog = Date.now() - stageStarted

  stageStarted = Date.now()
  const assets = new AssetManager(repositoryRoot, stagingDir)
  const runtimeAssets = await assets.initialize({
    needsDrawio: sources.some((document) => /\.drawio(?:[?#)"']|$)/i.test(document.markdown)),
  })
  elapsedMs.assets = Date.now() - stageStarted

  stageStarted = Date.now()
  const rendered: RenderedDocument[] = []
  for (const [index, source] of sources.entries()) {
    log(`[render ${index + 1}/${sources.length}] ${source.repositoryPath}`)
    rendered.push(await transformDocument(source, {
      repositoryRoot,
      markdown: options.markdown,
      locale,
      assets,
      resolveLink,
    }))
  }
  for (const document of rendered) rewriteDocumentLinks(document, resolveLink)
  validateRenderedDocuments(rendered)
  elapsedMs.markdown = Date.now() - stageStarted

  stageStarted = Date.now()
  const html = assembleBookHtml(options.definition, locale, rendered, metadata, runtimeAssets)
  await writeFile(htmlPath, html, 'utf8')
  elapsedMs.assemble = Date.now() - stageStarted

  const build: BookBuild = {
    definition: options.definition,
    locale,
    documents: rendered,
    metadata,
    outputDir,
    assetDir,
    htmlPath,
    pdfPath,
    reportPath,
  }
  const report: BuildReport = {
    schemaVersion: 1,
    book: options.definition.id,
    language: options.language,
    revision: metadata.revision,
    generatedAt: metadata.generatedAt,
    sourceDocuments: sources.length,
    renderedDocuments: rendered.length,
    elapsedMs,
    transforms: sumStats(rendered),
    warnings: [...assets.warnings],
    versions: await packageVersions(repositoryRoot),
  }

  if (!options.htmlOnly) {
    try {
      stageStarted = Date.now()
      log(`[paginate] ${options.definition.id}/${options.language}`)
      const browser = await exportBookPdf({
        stagingDir,
        outputPath: pdfPath,
        timeoutMs: options.timeoutMs,
        executablePath: options.executablePath,
      })
      elapsedMs.browser = browser.elapsedMs
      report.pageCount = browser.pageCount
      report.versions.chrome = browser.chromeVersion

      const postflight = await postflightPdf(pdfPath, { expectCjk: options.language === 'zh' })
      if (postflight.pageCount !== undefined && postflight.pageCount !== browser.pageCount) {
        throw new Error(`PDF page count ${postflight.pageCount} does not match paged DOM ${browser.pageCount}`)
      }
      report.pdfBytes = postflight.bytes
      report.warnings.push(...postflight.warnings)
      elapsedMs.postflight = Date.now() - stageStarted - browser.elapsedMs
    } catch (error) {
      await cleanupPublicationOutputs(pdfPath, reportPath)
      throw error
    }
  }

  if (!options.keepStaging && !options.htmlOnly) {
    try {
      await rm(stagingDir, { recursive: true, force: true })
    } catch (error) {
      report.warnings.push(`Could not remove staging directory ${stagingDir}: ${String(error)}`)
    }
  }
  elapsedMs.total = Date.now() - startedAt
  const temporaryReportPath = `${reportPath}.tmp-${process.pid}-${Date.now()}`
  try {
    try {
      await writeFile(temporaryReportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8')
      await rename(temporaryReportPath, reportPath)
    } finally {
      await rm(temporaryReportPath, { force: true }).catch(() => undefined)
    }
  } catch (error) {
    if (!options.htmlOnly) {
      await cleanupPublicationOutputs(pdfPath, reportPath)
    }
    throw error
  }
  return { build, report }
}
