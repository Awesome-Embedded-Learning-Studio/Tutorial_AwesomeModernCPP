import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import matter from 'gray-matter'
import type { PageData } from 'vitepress'
import {
  CONTRIBUTION_ROLES, DEFAULT_CONTRIBUTOR, formatArticleContributions,
  type ContributionPerson,
} from '../theme/utils/article-contributors'
import { getArticleHistory } from './article-history'

const PROJECT_ROOT = fileURLToPath(new URL('../../../', import.meta.url))

export function validateContributors(value: unknown, where: string): asserts value is ContributionPerson[] {
  const fail = (message: string): never => { throw new Error(`[article-contributors] ${where}: ${message}`) }
  if (!Array.isArray(value)) fail('contributors must be an array (use [] to hide the signature)')
  const users = new Set<string>()
  for (const person of value as ContributionPerson[]) {
    if (!person || typeof person.github !== 'string' || !/^[a-z\d](?:[a-z\d-]{0,37}[a-z\d])?$/i.test(person.github)) fail('invalid GitHub username')
    const user = person.github.toLowerCase()
    if (users.has(user)) fail(`duplicate contributor ${person.github}; use roles instead`)
    users.add(user)
    if (!Array.isArray(person.roles) || !person.roles.length
      || person.roles.some(role => !CONTRIBUTION_ROLES.includes(role))
      || new Set(person.roles).size !== person.roles.length) fail(`invalid roles for ${person.github}`)
    if (person.note !== undefined && typeof person.note !== 'string'
      && (!person.note || typeof person.note !== 'object'
        || ['zh', 'en'].some(lang => typeof (person.note as Record<string, unknown>)[lang] !== 'string'))) fail(`invalid note for ${person.github}`)
    if (person.name !== undefined && (typeof person.name !== 'string' || !person.name.trim())) fail('invalid display name')
    if (person.pr !== undefined && (!Number.isInteger(person.pr) || person.pr <= 0)) fail('invalid PR number')
  }
}

/** EN inherits explicitly declared source credits unless its own page overrides them. */
function sourceContributors(relativePath: string): unknown {
  if (!relativePath.startsWith('en/')) return undefined
  try {
    return matter(readFileSync(join(PROJECT_ROOT, 'documents', relativePath.slice(3)), 'utf8')).data.contributors
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === 'ENOENT') return undefined
    throw error
  }
}

export function applyArticleContributors(page: PageData): void {
  const fm = page.frontmatter
  if (fm.weeklyIssue) return // WeeklyPageHeader already renders this issue's acknowledgements.
  // Navigation, home, team and weekly landing pages do not get default author cards.
  if (fm.contributors === undefined && typeof fm.chapter !== 'number') return
  if (fm.layout && fm.layout !== 'doc') return
  const people = fm.contributors !== undefined ? fm.contributors
    : sourceContributors(page.relativePath) ?? [DEFAULT_CONTRIBUTOR]
  validateContributors(people, page.relativePath)
  if (!people.length) return
  const locale = page.relativePath.startsWith('en/') ? 'en' : 'zh'
  fm.articleContributions = formatArticleContributions(people, getArticleHistory(page.relativePath), locale)
}
