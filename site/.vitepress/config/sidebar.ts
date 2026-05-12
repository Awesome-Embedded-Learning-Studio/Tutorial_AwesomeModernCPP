import type { DefaultTheme } from 'vitepress'
import { readdirSync, statSync, readFileSync, existsSync } from 'fs'
import { join, relative } from 'path'

type SidebarItem = DefaultTheme.SidebarItem

const DOCS_ROOT = join(import.meta.dirname, '../../../documents')

function extractTitle(filePath: string): string | null {
  try {
    const content = readFileSync(filePath, 'utf-8')
    const fmMatch = content.match(/^---[\s\S]*?^title:\s*['"]?(.+?)['"]?\s*$/m)
    if (fmMatch) return fmMatch[1]
    const h1 = content.match(/^#\s+(.+)$/m)
    if (h1) return h1[1].replace(/\{.*?\}/g, '').trim()
  } catch { /* ignore */ }
  return null
}

function humanize(name: string): string {
  return name
    .replace(/^\d+[-]?/, '')
    .replace(/[-_]/g, ' ')
    .replace(/\b\w/g, c => c.toUpperCase())
}

function sortEntries(a: string, b: string): number {
  const na = a.match(/^(\d+)/)?.[1]
  const nb = b.match(/^(\d+)/)?.[1]
  if (na && nb) return parseInt(na) - parseInt(nb)
  if (na) return -1
  if (nb) return 1
  return a.localeCompare(b, 'zh-CN')
}

function scanDir(dir: string, urlPrefix: string, depth = 0): SidebarItem[] {
  if (depth > 5) return []

  let entries: string[]
  try {
    entries = readdirSync(dir).filter(e =>
      !e.startsWith('.') &&
      e !== 'hooks' &&
      e !== 'stylesheets' &&
      e !== 'javascripts' &&
      e !== 'images'
    )
  } catch { return [] }

  entries.sort(sortEntries)
  const items: SidebarItem[] = []

  for (const name of entries) {
    const fullPath = join(dir, name)
    if (!statSync(fullPath).isDirectory() && !name.endsWith('.md')) continue

    if (statSync(fullPath).isDirectory()) {
      const subItems = scanDir(fullPath, `${urlPrefix}/${name}`, depth + 1)
      const indexPath = join(fullPath, 'index.md')
      const title = extractTitle(indexPath) || humanize(name)

      if (subItems.length > 0) {
        items.push({
          text: title,
          link: existsSync(indexPath) ? `${urlPrefix}/${name}/` : undefined,
          items: subItems,
          collapsed: depth > 0,
        })
      } else if (existsSync(indexPath)) {
        items.push({ text: title, link: `${urlPrefix}/${name}/` })
      }
    } else if (name !== 'index.md' && name !== 'tags.md') {
      const title = extractTitle(fullPath) || humanize(name.replace(/\.md$/, ''))
      items.push({ text: title, link: `${urlPrefix}/${name.replace(/\.md$/, '')}` })
    }
  }

  return items
}

export function volumeSidebar(relDir: string, urlPrefix: string): DefaultTheme.SidebarItem[] {
  const dir = join(DOCS_ROOT, relDir)
  const indexPath = join(dir, 'index.md')
  const items = scanDir(dir, urlPrefix)

  const overviewTitle = extractTitle(indexPath) || humanize(relDir)
  return [
    { text: overviewTitle, link: `${urlPrefix}/` },
    ...items,
  ]
}

// English sidebar — only includes files that exist under documents/en/
function enSidebar(): DefaultTheme.Sidebar {
  const enDir = join(DOCS_ROOT, 'en')
  if (!existsSync(enDir)) return {}

  const items = scanDir(enDir, '/en')
  if (items.length === 0) return {}
  return { '/en/': [{ text: 'English', items }] }
}

export function buildSidebar(): DefaultTheme.Sidebar {
  const sidebar: DefaultTheme.Sidebar = {
    '/vol1-fundamentals/': volumeSidebar('vol1-fundamentals', '/vol1-fundamentals'),
    '/vol2-modern-features/': volumeSidebar('vol2-modern-features', '/vol2-modern-features'),
    '/vol3-standard-library/': volumeSidebar('vol3-standard-library', '/vol3-standard-library'),
    '/vol4-advanced/': volumeSidebar('vol4-advanced', '/vol4-advanced'),
    '/vol5-concurrency/': volumeSidebar('vol5-concurrency', '/vol5-concurrency'),
    '/vol6-performance/': volumeSidebar('vol6-performance', '/vol6-performance'),
    '/vol7-engineering/': volumeSidebar('vol7-engineering', '/vol7-engineering'),
    '/vol8-domains/': volumeSidebar('vol8-domains', '/vol8-domains'),
    '/vol9-open-source-project-learn/': volumeSidebar('vol9-open-source-project-learn', '/vol9-open-source-project-learn'),
    '/compilation/': volumeSidebar('compilation', '/compilation'),
    '/cpp-reference/': volumeSidebar('cpp-reference', '/cpp-reference'),
    '/projects/': volumeSidebar('projects', '/projects'),
    '/appendix/': [
      { text: '附录', link: '/appendix/' },
      { text: '术语表', link: '/appendix/terminology' },
    ],
    '/team/': [
      { text: '贡献者', link: '/team/' },
    ],
  }

  return { ...sidebar, ...enSidebar() }
}
