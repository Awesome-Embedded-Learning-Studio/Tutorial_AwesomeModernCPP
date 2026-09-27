import { statSync } from 'node:fs'
import { dirname, relative, resolve, sep } from 'node:path'
import type { CanonicalSourceLookup } from './catalog'
import { resolveCanonicalSource } from './catalog'
import type { BookLocale, SourceDocument } from './model'
import type { LinkResolution } from './transform'

const REPOSITORY_BLOB_ROOT = 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main'
const REPOSITORY_TREE_ROOT = 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main'

function explicitExternalLink(href: string, from: SourceDocument): string | undefined {
  if (href.startsWith('//')) {
    try {
      const url = new URL(`https:${href}`)
      if (!url.hostname) throw new Error('missing host')
      return url.href
    } catch {
      throw new Error(`${from.repositoryPath}: invalid protocol-relative link "${href}"`)
    }
  }

  const scheme = href.match(/^([a-z][a-z\d+.-]*):/i)?.[1].toLowerCase()
  if (!scheme) return undefined
  if (scheme === 'mailto' || scheme === 'tel') return href
  if (scheme === 'http' || scheme === 'https') {
    try {
      if (!/^https?:\/\//i.test(href)) throw new Error('missing authority delimiter')
      const url = new URL(href)
      if (!url.hostname) throw new Error('missing host')
      return href
    } catch {
      throw new Error(`${from.repositoryPath}: invalid external link "${href}"`)
    }
  }
  throw new Error(`${from.repositoryPath}: unsupported or unsafe link scheme in "${href}"`)
}

function splitFragment(href: string): { path: string; fragment: string } {
  const index = href.indexOf('#')
  return index === -1
    ? { path: href, fragment: '' }
    : { path: href.slice(0, index), fragment: href.slice(index + 1) }
}

function withFragment(url: string, fragment: string): string {
  return fragment ? `${url}#${fragment}` : url
}

interface RepositoryTarget {
  path: string
  kind: 'file' | 'directory'
}

function localRepositoryTarget(repositoryRoot: string, href: string, from: SourceDocument): RepositoryTarget | undefined {
  const path = splitFragment(href).path.split('?', 1)[0]
  if (!path || path.startsWith('/') || /^[a-z][a-z\d+.-]*:/i.test(path)) return undefined
  const decoded = decodeURIComponent(path)
  // VitePress rewrites Markdown links to .html during rendering. Repository
  // files outside documents/ are not in the page catalog, so recover their
  // original .md name before producing a GitHub source link.
  const htmlBase = decoded.endsWith('.html') ? decoded.slice(0, -5) : undefined
  const candidates = [
    decoded,
    ...(htmlBase ? [`${htmlBase}.md`, htmlBase] : []),
  ]
  for (const pathCandidate of candidates) {
    const candidate = resolve(dirname(from.sourcePath), pathCandidate)
    const rel = relative(repositoryRoot, candidate)
    if (rel === '..' || rel.startsWith(`..${sep}`)) continue
    try {
      const info = statSync(candidate)
      if (!info.isFile() && !info.isDirectory()) continue
      return {
        path: rel.replaceAll(sep, '/'),
        kind: info.isDirectory() ? 'directory' : 'file',
      }
    } catch {
      // Try the next spelling before classifying this as an unresolved link.
    }
  }
  return undefined
}

function encodeRepositoryPath(path: string): string {
  return path.split('/').map(encodeURIComponent).join('/')
}

export function createBookLinkResolver(options: {
  repositoryRoot: string
  locale: BookLocale
  bookDocuments: readonly SourceDocument[]
  repositoryLookup: CanonicalSourceLookup
}): (href: string, from: SourceDocument) => LinkResolution {
  const bookIds = new Set(options.bookDocuments.map(({ docId }) => docId))
  const onlineRoot = options.locale.onlinePrefix.replace(/\/$/, '')

  return (href, from) => {
    const trimmed = href.trim()
    const { fragment } = splitFragment(trimmed)
    const external = explicitExternalLink(trimmed, from)
    if (external !== undefined) return { kind: 'external', href: external }

    let target = resolveCanonicalSource(trimmed, from, options.repositoryLookup)
    // A small number of translated pages retain root-absolute links from the
    // Chinese source. Prefer the locale catalog when the exact route does not
    // exist; relative English links already resolve below /en naturally.
    if (!target && options.locale.sourcePrefix && trimmed.startsWith('/')) {
      const prefix = options.locale.sourcePrefix.replace(/^\/+|\/+$/g, '')
      const localized = `/${prefix}/${trimmed.replace(/^\/+/, '')}`
      target = resolveCanonicalSource(localized, from, options.repositoryLookup)
    }
    if (target) {
      if (bookIds.has(target.docId)) {
        return { kind: 'same-book', href: target.canonicalPath, target, fragment }
      }
      return {
        kind: 'cross-book',
        href: withFragment(`${onlineRoot}${target.canonicalPath}`, fragment),
        target,
        fragment,
      }
    }

    const repositoryTarget = localRepositoryTarget(options.repositoryRoot, trimmed, from)
    if (repositoryTarget) {
      const root = repositoryTarget.kind === 'directory' ? REPOSITORY_TREE_ROOT : REPOSITORY_BLOB_ROOT
      return {
        kind: 'external',
        href: withFragment(`${root}/${encodeRepositoryPath(repositoryTarget.path)}`, fragment),
      }
    }

    if (trimmed === '/' || trimmed === './' || trimmed === '../') {
      return { kind: 'external', href: onlineRoot }
    }

    throw new Error(`${from.repositoryPath}: unresolved internal or repository link "${href}"`)
  }
}
