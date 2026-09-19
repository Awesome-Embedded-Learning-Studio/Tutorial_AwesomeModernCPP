// 标签索引页数据生成器:扫描 documents 树的 frontmatter,产出「标签 → 文章」索引。
// 数据源唯一——文章 frontmatter 的 tags 字段(白名单与分类在 scripts/tags.json)。
// 作者给文章加标签即上站,无需任何手工同步。
//
// 消费链路(与 weekly-manifest 的 transformPageData 路线同构):
//   - dev:config/index.ts 的 transformPageData 调 applyTagsPageData 注入
//   - build:scripts/build.ts 的 generateRootConfig 生成的 root 配置同样调用
// dev 下改了文章标签后需重启 dev server 才刷新(与 sidebar 行为一致);
// 扫描结果按进程 memoize,transformPageData 每页触发也不会重复扫全树。
//
// 附带构建期体检:白名单里从未被用到的主题标签会打 warn,提醒清理死标签。

import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs'
import { basename, join, relative, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import matter from 'gray-matter'
import type { PageData } from 'vitepress'

const PROJECT_ROOT = fileURLToPath(new URL('../../../', import.meta.url))
const DOCUMENTS = join(PROJECT_ROOT, 'documents')

// ── 标签分类(scripts/tags.json,与 Python 校验器共用) ────────

interface TaxonomyCategory {
  key: string
  zh: string
  en: string
  role: 'topic' | 'platform' | 'audience'
  tags: string[]
}

const TAXONOMY: { categories: TaxonomyCategory[] } = JSON.parse(
  readFileSync(join(PROJECT_ROOT, 'scripts', 'tags.json'), 'utf8'),
)
const TOPIC_CATEGORIES = TAXONOMY.categories.filter(c => c.role === 'topic')
const PLATFORM_TAGS = new Set(
  TAXONOMY.categories.filter(c => c.role === 'platform').flatMap(c => c.tags),
)
const AUDIENCE_TAGS = new Set(
  TAXONOMY.categories.filter(c => c.role === 'audience').flatMap(c => c.tags),
)
const DIFFICULTY_TAGS = ['beginner', 'intermediate', 'advanced']

// ── 卷名映射(顺序即结果列表的排序优先级,与 build.ts VOLUMES 对齐) ──

const VOLUME_ORDER = [
  'getting-started',
  'vol1-fundamentals', 'vol2-modern-features', 'vol3-standard-library',
  'vol4-advanced', 'vol5-concurrency', 'vol6-performance',
  'vol7-engineering', 'vol8-domains', 'vol9-open-source-project-learn',
  'vol10-open-lecture-notes', 'compilation', 'crash-lab',
  'weekly-problems', 'cpp-reference', 'projects', 'community',
  'roadmap', 'appendix', 'team',
]

const VOLUME_LABELS: Record<'zh' | 'en', Record<string, string>> = {
  zh: {
    'getting-started': '新手起步',
    'vol1-fundamentals': '卷一·基础入门',
    'vol2-modern-features': '卷二·现代特性',
    'vol3-standard-library': '卷三·标准库深入',
    'vol4-advanced': '卷四·高级主题',
    'vol5-concurrency': '卷五·并发编程',
    'vol6-performance': '卷六·性能优化',
    'vol7-engineering': '卷七·工程实践',
    'vol8-domains': '卷八·领域应用',
    'vol9-open-source-project-learn': '卷九·开源项目学习',
    'vol10-open-lecture-notes': '卷十·课程与演讲笔记',
    'compilation': '编译与链接',
    'crash-lab': '崩溃实验室',
    'weekly-problems': '每周一些题',
    'cpp-reference': 'C++ 速查',
    'projects': '实战项目',
    'community': '社区文章',
    'roadmap': '路线图',
    'appendix': '附录',
    'team': '贡献者',
  },
  en: {
    'getting-started': 'Getting Started',
    'vol1-fundamentals': 'Vol.1 Fundamentals',
    'vol2-modern-features': 'Vol.2 Modern Features',
    'vol3-standard-library': 'Vol.3 Standard Library',
    'vol4-advanced': 'Vol.4 Advanced Topics',
    'vol5-concurrency': 'Vol.5 Concurrency',
    'vol6-performance': 'Vol.6 Performance',
    'vol7-engineering': 'Vol.7 Engineering',
    'vol8-domains': 'Vol.8 Domain Applications',
    'vol9-open-source-project-learn': 'Vol.9 Open Source Projects',
    'vol10-open-lecture-notes': 'Vol.10 Courses & Talks',
    'compilation': 'Compilation & Linking',
    'crash-lab': 'Crash Lab',
    'weekly-problems': 'Weekly Problems',
    'cpp-reference': 'C++ Reference',
    'projects': 'Projects',
    'community': 'Community',
    'roadmap': 'Roadmap',
    'appendix': 'Appendix',
    'team': 'Team',
  },
}

// ── 数据形状(TagExplorer.vue 消费) ─────────────────────────

export interface TagArticle {
  /** 标题 */
  t: string
  /** 站内链接(clean URL,不含 base,组件侧 withBase) */
  h: string
  /** 所属顶级目录 key(标签查 VOLUME_LABELS) */
  v: string
  /** beginner | intermediate | advanced(缺省则无难度筛选) */
  d?: string
  /** platform: host | stm32f1 | stm32f4 */
  p?: string
  /** 预估阅读分钟 */
  m?: number
  /** 主题标签(平台/受众标签已剔除) */
  tg: string[]
}

export interface TagsIndex {
  locale: 'zh' | 'en'
  /** 标签墙上展示的主题标签总数(仅 count>0) */
  tagCount: number
  articleCount: number
  volLabels: Record<string, string>
  categories: Array<{ key: string; label: string; labelEn: string; tags: Array<{ name: string; count: number }> }>
  articles: TagArticle[]
}

// ── 扫描 ────────────────────────────────────────────────────

/** 不算「文章」的文件:本页、404、README(index.md 单独判定,见 isArticleIndex) */
const SKIP_FILE_NAMES = new Set(['tags.md', '404.md', 'README.md'])
/** 不进扫描的目录:资源与公共目录 */
const SKIP_DIR_NAMES = ['images', 'public', 'stylesheets', 'javascripts', 'hooks']

function walkMd(dir: string, skipTopEn: boolean, out: string[]) {
  let entries
  try {
    entries = readdirSync(dir, { withFileTypes: true })
  } catch {
    return
  }
  for (const e of entries) {
    if (e.name.startsWith('.')) continue
    const full = join(dir, e.name)
    if (e.isDirectory()) {
      if (SKIP_DIR_NAMES.includes(e.name)) continue
      // zh 扫描跳过 en 子树(en 有自己的扫描)
      if (skipTopEn && e.name === 'en') continue
      walkMd(full, skipTopEn, out)
    } else if (e.name.endsWith('.md') && !SKIP_FILE_NAMES.has(e.name)) {
      out.push(full)
    }
  }
}

/**
 * index.md 多数是导航页,但也有「一站一文」的例外(如 F103 教程线每个站唯一
 * 页面就是 index.md,带完整文章级 frontmatter)。判别:带数字 chapter 的算文章,
 * 导航页(vol 根/roadmap/community 等)没有 chapter/order。
 */
function isArticleIndex(fm: Record<string, unknown>): boolean {
  return typeof fm.chapter === 'number'
}

function toArticle(absPath: string, docRoot: string, locale: 'zh' | 'en'): TagArticle | null {
  // frontmatter 永远在文件头,截 16KB 足够,免去整读长文
  const raw = readFileSync(absPath, 'utf8').slice(0, 16384)
  let fm: Record<string, unknown>
  try {
    fm = matter(raw).data as Record<string, unknown>
  } catch {
    return null
  }
  const title = typeof fm.title === 'string' ? fm.title.trim() : ''
  if (!title) return null
  // 纯导航 index.md 不进文章列表
  if (basename(absPath) === 'index.md' && !isArticleIndex(fm)) return null

  const relWithExt = relative(docRoot, absPath).split(sep).join('/')
  // 目录 index 页的 URL 是目录本身(尾斜杠),普通页面去掉 .md
  const isDirIndex = relWithExt === 'index.md' || relWithExt.endsWith('/index.md')
  const rel = isDirIndex ? relWithExt.replace(/(^|\/)index\.md$/, '$1') : relWithExt.replace(/\.md$/, '')
  const href = locale === 'en' ? `/en/${rel}` : `/${rel}`
  const volKey = rel.split('/')[0]

  const tags = Array.isArray(fm.tags) ? fm.tags.filter((t): t is string => typeof t === 'string') : []
  const topicTags = tags.filter(t => !PLATFORM_TAGS.has(t) && !AUDIENCE_TAGS.has(t))

  const difficulty
    = typeof fm.difficulty === 'string' && DIFFICULTY_TAGS.includes(fm.difficulty)
      ? fm.difficulty
      : tags.find(t => DIFFICULTY_TAGS.includes(t))
  const platform
    = typeof fm.platform === 'string' && PLATFORM_TAGS.has(fm.platform)
      ? fm.platform
      : tags.find(t => PLATFORM_TAGS.has(t))

  const minutes = typeof fm.reading_time_minutes === 'number' ? fm.reading_time_minutes : undefined

  return {
    t: title,
    h: href,
    v: volKey,
    d: difficulty,
    p: platform,
    m: minutes,
    tg: topicTags,
  }
}

const cache = new Map<'zh' | 'en', TagsIndex>()

export function getTagsIndex(locale: 'zh' | 'en'): TagsIndex {
  const hit = cache.get(locale)
  if (hit) return hit

  const docRoot = locale === 'en' ? join(DOCUMENTS, 'en') : DOCUMENTS
  const files: string[] = []
  if (existsSync(docRoot)) walkMd(docRoot, locale === 'zh', files)

  const volOrder = new Map(VOLUME_ORDER.map((k, i) => [k, i]))
  const articles = files
    .map(f => toArticle(f, docRoot, locale))
    .filter((a): a is TagArticle => a !== null)
    .sort((a, b) => {
      const va = volOrder.get(a.v) ?? VOLUME_ORDER.length
      const vb = volOrder.get(b.v) ?? VOLUME_ORDER.length
      return va !== vb ? va - vb : a.h.localeCompare(b.h, locale === 'zh' ? 'zh-CN' : 'en')
    })

  // 标签计数(仅主题标签;墙只渲染 count>0 的)
  const counts = new Map<string, number>()
  for (const a of articles) for (const t of a.tg) counts.set(t, (counts.get(t) ?? 0) + 1)

  const categories = TOPIC_CATEGORIES.map(cat => ({
    key: cat.key,
    label: locale === 'zh' ? cat.zh : cat.en,
    labelEn: cat.en,
    tags: cat.tags
      .map(name => ({ name, count: counts.get(name) ?? 0 }))
      .filter(t => t.count > 0)
      .sort((a, b) => b.count - a.count || a.name.localeCompare(b.name, 'zh-CN')),
  })).filter(cat => cat.tags.length > 0)

  const tagCount = categories.reduce((n, c) => n + c.tags.length, 0)

  // 体检:白名单里从未用到的主题标签——多半是改名残留,提醒清理
  const used = new Set(counts.keys())
  const dead = TOPIC_CATEGORIES.flatMap(c => c.tags).filter(t => !used.has(t))
  if (dead.length > 0) {
    console.warn(`[tags-manifest] 白名单主题标签从未被使用(${dead.length}): ${dead.join('、')} —— 考虑从 scripts/tags.json 清理`)
  }
  const untagged = articles.filter(a => a.tg.length === 0).length
  if (untagged > 0) {
    console.warn(`[tags-manifest] ${locale} 有 ${untagged} 篇文章没有主题标签,不出现在任何标签过滤结果里`)
  }

  const index: TagsIndex = {
    locale,
    tagCount,
    articleCount: articles.length,
    volLabels: VOLUME_LABELS[locale],
    categories,
    articles,
  }
  cache.set(locale, index)
  return index
}

/** 同时供 dev 主配置与 build.ts 的 root 配置使用:给 tags 页注入索引数据,
 *  给文章页注入算好的主题标签(文章页底部徽章用)。 */
export function applyTagsPageData(page: PageData): void {
  const isZh = page.relativePath === 'tags.md'
  const isEn = page.relativePath === 'en/tags.md'
  if (isZh || isEn) {
    page.frontmatter.sidebar = false
    page.frontmatter.aside = false
    page.frontmatter.tagsIndex = getTagsIndex(isEn ? 'en' : 'zh')
    return
  }
  // 文章页:客户端组件拿不到 tags.json(node 侧数据),构建期把「过滤掉
  // 平台/受众全员标签后的主题标签」注入 frontmatter,单一数据源不破。
  // tagsPageBase 一并注入:分卷 EN 构建的 localeIndex 是 'root',客户端
  // 判不了语言,目标索引页路径由构建期按 relativePath 的 en/ 前缀定死。
  // 没有主题标签的页面不注入,组件据此什么都不渲染。
  const tags = page.frontmatter.tags
  if (Array.isArray(tags)) {
    const topic = tags.filter(t => typeof t === 'string' && !PLATFORM_TAGS.has(t) && !AUDIENCE_TAGS.has(t))
    if (topic.length > 0) {
      page.frontmatter.topicTags = topic
      page.frontmatter.tagsPageBase = page.relativePath.startsWith('en/') ? '/en/tags' : '/tags'
    }
  }
}
