import assert from 'node:assert/strict'
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import test from 'node:test'

import { buildCanonicalSourceLookup } from '../catalog'
import { createBookLinkResolver } from '../links'
import type {
  BookLocale,
  ContentUnit,
  SourceDocument,
} from '../model'

const repositoryWebRoot = 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP'

const locale: BookLocale = {
  language: 'zh',
  htmlLang: 'zh-CN',
  sourcePrefix: '',
  onlinePrefix: 'https://docs.example.test/Tutorial_AwesomeModernCPP',
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

const alphaUnit: ContentUnit = {
  id: 'alpha',
  sourceDir: 'alpha',
  urlPrefix: '/alpha',
}

const betaUnit: ContentUnit = {
  id: 'beta',
  sourceDir: 'beta',
  urlPrefix: '/beta',
}

test('classifies same-book, cross-book, and external links', (context) => {
  const repositoryRoot = temporaryRepository(context)
  const from = document(repositoryRoot, alphaUnit, 'ch/from.md', '/alpha/ch/from')
  const sameBook = document(repositoryRoot, alphaUnit, 'ch/target.md', '/alpha/ch/target')
  const crossBook = document(repositoryRoot, betaUnit, 'topic.md', '/beta/topic')
  const resolveLink = createBookLinkResolver({
    repositoryRoot,
    locale,
    bookDocuments: [from, sameBook],
    repositoryLookup: buildCanonicalSourceLookup([from, sameBook, crossBook]),
  })

  assert.deepEqual(resolveLink('./target.md#details', from), {
    kind: 'same-book',
    href: '/alpha/ch/target',
    target: sameBook,
    fragment: 'details',
  })
  assert.deepEqual(resolveLink('../../beta/topic.html#overview', from), {
    kind: 'cross-book',
    href: `${locale.onlinePrefix}/beta/topic#overview`,
    target: crossBook,
    fragment: 'overview',
  })
  assert.deepEqual(resolveLink('https://cppreference.com/w/cpp', from), {
    kind: 'external',
    href: 'https://cppreference.com/w/cpp',
  })
  assert.deepEqual(resolveLink('mailto:reader@example.com', from), {
    kind: 'external',
    href: 'mailto:reader@example.com',
  })
  assert.deepEqual(resolveLink('//isocpp.org/resources', from), {
    kind: 'external',
    href: 'https://isocpp.org/resources',
  })
})

test('falls back to the active locale for untranslated root-absolute links', (context) => {
  const repositoryRoot = temporaryRepository(context)
  const englishLocale: BookLocale = { ...locale, language: 'en', sourcePrefix: 'en' }
  const from = document(repositoryRoot, alphaUnit, 'index.md', '/en/alpha/')
  const target = document(repositoryRoot, betaUnit, 'topic.md', '/en/beta/topic')
  const resolveLink = createBookLinkResolver({
    repositoryRoot,
    locale: englishLocale,
    bookDocuments: [from],
    repositoryLookup: buildCanonicalSourceLookup([from, target]),
  })

  assert.deepEqual(resolveLink('/beta/topic#details', from), {
    kind: 'cross-book',
    href: `${locale.onlinePrefix}/en/beta/topic#details`,
    target,
    fragment: 'details',
  })
})

test('maps repository directories and VitePress HTML rewrites to GitHub', (context) => {
  const repositoryRoot = temporaryRepository(context)
  const from = document(repositoryRoot, alphaUnit, 'ch/from.md', '/alpha/ch/from')
  fixture(repositoryRoot, 'code/demo/README.md', '# Demo')
  fixture(repositoryRoot, 'code/demo/foo.cpp', 'int main() {}')
  const resolveLink = createBookLinkResolver({
    repositoryRoot,
    locale,
    bookDocuments: [from],
    repositoryLookup: buildCanonicalSourceLookup([from]),
  })

  assert.deepEqual(resolveLink('../../../code/demo/#build', from), {
    kind: 'external',
    href: `${repositoryWebRoot}/tree/main/code/demo#build`,
  })
  assert.deepEqual(resolveLink('../../../code/demo/README.html#usage', from), {
    kind: 'external',
    href: `${repositoryWebRoot}/blob/main/code/demo/README.md#usage`,
  })
  assert.deepEqual(resolveLink('../../../code/demo/foo.cpp.html', from), {
    kind: 'external',
    href: `${repositoryWebRoot}/blob/main/code/demo/foo.cpp`,
  })
})

test('fails fast for every unresolved local-looking link', (context) => {
  const repositoryRoot = temporaryRepository(context)
  const from = document(repositoryRoot, alphaUnit, 'ch/from.md', '/alpha/ch/from')
  const resolveLink = createBookLinkResolver({
    repositoryRoot,
    locale,
    bookDocuments: [from],
    repositoryLookup: buildCanonicalSourceLookup([from]),
  })

  for (const href of [
    './missing-page',
    './missing-page.md',
    './missing-page.html',
    './missing-archive.zip',
    '/missing-image.svg',
  ]) {
    assert.throws(
      () => resolveLink(href, from),
      new RegExp(`documents/alpha/ch/from\\.md: unresolved internal or repository link "${escapeRegExp(href)}"`),
    )
  }
})

test('rejects unsupported, unsafe, and malformed external schemes', (context) => {
  const repositoryRoot = temporaryRepository(context)
  const from = document(repositoryRoot, alphaUnit, 'ch/from.md', '/alpha/ch/from')
  const resolveLink = createBookLinkResolver({
    repositoryRoot,
    locale,
    bookDocuments: [from],
    repositoryLookup: buildCanonicalSourceLookup([from]),
  })

  for (const href of ['javascript:alert(1)', 'data:text/html,unsafe', 'file:///etc/passwd', 'ftp://example.test/file']) {
    assert.throws(() => resolveLink(href, from), /unsupported or unsafe link scheme/)
  }
  assert.throws(() => resolveLink('https:relative-host', from), /invalid external link/)
  assert.throws(() => resolveLink('//', from), /invalid protocol-relative link/)
})

function temporaryRepository(context: test.TestContext): string {
  const repositoryRoot = mkdtempSync(join(tmpdir(), 'pdf-links-'))
  context.after(() => rmSync(repositoryRoot, { recursive: true, force: true }))
  return repositoryRoot
}

function document(
  repositoryRoot: string,
  unit: ContentUnit,
  relativePath: string,
  canonicalPath: string,
): SourceDocument {
  const repositoryPath = `documents/${unit.sourceDir}/${relativePath}`
  fixture(repositoryRoot, repositoryPath, '# Fixture')
  return {
    sourcePath: join(repositoryRoot, repositoryPath),
    relativePath,
    repositoryPath,
    unit,
    docId: `doc-${unit.id}-${relativePath.replace(/[^A-Za-z0-9]+/g, '-')}`,
    canonicalPath,
    title: canonicalPath,
    description: '',
    chapter: '',
    order: 0,
    kind: 'article',
    markdown: '# Fixture',
    frontmatter: {},
  }
}

function fixture(repositoryRoot: string, relativePath: string, content: string): void {
  const path = join(repositoryRoot, relativePath)
  mkdirSync(dirname(path), { recursive: true })
  writeFileSync(path, content, 'utf8')
}

function escapeRegExp(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}
