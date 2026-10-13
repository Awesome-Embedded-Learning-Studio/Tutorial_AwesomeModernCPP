// One-time migration / audit. Default: report; --write: append credits to Markdown frontmatter.
// Human role declarations already present in an article always win over Git-derived revisions.
import { existsSync, readFileSync, readdirSync, writeFileSync } from 'node:fs'
import { join, relative } from 'node:path'
import { fileURLToPath } from 'node:url'
import matter from 'gray-matter'
import { getArticleHistory } from '../site/.vitepress/config/article-history'
import { DEFAULT_CONTRIBUTOR, type ContributionPerson } from '../site/.vitepress/theme/utils/article-contributors'

const root = fileURLToPath(new URL('../', import.meta.url))
const documents = join(root, 'documents')
const write = process.argv.includes('--write')
const verifiedUsers = new Set(['xiaoshuaijie', 'owollz4', 'yukunj', 'voyagerroc-lab', 'voyagerroc-pro'])
const canonical: Record<string, string> = { yukunj: 'YukunJ', 'voyagerroc-lab': 'Voyagerroc-Lab', 'voyagerroc-pro': 'Voyagerroc-Pro' }

function files(dir: string): string[] {
  return readdirSync(dir, { withFileTypes: true }).flatMap(entry => {
    if (entry.name.startsWith('.') || ['images', 'public'].includes(entry.name)) return []
    const path = join(dir, entry.name)
    return entry.isDirectory() ? files(path) : entry.name.endsWith('.md') ? [path] : []
  })
}

const changed: string[] = []
const byUser = new Map<string, number>()
for (const path of files(documents)) {
  const raw = readFileSync(path, 'utf8')
  const fm = matter(raw).data
  if (typeof fm.chapter !== 'number' || fm.contributors !== undefined) continue
  const rel = relative(documents, path).replaceAll('\\', '/')
  if (rel.startsWith('weekly-problems/')) continue // Has its own author acknowledgements.
  // EN inherits source declarations at build time. Do not repeat source credits in translations.
  if (rel.startsWith('en/')) {
    const original = join(documents, rel.slice(3))
    if (existsSync(original)) continue
  }
  const history = getArticleHistory(rel)
  const external = new Map<string, { github: string; subjects: string[]; prs: Set<number> }>()
  for (const entry of history) for (const author of entry.authors) {
    const key = author.toLowerCase()
    if (!verifiedUsers.has(key)) continue
    const item = external.get(key) ?? { github: canonical[key] ?? author, subjects: [], prs: new Set<number>() }
    item.subjects.push(entry.note)
    if (entry.pr) item.prs.add(entry.pr)
    external.set(key, item)
  }
  if (!external.size) continue
  const people: ContributionPerson[] = [DEFAULT_CONTRIBUTOR]
  for (const item of external.values()) {
    const exercises = item.subjects.some(subject => /答案|练习|exercise/i.test(subject))
    const prs = [...item.prs].sort((a, b) => a - b).map(pr => `#${pr}`).join(', ')
    const suffix = prs ? ` (PR ${prs})` : ''
    people.push({
      github: item.github, roles: ['revision'],
      note: {
        zh: (exercises ? '补充练习参考答案与修订正文' : '参与文章内容与示例修订') + suffix,
        en: (exercises ? 'Contributed exercise solutions and article revisions' : 'Contributed article and example revisions') + suffix,
      },
    })
    byUser.set(item.github, (byUser.get(item.github) ?? 0) + 1)
  }
  // Append without re-serializing the existing YAML, preserving author formatting and translation hashes.
  const frontmatter = raw.match(/^(---\r?\n)([\s\S]*?)(\r?\n---)(?:\r?\n|$)/)
  if (!frontmatter) throw new Error(`Missing frontmatter: ${rel}`)
  const eol = raw.includes('\r\n') ? '\r\n' : '\n'
  const block = ['contributors:', ...people.flatMap(person => [
    `  - github: ${person.github}`,
    `    roles: [${person.roles.join(', ')}]`,
    '    note:',
    `      zh: ${JSON.stringify(typeof person.note === 'object' ? person.note.zh : person.note)}`,
    `      en: ${JSON.stringify(typeof person.note === 'object' ? person.note.en : person.note)}`,
  ])].join(eol)
  const offset = frontmatter[1].length + frontmatter[2].length
  if (write) writeFileSync(path, raw.slice(0, offset) + eol + block + raw.slice(offset))
  changed.push(rel)
}
console.log(JSON.stringify({ mode: write ? 'write' : 'audit', articles: changed.length, byUser: Object.fromEntries(byUser), paths: changed }, null, 2))
