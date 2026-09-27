import { createHash } from 'node:crypto'
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs'
import { basename, dirname, join, posix, relative, resolve } from 'node:path'

import matter from 'gray-matter'

import { CONTENT_UNITS, getLocale } from './books'
import type {
  BookDefinition,
  BookLocale,
  ContentUnit,
  Frontmatter,
  SourceDocument,
} from './model'

const EXCLUDED_MARKDOWN = new Set(['readme.md', 'tags.md'])
const NATURAL_COLLATOR = new Intl.Collator('en', {
  numeric: true,
  sensitivity: 'base',
})

export interface DiscoverBookOptions {
  repositoryRoot: string
  book: BookDefinition
  locale: BookLocale
  units?: readonly ContentUnit[]
}

export interface DiscoverRepositoryOptions {
  repositoryRoot: string
  locales?: readonly BookLocale[]
  units?: readonly ContentUnit[]
}

export interface CanonicalSourceLookup {
  /** Primary clean URL for every source document. */
  readonly byCanonicalPath: ReadonlyMap<string, SourceDocument>
  /** Stable, globally unique document identifier. */
  readonly byDocId: ReadonlyMap<string, SourceDocument>
  /** Clean URLs plus accepted source/HTML/index spellings. */
  readonly aliases: ReadonlyMap<string, SourceDocument>
}

export interface ResolveCanonicalOptions {
  /** Repository deployment prefixes which should be removed from absolute paths. */
  siteBasePaths?: readonly string[]
  /** Origins that are allowed to be treated as links to this repository. */
  allowedOrigins?: readonly string[]
}

interface CatalogRecord {
  document: SourceDocument
  baseName: string
  directory: string
  isIndex: boolean
  chapterSort?: SortValue
  orderSort?: SortValue
  sidebarSort?: SortValue
}

interface DirectoryNode {
  name: string
  relativePath: string
  index?: CatalogRecord
  articles: CatalogRecord[]
  children: Map<string, DirectoryNode>
}

interface SortValue {
  kind: 'number' | 'text'
  value: number | string
}

type DirectoryEntry =
  | { kind: 'article'; record: CatalogRecord }
  | { kind: 'directory'; node: DirectoryNode }

const DEFAULT_SITE_BASE_PATHS = ['/Tutorial_AwesomeModernCPP'] as const
const DEFAULT_ALLOWED_ORIGINS = [
  'https://awesome-embedded-learning-studio.github.io',
] as const

/**
 * Recursively discover and order the Markdown sources belonging to one book.
 * The order of `book.units` is significant and is preserved.
 */
export function discoverBookDocuments(options: DiscoverBookOptions): SourceDocument[] {
  const availableUnits = indexUnits(options.units ?? CONTENT_UNITS)
  const documents: SourceDocument[] = []

  for (const unitId of options.book.units) {
    const unit = availableUnits.get(unitId)
    if (!unit) {
      throw new Error(
        `Book "${options.book.id}" references unknown content unit "${unitId}"`,
      )
    }
    const unitDocuments = discoverUnitDocuments(options.repositoryRoot, unit, options.locale)
    if (options.book.units.length > 1) {
      for (const document of unitDocuments) {
        if (document.kind === 'book-index') document.kind = 'chapter-index'
      }
    }
    documents.push(...unitDocuments)
  }

  // Constructing the lookup is also the central uniqueness assertion. Running it
  // here prevents a partial/ambiguous catalog from reaching the render stage.
  buildCanonicalSourceLookup(documents)
  return documents
}

/** Discover both locales (by default) for every registered content unit. */
export function discoverRepositoryDocuments(
  options: DiscoverRepositoryOptions,
): SourceDocument[] {
  const units = options.units ?? CONTENT_UNITS
  const locales = options.locales ?? [getLocale('zh'), getLocale('en')]
  const documents: SourceDocument[] = []

  for (const locale of locales) {
    for (const unit of units) {
      documents.push(...discoverUnitDocuments(options.repositoryRoot, unit, locale))
    }
  }

  buildCanonicalSourceLookup(documents)
  return documents
}

/** Discover all configured sources and return a ready-to-use canonical lookup. */
export function buildRepositoryCanonicalSourceLookup(
  options: DiscoverRepositoryOptions,
): CanonicalSourceLookup {
  return buildCanonicalSourceLookup(discoverRepositoryDocuments(options))
}

/** Build the full-repository lookup consumed by Markdown link transformation. */
export function buildCanonicalSourceLookup(
  documents: Iterable<SourceDocument>,
): CanonicalSourceLookup {
  const byCanonicalPath = new Map<string, SourceDocument>()
  const byDocId = new Map<string, SourceDocument>()
  const aliases = new Map<string, SourceDocument>()

  for (const document of documents) {
    const canonicalPath = normalizeCanonicalPath(document.canonicalPath)
    insertUnique(byCanonicalPath, canonicalPath, document, 'canonical path')
    insertUnique(byDocId, document.docId, document, 'document id')

    for (const alias of canonicalAliases(document)) {
      insertUnique(aliases, alias, document, 'canonical alias')
    }
  }

  return { byCanonicalPath, byDocId, aliases }
}

/**
 * Return canonical aliases for an href, in resolution order. The function does
 * not perform lookup itself so callers can distinguish same-book and cross-book
 * targets using their own catalog(s).
 */
export function resolveCanonicalCandidates(
  href: string,
  from: SourceDocument,
  options: ResolveCanonicalOptions = {},
): string[] {
  const value = href.trim()
  if (!value) return []

  const siteBasePaths = options.siteBasePaths ?? DEFAULT_SITE_BASE_PATHS
  const allowedOrigins = new Set(options.allowedOrigins ?? DEFAULT_ALLOWED_ORIGINS)
  let linkPath: string

  if (/^[a-z][a-z\d+.-]*:/i.test(value)) {
    let url: URL
    try {
      url = new URL(value)
    } catch {
      return []
    }
    if (!['http:', 'https:'].includes(url.protocol) || !allowedOrigins.has(url.origin)) {
      return []
    }
    linkPath = url.pathname
  } else {
    if (value.startsWith('//')) return []
    linkPath = value.split(/[?#]/, 1)[0]
  }

  try {
    linkPath = decodeURI(linkPath)
  } catch {
    // Keep the original spelling. A malformed escape will simply not resolve.
  }

  const hadTrailingSlash = linkPath.endsWith('/')
  const fromDirectory = from.relativePath.toLowerCase().endsWith('/index.md')
    || from.relativePath.toLowerCase() === 'index.md'
    ? from.canonicalPath
    : posix.dirname(from.canonicalPath)

  let absolutePath = !linkPath
    ? from.canonicalPath
    : linkPath.startsWith('/')
      ? linkPath
      : posix.resolve(fromDirectory, linkPath)
  absolutePath = stripSiteBasePath(absolutePath, siteBasePaths)
  absolutePath = normalizeCanonicalPath(
    hadTrailingSlash && absolutePath !== '/' ? `${absolutePath}/` : absolutePath,
  )

  return linkAliasesInPriorityOrder(absolutePath)
}

/** Resolve the first matching canonical candidate, if any. */
export function resolveCanonicalSource(
  href: string,
  from: SourceDocument,
  lookup: CanonicalSourceLookup,
  options: ResolveCanonicalOptions = {},
): SourceDocument | undefined {
  for (const candidate of resolveCanonicalCandidates(href, from, options)) {
    const document = lookup.aliases.get(candidate)
      ?? lookup.byCanonicalPath.get(candidate)
    if (document) return document
  }
  return undefined
}

/** Convert a repository-relative Markdown path to its clean canonical route. */
export function canonicalPathFor(
  unit: ContentUnit,
  locale: BookLocale,
  relativePath: string,
): string {
  const normalizedRelative = toPosix(relativePath).replace(/^\/+/, '')
  const lowerName = posix.basename(normalizedRelative).toLowerCase()
  const localePrefix = locale.sourcePrefix
    ? `/${locale.sourcePrefix.replace(/^\/+|\/+$/g, '')}`
    : ''
  const unitPrefix = `/${unit.urlPrefix.replace(/^\/+|\/+$/g, '')}`
  const base = normalizeCanonicalPath(`${localePrefix}${unitPrefix}`)

  if (lowerName === 'index.md') {
    const directory = posix.dirname(normalizedRelative)
    const suffix = directory === '.' ? '' : `/${directory}`
    return normalizeCanonicalPath(`${base}${suffix}/`)
  }

  const route = normalizedRelative.replace(/\.md$/i, '')
  return normalizeCanonicalPath(`${base}/${route}`)
}

function discoverUnitDocuments(
  repositoryRoot: string,
  unit: ContentUnit,
  locale: BookLocale,
): SourceDocument[] {
  const documentsRoot = resolve(repositoryRoot, 'documents')
  const sourceRoot = resolve(
    documentsRoot,
    ...(locale.sourcePrefix ? locale.sourcePrefix.split('/') : []),
    unit.sourceDir,
  )

  if (!existsSync(sourceRoot) || !statSync(sourceRoot).isDirectory()) {
    throw new Error(
      `Missing source directory for ${locale.language}/${unit.id}: ${sourceRoot}`,
    )
  }

  const root = createDirectoryNode('', '')
  const records = collectMarkdownPaths(sourceRoot).map((sourcePath) =>
    readCatalogRecord(repositoryRoot, sourceRoot, sourcePath, unit, locale),
  )

  for (const record of records) addRecordToTree(root, record)

  const ordered: SourceDocument[] = []
  flattenDirectory(root, ordered)
  return ordered
}

function collectMarkdownPaths(root: string): string[] {
  const found: string[] = []

  function walk(directory: string): void {
    const entries = readdirSync(directory, { withFileTypes: true })
      .sort((left, right) => naturalCompare(left.name, right.name))

    for (const entry of entries) {
      const sourcePath = join(directory, entry.name)
      if (entry.isDirectory()) {
        walk(sourcePath)
      } else if (
        entry.isFile()
        && entry.name.toLowerCase().endsWith('.md')
        && !EXCLUDED_MARKDOWN.has(entry.name.toLowerCase())
      ) {
        found.push(sourcePath)
      }
    }
  }

  walk(root)
  return found
}

function readCatalogRecord(
  repositoryRoot: string,
  sourceRoot: string,
  sourcePath: string,
  unit: ContentUnit,
  locale: BookLocale,
): CatalogRecord {
  const relativePath = toPosix(relative(sourceRoot, sourcePath))
  const repositoryPath = toPosix(relative(repositoryRoot, sourcePath))
  const baseName = basename(sourcePath)
  const isIndex = baseName.toLowerCase() === 'index.md'
  const directory = toPosix(dirname(relativePath)) === '.'
    ? ''
    : toPosix(dirname(relativePath))
  let parsed: matter.GrayMatterFile<string>

  try {
    parsed = matter(readFileSync(sourcePath, 'utf8'))
  } catch (error) {
    throw new Error(`Cannot parse frontmatter in ${repositoryPath}: ${errorMessage(error)}`)
  }

  const frontmatter = parsed.data as Frontmatter
  const title = readOptionalText(frontmatter.title, 'title', repositoryPath)
    || firstHeading(parsed.content)
    || humanize(isIndex ? posix.basename(directory || unit.sourceDir) : baseName.replace(/\.md$/i, ''))
  const description = readOptionalText(
    frontmatter.description,
    'description',
    repositoryPath,
  )
  const chapter = readOptionalText(frontmatter.chapter, 'chapter', repositoryPath)
  const frontmatterOrder = readOptionalNumber(frontmatter.order, 'order', repositoryPath)
  const sidebarOrder = readOptionalNumber(
    frontmatter.sidebar_order,
    'sidebar_order',
    repositoryPath,
  )
  const inferredOrder = leadingNumber(baseName)
  const canonicalPath = canonicalPathFor(unit, locale, relativePath)

  const document: SourceDocument = {
    sourcePath,
    relativePath,
    repositoryPath,
    unit,
    docId: documentIdFor(locale, unit, relativePath),
    canonicalPath,
    title,
    description,
    chapter,
    order: frontmatterOrder ?? (isIndex ? sidebarOrder : inferredOrder) ?? 0,
    kind: isIndex
      ? directory === '' ? 'book-index' : 'chapter-index'
      : 'article',
    markdown: parsed.content,
    frontmatter,
  }

  return {
    document,
    baseName,
    directory,
    isIndex,
    chapterSort: sortValue(frontmatter.chapter, 'chapter', repositoryPath),
    orderSort: sortValue(frontmatter.order, 'order', repositoryPath),
    sidebarSort: sortValue(frontmatter.sidebar_order, 'sidebar_order', repositoryPath),
  }
}

function createDirectoryNode(name: string, relativePath: string): DirectoryNode {
  return { name, relativePath, articles: [], children: new Map() }
}

function addRecordToTree(root: DirectoryNode, record: CatalogRecord): void {
  let node = root
  if (record.directory) {
    let accumulated = ''
    for (const segment of record.directory.split('/')) {
      accumulated = accumulated ? `${accumulated}/${segment}` : segment
      let child = node.children.get(segment)
      if (!child) {
        child = createDirectoryNode(segment, accumulated)
        node.children.set(segment, child)
      }
      node = child
    }
  }

  if (record.isIndex) {
    if (node.index) {
      throw new Error(
        `Duplicate index documents: ${node.index.document.repositoryPath} and ${record.document.repositoryPath}`,
      )
    }
    node.index = record
  } else {
    node.articles.push(record)
  }
}

function flattenDirectory(node: DirectoryNode, output: SourceDocument[]): void {
  if (node.index) output.push(node.index.document)

  const entries: DirectoryEntry[] = [
    ...node.articles.map((record): DirectoryEntry => ({ kind: 'article', record })),
    ...[...node.children.values()].map((child): DirectoryEntry => ({
      kind: 'directory',
      node: child,
    })),
  ]
  entries.sort(compareDirectoryEntries)

  for (const entry of entries) {
    if (entry.kind === 'article') output.push(entry.record.document)
    else flattenDirectory(entry.node, output)
  }
}

function compareDirectoryEntries(left: DirectoryEntry, right: DirectoryEntry): number {
  const leftPrimary = entryPrimarySort(left)
  const rightPrimary = entryPrimarySort(right)
  let comparison = compareOptionalSortValues(leftPrimary, rightPrimary)
  if (comparison !== 0) return comparison

  comparison = compareOptionalSortValues(entrySecondarySort(left), entrySecondarySort(right))
  if (comparison !== 0) return comparison

  comparison = naturalCompare(entryName(left), entryName(right))
  if (comparison !== 0) return comparison
  return entryPath(left).localeCompare(entryPath(right), 'en')
}

function entryPrimarySort(entry: DirectoryEntry): SortValue | undefined {
  if (entry.kind === 'directory') return entry.node.index?.sidebarSort
  return entry.record.chapterSort ?? entry.record.orderSort
}

function entrySecondarySort(entry: DirectoryEntry): SortValue | undefined {
  if (entry.kind === 'directory') return undefined
  return entry.record.chapterSort ? entry.record.orderSort : undefined
}

function entryName(entry: DirectoryEntry): string {
  return entry.kind === 'directory' ? entry.node.name : entry.record.baseName
}

function entryPath(entry: DirectoryEntry): string {
  return entry.kind === 'directory'
    ? entry.node.relativePath
    : entry.record.document.relativePath
}

function sortValue(
  value: unknown,
  field: string,
  repositoryPath: string,
): SortValue | undefined {
  if (value === undefined || value === null || value === '') return undefined
  if (typeof value === 'number') {
    if (!Number.isFinite(value)) invalidFrontmatter(field, repositoryPath, value)
    return { kind: 'number', value }
  }
  if (typeof value === 'string') {
    const trimmed = value.trim()
    if (!trimmed) return undefined
    const numeric = Number(trimmed)
    return Number.isFinite(numeric)
      ? { kind: 'number', value: numeric }
      : { kind: 'text', value: trimmed }
  }
  invalidFrontmatter(field, repositoryPath, value)
}

function compareOptionalSortValues(
  left: SortValue | undefined,
  right: SortValue | undefined,
): number {
  if (left && !right) return -1
  if (!left && right) return 1
  if (!left || !right) return 0
  if (left.kind === 'number' && right.kind === 'number') {
    return (left.value as number) - (right.value as number)
  }
  if (left.kind === 'number') return -1
  if (right.kind === 'number') return 1
  return naturalCompare(String(left.value), String(right.value))
}

function naturalCompare(left: string, right: string): number {
  const collated = NATURAL_COLLATOR.compare(left, right)
  return collated || left.localeCompare(right, 'en')
}

function canonicalAliases(document: SourceDocument): string[] {
  return linkAliasesInPriorityOrder(normalizeCanonicalPath(document.canonicalPath))
}

function linkAliasesInPriorityOrder(path: string): string[] {
  const result: string[] = []
  const add = (candidate: string): void => {
    const normalized = normalizeCanonicalPath(candidate)
    if (!result.includes(normalized)) result.push(normalized)
  }
  const indexMatch = path.match(/^(.*\/)?index(?:\.md|\.html)?$/i)
  const extensionMatch = path.match(/\.(?:md|html)$/i)

  if (indexMatch) {
    const directory = indexMatch[1] || '/'
    add(directory.endsWith('/') ? directory : `${directory}/`)
    add(directory.replace(/\/+$/, '') || '/')
    add(path)
    return result
  }

  if (extensionMatch) {
    const clean = path.replace(/\.(?:md|html)$/i, '') || '/'
    add(clean)
    add(path)
    add(clean === '/' || clean.endsWith('/') ? clean : `${clean}/`)
    return result
  }

  add(path)
  if (path !== '/') {
    const withoutSlash = path.replace(/\/+$/, '')
    if (path.endsWith('/')) {
      add(withoutSlash)
      add(`${withoutSlash}/index`)
      add(`${withoutSlash}/index.md`)
      add(`${withoutSlash}/index.html`)
    } else {
      add(`${path}/`)
      add(`${path}.md`)
      add(`${path}.html`)
    }
  }
  return result
}

function normalizeCanonicalPath(value: string): string {
  const trailingSlash = value.length > 1 && value.endsWith('/')
  const pathOnly = value.split(/[?#]/, 1)[0].replace(/\\/g, '/')
  let normalized = posix.normalize(pathOnly.startsWith('/') ? pathOnly : `/${pathOnly}`)
  if (trailingSlash && normalized !== '/' && !normalized.endsWith('/')) normalized += '/'
  return normalized
}

function stripSiteBasePath(path: string, prefixes: readonly string[]): string {
  const normalized = normalizeCanonicalPath(path)
  const ordered = [...prefixes]
    .map((prefix) => normalizeCanonicalPath(prefix).replace(/\/+$/, ''))
    .filter(Boolean)
    .sort((left, right) => right.length - left.length)

  for (const prefix of ordered) {
    if (normalized === prefix) return '/'
    if (normalized.startsWith(`${prefix}/`)) {
      return normalized.slice(prefix.length) || '/'
    }
  }
  return normalized
}

function documentIdFor(
  locale: BookLocale,
  unit: ContentUnit,
  relativePath: string,
): string {
  const identity = `${locale.language}:${unit.id}:${toPosix(relativePath).replace(/\.md$/i, '')}`
  const readable = identity
    .normalize('NFKD')
    .replace(/[^a-zA-Z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .toLowerCase()
  const digest = createHash('sha256').update(identity).digest('hex').slice(0, 10)
  return `doc-${readable || 'source'}-${digest}`
}

function indexUnits(units: readonly ContentUnit[]): Map<string, ContentUnit> {
  const indexed = new Map<string, ContentUnit>()
  for (const unit of units) {
    if (indexed.has(unit.id)) throw new Error(`Duplicate content unit id "${unit.id}"`)
    indexed.set(unit.id, unit)
  }
  return indexed
}

function insertUnique(
  destination: Map<string, SourceDocument>,
  key: string,
  document: SourceDocument,
  label: string,
): void {
  const existing = destination.get(key)
  if (existing && existing !== document) {
    throw new Error(
      `Duplicate ${label} "${key}": ${existing.repositoryPath} and ${document.repositoryPath}`,
    )
  }
  destination.set(key, document)
}

function readOptionalText(value: unknown, field: string, repositoryPath: string): string {
  if (value === undefined || value === null) return ''
  if (typeof value === 'string' || typeof value === 'number') return String(value).trim()
  invalidFrontmatter(field, repositoryPath, value)
}

function readOptionalNumber(
  value: unknown,
  field: string,
  repositoryPath: string,
): number | undefined {
  const sortable = sortValue(value, field, repositoryPath)
  if (!sortable) return undefined
  if (sortable.kind !== 'number') invalidFrontmatter(field, repositoryPath, value)
  return sortable.value as number
}

function invalidFrontmatter(field: string, repositoryPath: string, value: unknown): never {
  throw new Error(
    `Invalid frontmatter ${field} in ${repositoryPath}: ${JSON.stringify(value)}`,
  )
}

function firstHeading(markdown: string): string {
  const match = markdown.match(/^#\s+(.+?)\s*$/m)
  return match?.[1].replace(/\s+\{#?[^}]+\}\s*$/, '').trim() ?? ''
}

function humanize(value: string): string {
  return value
    .replace(/^\d+[a-z]?[-_]?/i, '')
    .replace(/[-_]+/g, ' ')
    .replace(/\b[a-z]/g, (letter) => letter.toUpperCase())
    .trim() || value
}

function leadingNumber(value: string): number | undefined {
  const match = value.match(/^(\d+)/)
  return match ? Number(match[1]) : undefined
}

function toPosix(value: string): string {
  return value.replace(/\\/g, '/')
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}
