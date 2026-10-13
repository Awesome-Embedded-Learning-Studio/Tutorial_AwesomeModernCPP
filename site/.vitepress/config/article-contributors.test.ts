import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { mkdtempSync, mkdirSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import test from 'node:test'
import type { PageData } from 'vitepress'
import { applyArticleContributors, validateContributors } from './article-contributors'
import { getArticleHistory, parseArticleHistory } from './article-history'
import { DEFAULT_CONTRIBUTOR, formatArticleContributions } from '../theme/utils/article-contributors'

const page = (relativePath: string, frontmatter: Record<string, unknown>) => ({ relativePath, frontmatter } as PageData)

test('multiple roles produce one person, with localized roles and safe profile URLs', () => {
  const person = { github: 'xiaoshuaijie', roles: ['review', 'revision'] as const, note: { zh: '校订', en: 'Revision' } }
  const data = formatArticleContributions([{ ...person, roles: [...person.roles] }], [], 'en')
  assert.equal(data.contributors.length, 1)
  assert.deepEqual(data.contributors[0].roles, ['Revision', 'Review'])
  assert.equal(data.contributors[0].note, 'Revision')
  assert.equal(data.contributors[0].avatar, 'https://github.com/xiaoshuaijie.png?size=96')
})

test('defaults apply to articles, navigation and explicit opt-outs remain empty', () => {
  const article = page('missing-test-article.md', { chapter: 1 })
  applyArticleContributors(article)
  assert.equal(article.frontmatter.articleContributions.contributors[0].github, DEFAULT_CONTRIBUTOR.github)
  for (const fm of [{}, { chapter: 1, contributors: [] }, { chapter: 1, layout: 'home' }, { chapter: 1, weeklyIssue: {} }]) {
    const skipped = page('index.md', fm)
    applyArticleContributors(skipped)
    assert.equal(skipped.frontmatter.articleContributions, undefined)
  }
})

test('frontmatter rejects duplicate users, bad roles and partial translations', () => {
  assert.throws(() => validateContributors([DEFAULT_CONTRIBUTOR, { ...DEFAULT_CONTRIBUTOR, github: 'charliechen114514' }], 'test'), /duplicate/)
  assert.throws(() => validateContributors([{ github: 'xiaoshuaijie', roles: ['writer'] }], 'test'), /invalid roles/)
  assert.throws(() => validateContributors([{ github: 'xiaoshuaijie', roles: ['author'], note: { zh: '原稿' } }], 'test'), /invalid note/)
  assert.throws(() => validateContributors(null, 'test'), /array/)
})

test('Git authors and verified coauthors are retained, tool trailers do not become people', () => {
  const history = parseArticleHistory('\x1e' + 'a'.repeat(40) + '\x1f2026-09-21\x1fErol Tasci\x1ftascierol32@gmail.com\x1fExplain build tools (#258)\x1fCo-authored-by: Voyagerroc-Pro <330935573+Voyagerroc-Pro@users.noreply.github.com>\nCo-authored-by: Claude <noreply@anthropic.com>\n')
  assert.deepEqual(history[0].authors, ['Voyagerroc-Lab', 'voyagerroc-pro'])
  assert.equal(history[0].pr, 258)
  assert.equal(history[0].date, '2026-09-21')
})

test('history follows file renames and preserves real dates and authors', () => {
  const root = mkdtempSync(join(tmpdir(), 'article-history-'))
  mkdirSync(join(root, 'documents'))
  const git = (args: string[], date?: string) => execFileSync('git', args, {
    cwd: root, encoding: 'utf8', env: { ...process.env, ...(date ? { GIT_AUTHOR_DATE: date, GIT_COMMITTER_DATE: date } : {}) },
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  git(['init', '-q'])
  git(['config', 'user.name', 'Charliechen114514'])
  git(['config', 'user.email', '725610365@qq.com'])
  writeFileSync(join(root, 'documents', 'old.md'), '# Original\n')
  git(['add', 'documents/old.md'])
  git(['commit', '-qm', 'Original (#1)', '--author=jie <2413704818@qq.com>'], '2026-01-01T12:00:00+08:00')
  git(['mv', 'documents/old.md', 'documents/new.md'])
  git(['commit', '-qm', 'Rename article'], '2026-01-02T12:00:00+08:00')
  const history = getArticleHistory('new.md', root)
  assert.equal(history.length, 2)
  assert.equal(history[0].note, 'Rename article')
  assert.equal(history[1].date, '2026-01-01')
  assert.deepEqual(history[1].authors, ['xiaoshuaijie'])
  assert.throws(() => getArticleHistory('../escape.md', root), /invalid document path/)
})

test('English inherits original WSL credits, an explicit translator override wins', () => {
  const en = page('en/getting-started/07-wsl2-environment.md', { chapter: 14 })
  applyArticleContributors(en)
  assert.equal(en.frontmatter.articleContributions.heading, 'Contributors of This Article')
  assert.equal(en.frontmatter.articleContributions.contributors[0].github, 'xiaoshuaijie')
  assert.equal(en.frontmatter.articleContributions.contributors[0].note, 'Wrote and submitted the original article')
  const override = page(en.relativePath, { chapter: 14, contributors: [{ github: 'YukunJ', roles: ['translation'] }] })
  applyArticleContributors(override)
  assert.deepEqual(override.frontmatter.articleContributions.contributors[0].roles, ['Translation'])
})
