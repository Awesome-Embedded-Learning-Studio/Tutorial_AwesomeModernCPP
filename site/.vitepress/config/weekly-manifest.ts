// 「每周一些题」manifest 生成器:扫描周页面 + 题目目录,产出周列表数据。
// 双链路共用:
//   - scripts/build.ts 构建时写进 dist(答题卡/首页挂件运行时 fetch)
//   - config/index.ts 的 dev 中间件按需现生成(dev 下改题即生效,无需重启)
// 出题人加目录即上站:documents/weekly-problems/week-XX.md 与
// code/volumn_codes/weekly-problems/week-XX/ 同名配对,题目按目录名排序。
//
// 附带构建期校验:每个 quiz.json 都过 parseQuizConfig,坏配置直接抛错、
// 构建失败——不带病上线。

import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import type { PageData } from 'vitepress'
import matter from 'gray-matter'
import type { WeeklyContributor } from '../theme/utils/weekly'

import { parseQuizConfig } from '../theme/utils/quiz-data'

export interface WeeklyManifestProblem {
  /** 相对仓库根的题目目录路径,即 QuizProblem 的 src */
  src: string
  title: string
  type: string
  stars: number
  /** solution/ 下每位投稿人的子文件夹名(一人一解,代码+说明) */
  solutions: string[]
}

export interface WeeklyManifestWeek {
  /** 周页面 slug(URL 为 /weekly-problems/<slug>) */
  slug: string
  title: string
  dateRange?: string
  description?: string
  weeklyThanks?: WeeklyContributor[]
  problems: WeeklyManifestProblem[]
}

export interface WeeklyManifest {
  generatedAt: string
  /** 最新在前 */
  weeks: WeeklyManifestWeek[]
}

/** manifest 在站点资源里的统一访问路径(fetchRepoText 可直接取) */
export const MANIFEST_REL = 'code/volumn_codes/weekly-problems/manifest.json'

function frontmatterField(raw: string, field: string): string | undefined {
  return raw.match(new RegExp(`^${field}:\\s*['"]?(.+?)['"]?\\s*$`, 'm'))?.[1]
}

export function generateWeeklyManifest(projectRoot: string): string {
  const docsDir = join(projectRoot, 'documents', 'weekly-problems')
  const codeRoot = join(projectRoot, 'code', 'volumn_codes', 'weekly-problems')

  const weeks: WeeklyManifestWeek[] = []
  if (existsSync(docsDir)) {
    const weekFiles = readdirSync(docsDir)
      .filter(f => /^week-\d+.*\.md$/.test(f))
      .sort()
      .reverse() // 最新在前

    for (const file of weekFiles) {
      const slug = file.replace(/\.md$/, '')
      const raw = readFileSync(join(docsDir, file), 'utf8')
      const thanks: unknown = matter(raw).data.weeklyThanks
      if (thanks !== undefined && (!Array.isArray(thanks) || thanks.some(person =>
        !person || typeof person.github !== 'string' || !/^[a-z\d](?:[a-z\d-]{0,37}[a-z\d])?$/i.test(person.github)
        || typeof person.role !== 'string' || !person.role.trim()))) {
        throw new Error(`${file}: weeklyThanks 应为名单,每项填写 github 用户名和 role 贡献说明`)
      }
      const week: WeeklyManifestWeek = {
        slug,
        title: frontmatterField(raw, 'title') ?? slug,
        dateRange: frontmatterField(raw, 'dateRange'),
        description: frontmatterField(raw, 'description'),
        weeklyThanks: thanks as WeeklyContributor[] | undefined,
        problems: [],
      }

      const weekCodeDir = join(codeRoot, slug)
      if (existsSync(weekCodeDir)) {
        for (const entry of readdirSync(weekCodeDir, { withFileTypes: true })) {
          if (!entry.isDirectory()) continue
          const quizPath = join(weekCodeDir, entry.name, 'quiz.json')
          if (!existsSync(quizPath)) continue
          // 坏配置在这里抛错 → 构建期爆出(开发期中间件同款逻辑)
          const cfg = parseQuizConfig(JSON.parse(readFileSync(quizPath, 'utf8')))
          // 题解:一人一子文件夹(solution/<投稿人>/),answer.md 必有、
          // solution.cpp 可选(口答/找 bug 类无参考代码)——缺件构建期爆出
          const solutions: string[] = []
          const solutionRoot = join(weekCodeDir, entry.name, 'solution')
          if (existsSync(solutionRoot)) {
            for (const person of readdirSync(solutionRoot, { withFileTypes: true })) {
              if (!person.isDirectory()) continue
              const hasNote = existsSync(join(solutionRoot, person.name, 'answer.md'))
              if (!hasNote) {
                throw new Error(`${slug}/${entry.name}/solution/${person.name}: 题解缺 answer.md(说明)`)
              }
              solutions.push(person.name)
            }
            solutions.sort()
          }
          week.problems.push({
            src: `code/volumn_codes/weekly-problems/${slug}/${entry.name}`,
            title: cfg.title,
            type: cfg.type,
            stars: cfg.stars,
            solutions,
          })
        }
        week.problems.sort((a, b) => a.src.localeCompare(b.src))
      }
      weeks.push(week)
    }
  }

  const manifest: WeeklyManifest = { generatedAt: new Date().toISOString(), weeks }
  return JSON.stringify(manifest, null, 2)
}

/** 同时供 dev 和分卷 SSG 使用:作者无需额外的页面组件或元数据。 */
export function applyWeeklyPageData(page: PageData, projectRoot: string): void {
  const match = page.relativePath.match(/^weekly-problems\/(index|week-[^/]+)\.md$/)
  if (!match) return
  const manifest = JSON.parse(generateWeeklyManifest(projectRoot)) as WeeklyManifest
  page.frontmatter.sidebar = false
  page.frontmatter.aside = false
  if (match[1] === 'index') page.frontmatter.weeklyArchive = manifest.weeks
  else page.frontmatter.weeklyIssue = manifest.weeks.find(week => week.slug === match[1])
}
