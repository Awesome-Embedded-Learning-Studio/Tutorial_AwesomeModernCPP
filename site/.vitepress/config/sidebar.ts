import type { DefaultTheme } from 'vitepress'
import { readdirSync, statSync, readFileSync, existsSync } from 'fs'
import { join, relative } from 'path'

type SidebarItem = DefaultTheme.SidebarItem

const DOCS_ROOT = join(import.meta.dirname, '../../../documents')

// 侧边栏标题经 v-html 渲染（见 VPSidebarItem.vue），裸的 < > 会被浏览器当成 HTML
// 标签吃掉（例如标题 `<numeric>：...` 的左侧会凭空消失）。先转义，v-html 再还原成字面量。
function escapeHtml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

function extractTitle(filePath: string): string | null {
  try {
    const content = readFileSync(filePath, 'utf-8')
    const fmMatch = content.match(/^---[\s\S]*?^title:\s*['"]?(.+?)['"]?\s*$/m)
    if (fmMatch) return escapeHtml(fmMatch[1])
    const h1 = content.match(/^#\s+(.+)$/m)
    if (h1) return escapeHtml(h1[1].replace(/\{.*?\}/g, '').trim())
  } catch { /* ignore */ }
  return null
}

function extractSidebarOrder(indexPath: string): number | undefined {
  try {
    const content = readFileSync(indexPath, 'utf-8')
    const m = content.match(/^sidebar_order:\s*(\d+)/m)
    return m ? parseInt(m[1], 10) : undefined
  } catch { /* ignore */ }
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

  // 子目录优先按其 index.md 的 sidebar_order 排序;文件按文件名开头数字;否则字母序
  const ordered = entries.map(name => {
    const full = join(dir, name)
    let order: number | undefined
    try {
      if (statSync(full).isDirectory()) {
        order = extractSidebarOrder(join(full, 'index.md'))
      } else if (/^\d+/.test(name)) {
        order = parseInt(name.match(/^(\d+)/)![1], 10)
      }
    } catch { /* ignore */ }
    return { name, order }
  })
  ordered.sort((a, b) => {
    if (a.order !== undefined && b.order !== undefined) return a.order - b.order
    if (a.order !== undefined) return -1
    if (b.order !== undefined) return 1
    return a.name.localeCompare(b.name, 'zh-CN')
  })
  entries = ordered.map(e => e.name)
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
    '/vol10-open-lecture-notes/': volumeSidebar('vol10-open-lecture-notes', '/vol10-open-lecture-notes'),
    '/compilation/': volumeSidebar('compilation', '/compilation'),
    '/cpp-reference/': volumeSidebar('cpp-reference', '/cpp-reference'),
    '/projects/': volumeSidebar('projects', '/projects'),
    '/community/': volumeSidebar('community', '/community'),
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
