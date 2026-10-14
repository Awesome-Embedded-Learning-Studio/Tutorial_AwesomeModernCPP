import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import test from 'node:test'
import type { PageData } from 'vitepress'
import { applyCommunityPageData, buildCommunityManifest } from './community-manifest'
import { communityArticleStatus } from '../theme/utils/community'

test('directory handles nested articles, multiple authors, missing credits and reviewed moves', () => {
  const root = mkdtempSync(join(tmpdir(), 'community-manifest-'))
  try {
    const incoming = join(root, 'documents/community/incoming/series')
    const reviewed = join(root, 'documents/community/articles')
    mkdirSync(incoming, { recursive: true })
    mkdirSync(reviewed, { recursive: true })
    writeFileSync(join(incoming, 'index.md'), '# Navigation')
    writeFileSync(join(incoming, 'note.md'), '---\ntitle: Note\ndescription: Networking\ncommunity_category: learning-notes\ncontributors:\n  - github: Writer\n    name: 作者\n    roles: [author]\n  - github: Second\n    roles: [author]\n  - github: Editor\n    roles: [revision]\n---\n')
    writeFileSync(join(reviewed, 'experience.md'), '---\ntitle: Experience\ncommunity_category: engineering\n---\n')
    const articles = buildCommunityManifest('zh', root)
    assert.equal(articles.length, 2)
    const note = articles.find(article => article.title === 'Note')!
    assert.equal(note.url, '/community/incoming/series/note')
    assert.deepEqual(note.authors.map(person => person.github), ['Writer', 'Second'])
    assert.equal(note.authors[0].avatar, 'https://github.com/Writer.png?size=96')
    assert.equal(note.publishedAt, undefined)
    assert.deepEqual(articles.find(article => article.title === 'Experience')!.authors, [])
    const translated = join(root, 'documents/en/community/incoming/series')
    mkdirSync(translated, { recursive: true })
    writeFileSync(join(translated, 'note.md'), '---\ntitle: Translated note\n---\n')
    const english = buildCommunityManifest('en', root)
    assert.equal(english[0].category, 'learning-notes')
    assert.deepEqual(english[0].authors, note.authors)
    writeFileSync(join(reviewed, 'bad.md'), '---\ntitle: Bad\ncommunity_category: typo\n---\n')
    assert.throws(() => buildCommunityManifest('zh', root), /invalid or missing community_category/)
  } finally { rmSync(root, { recursive: true, force: true }) }
})

test('recent articles use their first traceable Git date and preserve natural path ordering for ties', () => {
  const root = mkdtempSync(join(tmpdir(), 'community-dates-'))
  try {
    const directory = join(root, 'documents/community/incoming')
    mkdirSync(directory, { recursive: true })
    const git = (args: string[], date?: string) => execFileSync('git', args, {
      cwd: root, stdio: ['ignore', 'pipe', 'pipe'],
      env: { ...process.env, ...(date ? { GIT_AUTHOR_DATE: date, GIT_COMMITTER_DATE: date } : {}) },
    })
    const commit = (date: string) => {
      git(['add', 'documents'])
      git(['-c', 'user.name=Writer', '-c', 'user.email=writer@example.test', 'commit', '-qm', 'Article'], date)
    }
    const article = (slug: string) => writeFileSync(join(directory, `${slug}.md`), `---\ntitle: ${slug}\ncommunity_category: engineering\n---\n${slug.repeat(50)}\n`)
    git(['init', '-q'])
    article('older')
    commit('2026-10-01T12:00:00+08:00')
    article('entry-10')
    article('entry-2')
    commit('2026-10-12T12:00:00+08:00')
    writeFileSync(join(directory, 'older.md'), '---\ntitle: Older, revised\ncommunity_category: engineering\n---\nRevised text.\n')
    commit('2026-10-13T12:00:00+08:00')
    article('unknown')

    const articles = buildCommunityManifest('zh', root)
    assert.deepEqual(articles.map(article => article.url.split('/').at(-1)), ['entry-2', 'entry-10', 'older', 'unknown'])
    assert.deepEqual(articles.map(article => article.publishedAt), ['2026-10-12', '2026-10-12', '2026-10-01', undefined])
  } finally { rmSync(root, { recursive: true, force: true }) }
})

test('article, author and topic landing pages receive localized catalogs without affecting other pages', () => {
  const root = mkdtempSync(join(tmpdir(), 'community-pages-'))
  try {
    for (const prefix of ['', 'en/']) {
      const directory = join(root, `documents/${prefix}community/incoming`)
      mkdirSync(directory, { recursive: true })
      writeFileSync(join(directory, 'note.md'), '---\ntitle: Note\ncommunity_category: learning-notes\n---\n')
      for (const slug of ['index', 'authors/index', 'topics/index', 'incoming/index', 'articles/index']) {
        const page = { relativePath: `${prefix}community/${slug}.md`, frontmatter: {} } as PageData
        applyCommunityPageData(page, root)
        assert.equal(page.frontmatter.communityArticles[0].url, `/${prefix}community/incoming/note`)
      }
    }
    for (const path of ['community/submit/index.md', 'community/incoming/note.md', 'vol1-fundamentals/index.md']) {
      const page = { relativePath: path, frontmatter: {} } as PageData
      applyCommunityPageData(page, root)
      assert.equal(page.frontmatter.communityArticles, undefined)
    }
  } finally { rmSync(root, { recursive: true, force: true }) }
})

test('community catalog preserves submitted Asio authors and inherited English credits', () => {
  const articles = buildCommunityManifest('zh')
  for (const slug of ['boost-asio-01-basic-client-server', 'boost-asio-02-sync-read-write', 'boost-asio-03-async-read-write']) {
    const article = articles.find(article => article.url === `/community/incoming/${slug}`)!
    assert.equal(article.authors[0].github, 'Karl-Heinrich-Marx-520')
    assert.equal(article.authors[0].name, '游翰灵')
  }
  const english = buildCommunityManifest('en')
  const packageArticle = english.find(article => article.url === '/en/community/incoming/why-cpp-package-manager-hard')!
  assert.equal(packageArticle.category, 'engineering')
  assert.equal(packageArticle.authors[0].github, 'Charliechen114514')
  assert.ok(english.every(article => article.url.startsWith('/en/community/')))
})

test('navigation, guides and other volumes do not use the community submission fallback', () => {
  assert.equal(communityArticleStatus('community/incoming/nested/index.md'), undefined)
  assert.equal(communityArticleStatus('community/submit/index.md'), undefined)
  assert.equal(communityArticleStatus('vol1-fundamentals/article.md'), undefined)
  assert.equal(communityArticleStatus('en/community/articles/nested/article.md'), 'articles')
})
