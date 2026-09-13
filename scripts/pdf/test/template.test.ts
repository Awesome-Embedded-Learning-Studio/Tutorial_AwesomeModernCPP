import assert from 'node:assert/strict'
import test from 'node:test'
import { parseHTML } from 'linkedom'
import { getBook, getLocale } from '../books'
import { emptyTransformStats } from '../transform'
import { assembleBookHtml } from '../template'
import type { DocumentKind, RenderedDocument } from '../model'

function document(path: string, title: string, kind: DocumentKind = 'article', chapter = '7'): RenderedDocument {
  return {
    unit: { id: 'vol3', sourceDir: 'vol3', urlPrefix: '/vol3' },
    repositoryPath: `documents/vol3/${path}`, sourcePath: `/tmp/vol3/${path}`,
    relativePath: path, canonicalPath: `/vol3/${path}`, docId: path.replaceAll('/', '-'),
    rootAnchor: path.replaceAll('/', '-'), title, kind, chapter, order: 0,
    description: '', markdown: '', html: `<h1>${title}</h1><p>Content</p>`,
    frontmatter: {}, headings: [], endnotes: [], stats: emptyTransformStats(),
  }
}

function render(documents: RenderedDocument[]) {
  return parseHTML(assembleBookHtml(getBook('vol3'), getLocale('zh'), documents,
    { revision: 'fixture', version: 'fixture', generatedAt: '2026-09-13' },
    { stylesheet: '/book.css', runtimeScript: '/runtime.js', pagedPolyfillScript: '/paged.js' },
  )).document
}

test('running titles follow nearest index despite reused numbers and nested indexes', () => {
  const docs = [
    document('index.md', 'Book', 'book-index', ''),
    document('containers/index.md', 'Containers', 'chapter-index', ''),
    document('containers/primer/index.md', 'Primer', 'chapter-index', ''),
    document('containers/primer/intro.md', 'Data structures'),
    document('containers/vector.md', 'Vector'),
    document('strings/index.md', 'Strings', 'chapter-index', ''),
    document('strings/char8.md', 'char8_t'),
    document('strings/view.md', 'string_view', 'article', ''),
    document('strings-other/topic.md', 'Unrelated', 'article', ''),
  ]
  const output = render(docs)
  assert.deepEqual(Array.from(output.querySelectorAll('article'), (a) => a.getAttribute('data-running-title')),
    ['标准库', 'Containers', 'Primer', 'Primer', 'Containers', 'Strings', 'Strings', 'Strings', 'Unrelated'])
  assert.equal(output.querySelectorAll('.book-document--chapter-start').length, 3)
})

test('flat chapters break only when chapter changes and use article titles', () => {
  const output = render([
    document('a.md', 'A', 'article', '1'), document('b.md', 'B', 'article', '1'),
    document('c.md', 'C', 'article', '2'),
  ])
  assert.deepEqual(Array.from(output.querySelectorAll('article'), (a) => a.getAttribute('data-running-title')), ['A', 'B', 'C'])
  assert.equal(output.querySelectorAll('.book-document--chapter-start').length, 2)
})
