import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import matter from 'gray-matter'
import type { PageData } from 'vitepress'
import { resolveArticleContributors } from './article-contributors'
import { getArticleHistory } from './article-history'
import { COMMUNITY_CATEGORIES, type CommunityArticle, type CommunityCategory } from '../theme/utils/community'

const PROJECT_ROOT = fileURLToPath(new URL('../../../', import.meta.url))

export function buildCommunityManifest(locale: 'zh' | 'en', root = PROJECT_ROOT): CommunityArticle[] {
  const prefix = locale === 'en' ? 'en/' : ''
  const articles: CommunityArticle[] = []
  for (const status of ['incoming', 'articles'] as const) {
    const directory = join(root, 'documents', prefix, 'community', status)
    if (!existsSync(directory)) continue
    function walk(dir: string, relativeDir: string) {
      for (const entry of readdirSync(dir, { withFileTypes: true })) {
        const path = join(dir, entry.name)
        const relativePath = `${relativeDir}/${entry.name}`
        if (entry.isDirectory()) { walk(path, relativePath); continue }
        if (!entry.name.endsWith('.md') || entry.name === 'index.md') continue
        const fm = matter(readFileSync(path, 'utf8')).data
        const people = resolveArticleContributors(fm, relativePath, root)
        const publishedAt = getArticleHistory(relativePath, root).at(-1)?.date
        let category = fm.community_category
        if (category === undefined && locale === 'en') {
          const source = join(root, 'documents', relativePath.slice(3))
          if (existsSync(source)) category = matter(readFileSync(source, 'utf8')).data.community_category
        }
        if (typeof category !== 'string' || !Object.hasOwn(COMMUNITY_CATEGORIES, category)) {
          throw new Error(`[community-manifest] ${relativePath}: invalid or missing community_category`)
        }
        if (typeof fm.title !== 'string' || !fm.title.trim()) {
          throw new Error(`[community-manifest] ${relativePath}: missing title`)
        }
        articles.push({
          url: `/${relativePath.slice(0, -3)}`, title: fm.title,
          description: typeof fm.description === 'string' ? fm.description : '',
          category: category as CommunityCategory, status,
          authors: people.filter(person => person.roles.includes('author'))
            .map(person => ({
              github: person.github, name: person.name ?? person.github,
              avatar: `https://github.com/${person.github}.png?size=96`,
            })),
          ...(publishedAt ? { publishedAt } : {}),
          readingTime: typeof fm.reading_time_minutes === 'number' && fm.reading_time_minutes > 0
            ? fm.reading_time_minutes : undefined,
        })
      }
    }
    walk(directory, `${prefix}community/${status}`)
  }
  return articles.sort((a, b) =>
    (b.publishedAt ?? '').localeCompare(a.publishedAt ?? '')
    || a.url.localeCompare(b.url, 'en', { numeric: true }),
  )
}

export function applyCommunityPageData(page: PageData, root = PROJECT_ROOT): void {
  if (!/^(?:en\/)?community\/(?:index|(?:incoming|articles|authors|topics)\/index)\.md$/.test(page.relativePath)) return
  page.frontmatter.communityArticles = buildCommunityManifest(page.relativePath.startsWith('en/') ? 'en' : 'zh', root)
}
