import assert from 'node:assert/strict'
import test from 'node:test'

import type { ContentUnit, RenderedDocument } from '../model'
import { validateRenderedDocuments } from '../validate'

const unit: ContentUnit = {
  id: 'fixture',
  sourceDir: 'fixture',
  urlPrefix: '/fixture',
}

function rendered(html: string): RenderedDocument {
  return {
    sourcePath: '/tmp/pdf-validate-fixture/documents/fixture/topic.md',
    relativePath: 'topic.md',
    repositoryPath: 'documents/fixture/topic.md',
    unit,
    docId: 'doc-fixture-topic',
    canonicalPath: '/fixture/topic',
    title: 'Fixture topic',
    description: '',
    chapter: '1',
    order: 1,
    kind: 'article',
    markdown: '',
    frontmatter: {},
    html,
    headings: [],
    rootAnchor: 'doc-fixture-topic',
    endnotes: [],
    stats: {
      chapterNav: 0, chapterLink: 0, onlineCompilerDemo: 0, refLink: 0,
      referenceCard: 0, referenceItem: 0, talkInfoCard: 0, remoteImages: 0,
      internalLinks: 0, crossBookLinks: 0, paperContext: 0,
    },
  }
}

test('allows passive book HTML and renderer-owned MathJax SVG', () => {
  const document = rendered(`<h1 id="doc-fixture-topic--title">Safe</h1>
<p><a href="#doc-fixture-topic">Article root</a> · <a href="https://example.test/reference">Reference</a></p>
<mjx-container class="MathJax" jax="SVG">
  <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10" role="img">
    <g><path d="M0 0L10 10"></path></g>
  </svg>
  <mjx-assistive-mml><math><mrow><mi>x</mi><mo>+</mo><mn>1</mn></mrow></math></mjx-assistive-mml>
</mjx-container>`)

  assert.doesNotThrow(() => validateRenderedDocuments([document]))
})

for (const [name, html, expected] of [
  [
    'multiline event-handler image',
    `<img
      src="data:image/png;base64,iVBORw0KGgo="
      onerror="console.error('unsafe')">`,
    /unsafe rendered attribute onerror/,
  ],
  [
    'multiline script element',
    `<script
      src="data:text/javascript,console.error('unsafe')"></script>`,
    /unsafe or unknown rendered element <script>/,
  ],
  [
    'multiline unknown element',
    `<widget
      mode="print">content</widget>`,
    /unsafe or unknown rendered element <widget>/,
  ],
  [
    'URL-bearing inline style',
    '<p style="background-image: url(https://example.test/tracker)">content</p>',
    /URL-bearing inline style/,
  ],
  [
    'unprocessed responsive image source',
    '<img src="/assets/safe.png" srcset="https://example.test/leak.png 2x">',
    /unsupported rendered URL attribute srcset/,
  ],
  [
    'hidden attribute',
    '<div hidden><h1>Hidden title</h1><p>Hidden body</p></div>',
    /unsafe rendered attribute hidden/,
  ],
  [
    'vertically clipped wrapper',
    '<div style="height: 1px; overflow: hidden"><h1>Clipped title</h1><p>Clipped body</p></div>',
    /print-suppressing inline style/,
  ],
  [
    'display-none wrapper',
    '<div style="display: none"><h1>Hidden title</h1><p>Hidden body</p></div>',
    /print-suppressing inline style/,
  ],
] as const) {
  test(`fails closed for a ${name} after Markdown rendering`, () => {
    assert.throws(() => validateRenderedDocuments([rendered(html)]), expected)
  })
}

test('requires every rendered source document to retain a usable main title', () => {
  assert.throws(
    () => validateRenderedDocuments([rendered('<p>Body without a document title.</p>')]),
    /rendered document has no usable h1 title/,
  )
  assert.throws(
    () => validateRenderedDocuments([rendered('<h1>\u200b</h1>')]),
    /rendered document has no usable h1 title/,
  )
})

for (const [name, html] of [
  [
    'MathML merror node',
    '<mjx-container class="MathJax" jax="SVG"><merror><mtext>Undefined control sequence</mtext></merror></mjx-container>',
  ],
  [
    'MathJax error class',
    '<mjx-container class="MathJax mjx-merror" jax="SVG"><math><mi>x</mi></math></mjx-container>',
  ],
  [
    'MathJax error attribute',
    '<mjx-container class="MathJax" jax="SVG" data-mjx-error="Undefined control sequence"><math><mi>x</mi></math></mjx-container>',
  ],
] as const) {
  test(`rejects a rendered ${name}`, () => {
    assert.throws(
      () => validateRenderedDocuments([rendered(`<h1>Formula fixture</h1>${html}`)]),
      /MathJax reported a formula error|unsafe or unknown rendered element <merror>/,
    )
  })
}
