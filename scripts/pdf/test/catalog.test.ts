import assert from 'node:assert/strict'
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import test from 'node:test'

import {
  buildCanonicalSourceLookup,
  discoverBookDocuments,
  discoverRepositoryDocuments,
  resolveCanonicalCandidates,
  resolveCanonicalSource,
} from '../catalog'
import type {
  BookDefinition,
  BookLocale,
  ContentUnit,
  SourceDocument,
} from '../model'

const zhLocale: BookLocale = {
  language: 'zh',
  htmlLang: 'zh-CN',
  sourcePrefix: '',
  onlinePrefix: 'https://example.test/tutorial',
  strings: {
    contents: '目录',
    generated: '生成日期',
    sourceRevision: '源码版本',
    onlineEdition: '在线版',
    references: '参考资料',
    lectureResources: '讲座资料',
    sourceCode: '示例源码',
    armSourceCode: 'ARM 示例源码',
  },
}

const enLocale: BookLocale = {
  ...zhLocale,
  language: 'en',
  htmlLang: 'en',
  sourcePrefix: 'en',
}

const alphaUnit: ContentUnit = {
  id: 'alpha',
  sourceDir: 'alpha-source',
  urlPrefix: '/alpha',
}

const betaUnit: ContentUnit = {
  id: 'beta',
  sourceDir: 'beta-source',
  urlPrefix: '/beta',
}

const book: BookDefinition = {
  id: 'fixture-book',
  title: { zh: '测试书', en: 'Fixture Book' },
  label: { zh: '测试', en: 'Fixture' },
  units: ['alpha', 'beta'],
}

test('discovers Markdown recursively and produces deterministic book order', (context) => {
  const repositoryRoot = temporaryRepository(context)

  fixture(repositoryRoot, 'documents/alpha-source/index.md', `---
title: Alpha overview
description: Root description
---
# ignored fallback
`)
  fixture(repositoryRoot, 'documents/alpha-source/README.md', '# excluded')
  fixture(repositoryRoot, 'documents/alpha-source/tags.md', '# excluded')
  fixture(repositoryRoot, 'documents/alpha-source/20-root.md', `---
title: Root twenty
chapter: 0
order: 20
---
body
`)
  fixture(repositoryRoot, 'documents/alpha-source/02-root.md', `---
chapter: 0
order: 2
---
# Root two
`)
  fixture(repositoryRoot, 'documents/alpha-source/late/index.md', `---
title: Late chapter
sidebar_order: 20
---
`)
  fixture(repositoryRoot, 'documents/alpha-source/early/index.md', `---
title: Early chapter
sidebar_order: 10
---
`)
  fixture(repositoryRoot, 'documents/alpha-source/early/10-topic.md', '# Topic ten')
  fixture(repositoryRoot, 'documents/alpha-source/early/2-topic.md', '# Topic two')
  fixture(repositoryRoot, 'documents/beta-source/index.md', '# Beta overview')

  const documents = discoverBookDocuments({
    repositoryRoot,
    book,
    locale: zhLocale,
    units: [alphaUnit, betaUnit],
  })

  assert.deepEqual(
    documents.map(({ repositoryPath }) => repositoryPath),
    [
      'documents/alpha-source/index.md',
      'documents/alpha-source/02-root.md',
      'documents/alpha-source/20-root.md',
      'documents/alpha-source/early/index.md',
      'documents/alpha-source/early/2-topic.md',
      'documents/alpha-source/early/10-topic.md',
      'documents/alpha-source/late/index.md',
      'documents/beta-source/index.md',
    ],
  )
  assert.equal(documents[0].kind, 'chapter-index')
  assert.equal(documents.at(-1)?.kind, 'chapter-index')
  assert.equal(documents[3].kind, 'chapter-index')
  assert.equal(documents[4].kind, 'article')
  assert.equal(documents[1].title, 'Root two')
  assert.equal(documents[1].chapter, '0')
  assert.equal(documents[1].order, 2)
  assert.equal(documents[0].description, 'Root description')
  assert.equal(documents[0].canonicalPath, '/alpha/')
  assert.equal(documents[3].canonicalPath, '/alpha/early/')
  assert.equal(documents[4].canonicalPath, '/alpha/early/2-topic')
  assert.match(documents[4].docId, /^doc-zh-alpha-/)
  assert.doesNotMatch(documents[0].markdown, /^---/)
})

test('builds a locale-aware full repository catalog', (context) => {
  const repositoryRoot = temporaryRepository(context)
  fixture(repositoryRoot, 'documents/alpha-source/index.md', '# 中文')
  fixture(repositoryRoot, 'documents/en/alpha-source/index.md', '# English')

  const documents = discoverRepositoryDocuments({
    repositoryRoot,
    locales: [zhLocale, enLocale],
    units: [alphaUnit],
  })
  const lookup = buildCanonicalSourceLookup(documents)

  assert.deepEqual(
    documents.map(({ canonicalPath }) => canonicalPath),
    ['/alpha/', '/en/alpha/'],
  )
  assert.equal(new Set(documents.map(({ docId }) => docId)).size, 2)
  assert.equal(lookup.byCanonicalPath.get('/en/alpha/')?.title, 'English')
})

test('returns canonical link aliases in priority order and resolves them', (context) => {
  const repositoryRoot = temporaryRepository(context)
  fixture(repositoryRoot, 'documents/alpha-source/index.md', '# Alpha')
  fixture(repositoryRoot, 'documents/alpha-source/early/index.md', '# Early')
  fixture(repositoryRoot, 'documents/alpha-source/early/2-topic.md', '# Two')
  fixture(repositoryRoot, 'documents/alpha-source/early/10-topic.md', '# Ten')
  fixture(repositoryRoot, 'documents/alpha-source/late/index.md', '# Late')
  fixture(repositoryRoot, 'documents/beta-source/index.md', '# Beta')

  const documents = discoverBookDocuments({
    repositoryRoot,
    book,
    locale: zhLocale,
    units: [alphaUnit, betaUnit],
  })
  const lookup = buildCanonicalSourceLookup(documents)
  const from = requiredDocument(documents, '/alpha/early/2-topic')

  assert.deepEqual(
    resolveCanonicalCandidates('../late/index.md#diagram', from).slice(0, 2),
    ['/alpha/late/', '/alpha/late'],
  )
  assert.equal(
    resolveCanonicalCandidates(
      '/Tutorial_AwesomeModernCPP/alpha/early/10-topic.html?print=1',
      from,
    )[0],
    '/alpha/early/10-topic',
  )
  assert.equal(resolveCanonicalSource('../late/', from, lookup)?.title, 'Late')
  assert.equal(resolveCanonicalSource('../../beta/index.md', from, lookup)?.title, 'Beta')
  assert.equal(resolveCanonicalSource('#local-heading', from, lookup), from)
  assert.deepEqual(resolveCanonicalCandidates('https://example.com/alpha/', from), [])
})

test('fails fast for duplicate canonical paths and document ids', (context) => {
  const repositoryRoot = temporaryRepository(context)
  const duplicateRoute: ContentUnit = {
    id: 'duplicate-route',
    sourceDir: 'duplicate-source',
    urlPrefix: '/alpha',
  }
  fixture(repositoryRoot, 'documents/alpha-source/index.md', '# Alpha')
  fixture(repositoryRoot, 'documents/duplicate-source/index.md', '# Duplicate')

  assert.throws(
    () => discoverBookDocuments({
      repositoryRoot,
      book: { ...book, units: ['alpha', 'duplicate-route'] },
      locale: zhLocale,
      units: [alphaUnit, duplicateRoute],
    }),
    /Duplicate canonical path/,
  )

  const [document] = discoverBookDocuments({
    repositoryRoot,
    book: { ...book, units: ['alpha'] },
    locale: zhLocale,
    units: [alphaUnit],
  })
  assert.equal(document.kind, 'book-index')
  const sameId: SourceDocument = {
    ...document,
    repositoryPath: 'documents/elsewhere.md',
    canonicalPath: '/elsewhere',
  }
  assert.throws(
    () => buildCanonicalSourceLookup([document, sameId]),
    /Duplicate document id/,
  )
})

test('fails fast for invalid ordering frontmatter', (context) => {
  const repositoryRoot = temporaryRepository(context)
  fixture(repositoryRoot, 'documents/alpha-source/index.md', '# Alpha')
  fixture(repositoryRoot, 'documents/alpha-source/topic.md', `---
order: eventually
---
# Topic
`)

  assert.throws(
    () => discoverBookDocuments({
      repositoryRoot,
      book: { ...book, units: ['alpha'] },
      locale: zhLocale,
      units: [alphaUnit],
    }),
    /Invalid frontmatter order.*documents\/alpha-source\/topic\.md/,
  )
})

function temporaryRepository(context: test.TestContext): string {
  const repositoryRoot = mkdtempSync(join(tmpdir(), 'pdf-catalog-'))
  context.after(() => rmSync(repositoryRoot, { recursive: true, force: true }))
  return repositoryRoot
}

function fixture(repositoryRoot: string, relativePath: string, content: string): void {
  const path = join(repositoryRoot, relativePath)
  mkdirSync(dirname(path), { recursive: true })
  writeFileSync(path, content, 'utf8')
}

function requiredDocument(
  documents: SourceDocument[],
  canonicalPath: string,
): SourceDocument {
  const document = documents.find((candidate) => candidate.canonicalPath === canonicalPath)
  assert.ok(document, `Missing fixture document ${canonicalPath}`)
  return document
}
