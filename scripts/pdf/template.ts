import type {
  BookDefinition,
  BookLocale,
  BuildMetadata,
  RenderedDocument,
} from './model'

/**
 * URLs are resolved by the caller (normally against the temporary book server).
 * Keeping them out of this module lets the HTML stay identical in local and CI
 * builds while all browser dependencies remain local and version-pinned.
 */
export interface BookAssetUrls {
  stylesheet: string
  runtimeScript: string
  pagedPolyfillScript: string
  mermaidModule?: string
  drawioViewerScript?: string
}

interface PreparedDocument {
  document: RenderedDocument
  chapterStart: boolean
  runningTitle: string
}

const localStrings = {
  zh: {
    series: '现代 C++ 教程',
    studio: 'Awesome Embedded Learning Studio',
    page: '页',
  },
  en: {
    series: 'Modern C++ Tutorial',
    studio: 'Awesome Embedded Learning Studio',
    page: 'page',
  },
} as const

function escapeHtml(value: string): string {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;')
}

function escapeAttribute(value: string): string {
  return escapeHtml(value).replaceAll('`', '&#96;')
}

/** Keep JSON inert inside a classic script element. */
function serializeForScript(value: unknown): string {
  return JSON.stringify(value)
    .replaceAll('<', '\\u003c')
    .replaceAll('>', '\\u003e')
    .replaceAll('&', '\\u0026')
    .replaceAll('\u2028', '\\u2028')
    .replaceAll('\u2029', '\\u2029')
}

function assertNonEmpty(name: string, value: string): void {
  if (value.trim() === '') throw new Error(`${name} must not be empty`)
}

function displayYear(value: string): string {
  const isoYear = /^(\d{4})/.exec(value)
  return isoYear?.[1] ?? value
}

function chapterKey(document: RenderedDocument): string {
  const chapter = String(document.chapter ?? '').trim()
  return chapter === '' ? '' : `${document.unit.id}:${chapter}`
}

/**
 * A chapter index normally precedes its articles. The second pass maps the
 * articles' frontmatter chapter value back to that human-readable title, so a
 * running header says "指针与引用" instead of merely "4".
 */
function prepareDocuments(
  documents: readonly RenderedDocument[],
  bookTitle: string,
): PreparedDocument[] {
  const titleByChapter = new Map<string, string>()
  let precedingChapterTitle = ''
  let precedingUnit = ''

  for (const document of documents) {
    if (document.unit.id !== precedingUnit) {
      precedingUnit = document.unit.id
      precedingChapterTitle = ''
    }
    if (document.kind === 'book-index') precedingChapterTitle = ''
    if (document.kind === 'chapter-index') precedingChapterTitle = document.title
    const key = chapterKey(document)
    if (key !== '' && precedingChapterTitle !== '' && !titleByChapter.has(key)) {
      titleByChapter.set(key, precedingChapterTitle)
    }
  }

  const prepared: PreparedDocument[] = []
  let runningTitle = bookTitle
  let previousArticleChapter = ''
  let currentUnit = ''
  let inExplicitChapterScope = false

  for (const document of documents) {
    if (document.unit.id !== currentUnit) {
      currentUnit = document.unit.id
      runningTitle = bookTitle
      previousArticleChapter = ''
      inExplicitChapterScope = false
    }
    const key = chapterKey(document)
    let chapterStart = document.kind === 'chapter-index'

    if (document.kind === 'book-index') {
      runningTitle = bookTitle
      previousArticleChapter = ''
      inExplicitChapterScope = false
    } else if (document.kind === 'chapter-index') {
      runningTitle = document.title
      if (key !== '') previousArticleChapter = key
      inExplicitChapterScope = true
    } else {
      const mappedChapterTitle = key === '' ? undefined : titleByChapter.get(key)
      if (key !== '' && key !== previousArticleChapter) {
        // Collections without an explicit chapter index still get one break at
        // the start of a new chapter value, but not before every article.
        chapterStart = mappedChapterTitle === undefined
        previousArticleChapter = key
      }

      if (mappedChapterTitle !== undefined) {
        runningTitle = mappedChapterTitle
        inExplicitChapterScope = true
      } else if (key !== '') {
        // A frontmatter chapter number alone does not provide a useful human
        // header. Flat collections often reuse one number for every article.
        runningTitle = document.title
        inExplicitChapterScope = false
      } else if (key === '' && !inExplicitChapterScope) {
        // Flat books such as getting-started have no chapter frontmatter. A
        // running header follows the current article without forcing that
        // article onto a new page.
        runningTitle = document.title
      }
    }

    prepared.push({ document, chapterStart, runningTitle })
  }

  return prepared
}

function renderTocEntry(document: RenderedDocument): string {
  const kind = escapeAttribute(document.kind)
  const href = `#${escapeAttribute(document.rootAnchor)}`
  return (
    `<li class="toc-entry toc-entry--${kind}">` +
    `<a href="${href}">` +
    `<span class="toc-entry__title">${escapeHtml(document.title)}</span>` +
    '<span class="toc-entry__leader" aria-hidden="true"></span>' +
    '</a></li>'
  )
}

function renderEndnotes(document: RenderedDocument, heading: string): string {
  if (document.endnotes.length === 0) return ''
  const notes = document.endnotes
    .map((note, index) => (
      `<li id="${escapeAttribute(document.rootAnchor)}-endnote-${index + 1}">` +
      `<a href="${escapeAttribute(note)}">${escapeHtml(note)}</a></li>`
    ))
    .join('\n')
  return (
    '<section class="document-endnotes" role="doc-endnotes">' +
    `<h2>${escapeHtml(heading)}</h2>` +
    `<ol>${notes}</ol>` +
    '</section>'
  )
}

function renderDocument(prepared: PreparedDocument, endnotesHeading: string): string {
  const document = prepared.document
  const classes = [
    'book-document',
    `book-document--${document.kind}`,
    prepared.chapterStart ? 'book-document--chapter-start' : '',
  ].filter(Boolean).join(' ')

  return (
    `<article id="${escapeAttribute(document.rootAnchor)}" class="${classes}" ` +
    `data-document-id="${escapeAttribute(document.docId)}" ` +
    `data-source-path="${escapeAttribute(document.repositoryPath)}" ` +
    `data-running-title="${escapeAttribute(prepared.runningTitle)}">` +
    '<div class="book-content">' + document.html + '</div>' +
    renderEndnotes(document, endnotesHeading) +
    `<span class="book-document-sentinel" data-document-sentinel="${escapeAttribute(document.docId)}" aria-hidden="true">&#xfeff;</span>` +
    '</article>'
  )
}

/**
 * Assemble one logical book as one HTML document. Paged.js must see every
 * target in this document so target-counter(), cross-chapter links, page
 * counters, and running strings are resolved in a single pagination pass.
 */
export function assembleBookHtml(
  definition: BookDefinition,
  locale: BookLocale,
  documents: readonly RenderedDocument[],
  metadata: BuildMetadata,
  assets: BookAssetUrls,
): string {
  if (documents.length === 0) throw new Error(`Book "${definition.id}" has no rendered documents`)
  assertNonEmpty('assets.stylesheet', assets.stylesheet)
  assertNonEmpty('assets.runtimeScript', assets.runtimeScript)
  assertNonEmpty('assets.pagedPolyfillScript', assets.pagedPolyfillScript)

  const language = locale.language
  const strings = localStrings[language]
  const bookTitle = definition.title[language]
  const bookLabel = definition.label[language]
  const prepared = prepareDocuments(documents, bookTitle)
  const tocDocuments = documents.filter((document) => document.kind !== 'book-index')
  const tocEntries = tocDocuments.map(renderTocEntry).join('\n')
  const body = prepared.map((document) => renderDocument(document, locale.strings.references)).join('\n')
  const browserAssets = {
    mermaidModule: assets.mermaidModule,
    drawioViewerScript: assets.drawioViewerScript,
    pagedPolyfillScript: assets.pagedPolyfillScript,
  }

  const optionalDrawioScript = assets.drawioViewerScript
    ? `<script defer src="${escapeAttribute(assets.drawioViewerScript)}"></script>`
    : ''
  // viewer-static initializes MathJax unconditionally even when every drawio
  // graph has math="0". Mark it as present so the pinned viewer does not fetch
  // viewer.diagrams.net; math-enabled drawio output is still required to expose
  // a rendered SVG and therefore remains covered by the runtime barrier.
  const drawioMathGuard = assets.drawioViewerScript
    ? 'window.MathJax = window.MathJax || { __bookDrawioNoMath: true };'
    : ''
  const optionalMermaidPreload = assets.mermaidModule
    ? `<link rel="modulepreload" href="${escapeAttribute(assets.mermaidModule)}">`
    : ''

  return `<!doctype html>
<html lang="${escapeAttribute(locale.htmlLang)}" dir="ltr" data-book-id="${escapeAttribute(definition.id)}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="generator" content="Tutorial_AwesomeModernCPP book pipeline">
  <link rel="icon" href="data:,">
  <title>${escapeHtml(bookLabel)} · ${escapeHtml(bookTitle)}</title>
  <link rel="stylesheet" href="${escapeAttribute(assets.stylesheet)}">
  ${optionalMermaidPreload}
  <script>
    window.PagedConfig = { auto: false };
    window.__BOOK_ASSETS__ = ${serializeForScript(browserAssets)};
    ${drawioMathGuard}
  </script>
  <script defer src="${escapeAttribute(assets.pagedPolyfillScript)}"></script>
  ${optionalDrawioScript}
  <script defer src="${escapeAttribute(assets.runtimeScript)}"></script>
</head>
<body data-book-id="${escapeAttribute(definition.id)}" data-book-language="${escapeAttribute(language)}" data-source-document-count="${documents.length}" data-toc-entry-count="${tocDocuments.length}" data-ready-state="pending">
  <main id="book-source" class="book-root" data-book-title="${escapeAttribute(bookTitle)}">
    <section class="book-cover" data-book-title="${escapeAttribute(bookTitle)}" aria-labelledby="book-cover-title">
      <div class="book-cover__frame">
        <p class="book-cover__series">${escapeHtml(strings.series)}</p>
        <p class="book-cover__label">${escapeHtml(bookLabel)}</p>
        <h1 id="book-cover-title">${escapeHtml(bookTitle)}</h1>
        <div class="book-cover__rule" aria-hidden="true"></div>
        <div class="book-cover__credit">
          <p class="book-cover__studio">${escapeHtml(strings.studio)}</p>
          <p><time datetime="${escapeAttribute(metadata.generatedAt)}">${escapeHtml(displayYear(metadata.generatedAt))}</time></p>
        </div>
      </div>
    </section>

    <nav class="book-toc" aria-labelledby="book-toc-title" role="doc-toc">
      <h1 id="book-toc-title">${escapeHtml(locale.strings.contents)}</h1>
      <ol class="toc-list">
        ${tocEntries}
      </ol>
    </nav>

    <div class="book-body">
      ${body}
    </div>
  </main>
  <noscript>This book requires JavaScript during generation so diagrams and pagination can be completed.</noscript>
</body>
</html>
`
}
