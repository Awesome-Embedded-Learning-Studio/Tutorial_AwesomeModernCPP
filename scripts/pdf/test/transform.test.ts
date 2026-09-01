import assert from 'node:assert/strict'
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import test from 'node:test'
import { parseHTML } from 'linkedom'

import type { BookLocale, ContentUnit, SourceDocument } from '../model'
import { createBookMarkdownRenderer } from '../markdown'
import {
  prepareSourceMarkdown,
  rewriteDocumentLinks,
  transformDocument,
  type TransformContext,
} from '../transform'

const unit: ContentUnit = {
  id: 'fixture',
  sourceDir: 'fixture',
  urlPrefix: '/fixture',
}

const locale: BookLocale = {
  language: 'zh',
  htmlLang: 'zh-CN',
  sourcePrefix: '',
  onlinePrefix: 'https://example.test',
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

test('preserves known components, standard HTML, and CommonMark autolinks', () => {
  const markdown = `<ChapterNav variant="sub">
  <ChapterLink num="1" href="/fixture/topic">Topic</ChapterLink>
</ChapterNav>
<OnlineCompilerDemo source-path="code/example.cpp" />
<RefLink id="ref-1" />
<ReferenceCard title="References"><ReferenceItem id="ref-1" /></ReferenceCard>
<TalkInfoCard speaker="Ada" />

<details open><summary>Details</summary><p>Body<br></p></details>
<https://example.com/reference?q=cpp>
<reader@example.com>`

  assert.equal(prepareSourceMarkdown(source(markdown)), markdown)
})

test('does not inspect fenced or inline code', () => {
  const markdown = `\`\`\`cpp
Number<T> value;
Foo<K,V> pair;
<UnknownWidget mode="interactive" />
\`\`\`

~~~vue
<AnotherUnknown>content</AnotherUnknown>
~~~

Inline \`Number<T> and <UnknownWidget enabled>\` remains literal.`

  assert.equal(prepareSourceMarkdown(source(markdown)), markdown)
})

test('does not inspect multiline code spans or fenced multiline component examples', () => {
  const markdown = `Inline \`<UnknownWidget
mode="example" />\` remains literal.

Inline \`
<UnknownWidget />
Number<T>
\` remains literal.

\`\`\`vue
<unknown-widget
  mode="example"
/>
\`\`\``

  assert.equal(prepareSourceMarkdown(source(markdown)), markdown)
})

test('escapes ambiguous C++ template angle expressions in prose', () => {
  const markdown = 'Use Number<T>, Foo<K,V>, vector\\<bool>, the escaped spelling <T\\>, and header <utility> in prose.'

  assert.equal(
    prepareSourceMarkdown(source(markdown)),
    'Use Number&lt;T&gt;, Foo&lt;K,V&gt;, vector\\<bool>, the escaped spelling &lt;T&gt;, and header &lt;utility&gt; in prose.',
  )
})

for (const [name, markdown] of [
  ['paired component', 'Before <UnknownWidget>body</UnknownWidget> after'],
  ['component with attributes', 'Before <UnknownWidget mode="print"> after'],
  ['self-closing component', 'Before <UnknownWidget /> after'],
  ['standalone component line', 'Before\n<UnknownWidget>\nAfter'],
  ['kebab-case component', 'Before <unknown-widget enabled="true"> after'],
  ['lowercase component', 'Before <widget mode="print">body</widget> after'],
  ['script tag', '<script>document.body.remove()</script>'],
  ['style tag', '<style>body { display: none }</style>'],
  ['multiline lowercase component', '<widget\n mode="print"\n/>'],
  ['multiline PascalCase component', '<UnknownWidget\n mode="print"\n/>'],
  ['multiline kebab-case component', '<unknown-widget\n mode="print"\n/>'],
  ['bare lowercase component', '<widget>'],
  ['component after an invalid backtick fence opener', '```vue`oops\n<widget\n mode="print"\n/>\n```'],
] as const) {
  test(`fails closed for an unknown ${name}`, () => {
    assert.throws(
      () => prepareSourceMarkdown(source(markdown)),
      /documents\/fixture\/topic\.md:\d+: unknown component <(?:UnknownWidget|unknown-widget|widget|script|style)>/,
    )
  })
}

test('rejects executable attributes on otherwise passive HTML', () => {
  assert.throws(
    () => prepareSourceMarkdown(source('<img src="missing.png" onerror="document.body.remove()">')),
    /unsafe HTML attribute on <img>/,
  )
})

test('rejects mis-cased spellings of known components', () => {
  assert.throws(
    () => prepareSourceMarkdown(source('<chapterlink href="/fixture/target">Target</chapterlink>')),
    /component <chapterlink> must use <ChapterLink> casing/,
  )
})

test('the real VitePress renderer cannot hide unknown multiline or mis-cased components', async () => {
  const repositoryRoot = resolve(import.meta.dirname, '..', '..', '..')
  const markdown = await createBookMarkdownRenderer(repositoryRoot)
  const context: TransformContext = {
    repositoryRoot,
    markdown,
    locale,
    assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }

  for (const fixture of [
    '<widget>',
    '<widget\n mode="print"\n/>',
    '<UnknownWidget\n mode="print"\n/>',
    '<unknown-widget\n mode="print"\n/>',
    '```vue`oops\n<widget\n mode="print"\n/>\n```',
  ]) {
    await assert.rejects(() => transformDocument(source(`# Fixture topic\n\n${fixture}`), context), /unknown component/)
  }
  await assert.rejects(
    () => transformDocument(
      source('# Fixture topic\n\n<chapterlink href="/fixture/target">Target</chapterlink>'),
      context,
    ),
    /must use <ChapterLink> casing/,
  )

  const rendered = await transformDocument(source(`# Fixture topic

<ChapterLink
  href="https://example.test/target"
>
Target
</ChapterLink>`), context)
  assert.match(rendered.html, /class="chapter-xref"/)
  assert.doesNotMatch(rendered.html, /ChapterLink|chapterlink/)
})

test('a rendered fragment contains no known component residue and retains its title', async () => {
  const markdown = `<h1 id="title">Fixture</h1>
<ChapterNav variant="sub"><ChapterLink href="/fixture/target">Inside nav</ChapterLink></ChapterNav>
<p><a href="#旧锚点">Memory Management</a></p>
<h2 id="memory-management">Memory Management</h2>
<ChapterLink href="/fixture/target">Target</ChapterLink>
<RefLink id="one" />
<ReferenceCard title="References">
  <ReferenceItem id="one" author="Ada" title="Paper" />
</ReferenceCard>
<TalkInfoCard talk-title="Talk" speaker="Speaker" />`
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: {
      render: (value: string) => value,
    } as TransformContext['markdown'],
    locale,
    // This fixture contains no images or compiler demos, so the transform never
    // calls the asset manager. The typed stub keeps this a focused unit test.
    assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }

  const rendered = await transformDocument(source(markdown), context)

  assert.doesNotMatch(
    rendered.html,
    /<(?:chapternav|chapterlink|reflink|referencecard|referenceitem|talkinfocard|qqgroupcard)\b/i,
  )
  assert.match(rendered.html, /class="chapter-xref"/)
  assert.match(rendered.html, /class="reference-link"/)
  assert.match(rendered.html, /class="reference-card"/)
  assert.match(rendered.html, /class="reference-number">\[one\]/)
  assert.match(rendered.html, /class="talk-info-card"/)
  assert.match(rendered.html, /href="#doc-fixture-topic--memory-management"/)
  assert.match(rendered.html, /<h1[^>]*>Fixture<\/h1>/)
})

test('promotes a title-like h2 when translated source has no h1', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale,
    assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const rendered = await transformDocument(source('<h2>Fixture topic</h2><p>Body</p>'), context)

  assert.match(rendered.html, /<h1[^>]*>Fixture topic<\/h1>/)
  assert.doesNotMatch(rendered.html, /<h2[^>]*>Fixture topic<\/h2>/)
})

test('preserves every local image when several share one paragraph', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale,
    assets: {
      resolveSourceAsset: (_from: string, raw: string) => `/tmp/pdf-transform-fixture/${raw}`,
      copyLocalAsset: async (path: string) => `/assets/${path.split('/').at(-1)}`,
    } as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const rendered = await transformDocument(
    source('<h1>Fixture topic</h1><p><img src="one.png" alt="one"> <img src="two.png" alt="two"></p>'),
    context,
  )

  assert.equal((rendered.html.match(/<img\b/g) ?? []).length, 2)
  assert.match(rendered.html, /src="\/assets\/one\.png"/)
  assert.match(rendered.html, /src="\/assets\/two\.png"/)
})

test('renders QQGroupCard as a paper aside with the staged QR asset', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale,
    assets: {
      resolveSourceAsset: (_from: string, raw: string) => `/tmp/pdf-transform-fixture/${raw.replace(/^\//, '')}`,
      copyLocalAsset: async (path: string) => `/assets/${path.split('/').at(-1)}`,
    } as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const rendered = await transformDocument(
    source('<h1>Fixture topic</h1>\n<QQGroupCard />'),
    context,
  )

  assert.match(rendered.html, /class="qq-group-card"/)
  assert.match(rendered.html, /class="id-value">1107100989/)
  assert.match(rendered.html, /href="https:\/\/qm\.qq\.com\/q\/cD89HxtmUg"/)
  assert.match(rendered.html, /src="\/assets\/qq-group\.svg"/)
  assert.doesNotMatch(rendered.html, /<qqgroupcard\b/i)
  assert.equal(rendered.stats.qqGroupCard, 1)
})

test('rewrites OnlineCompilerDemo instructions into a paper context', async (testContext) => {
  const repositoryRoot = await mkdtemp(join(tmpdir(), 'pdf-online-demo-'))
  testContext.after(() => rm(repositoryRoot, { recursive: true, force: true }))
  await writeFile(join(repositoryRoot, 'demo.cpp'), 'int main() { return 0; }\n')
  const context: TransformContext = {
    repositoryRoot,
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale,
    assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const rendered = await transformDocument(source(`<h1>Fixture topic</h1>
<h2>在线运行</h2>
<p>试着在线编辑并运行这段代码，修改输出看看效果：</p>
<OnlineCompilerDemo source-path="demo.cpp"
  description="在浏览器中编辑并运行示例。切换到 ARM 汇编查看效果。" />`), context)

  assert.match(rendered.html, /<h2[^>]*>示例源码<\/h2>/)
  assert.match(rendered.html, /以下为配套静态源码/)
  assert.match(rendered.html, /阅读示例。另附 ARM 版本源码，可用于对照效果。/)
  assert.doesNotMatch(rendered.html, /在线运行|在浏览器中|切换到 ARM 汇编/)
})

test('materializes stable Shiki line numbers before Paged.js cloning', async () => {
  const vitePressHtml = `<div class="vp-code-group"><div class="tabs"><input type="radio"><label>CPP</label></div>
<pre class="shiki" v-pre><button class="copy">Copy</button><code><span class="line">first</span>
<span class="line"></span>
<span class="line">third</span></code></pre></div>`
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: {
      render: () => vitePressHtml,
    } as TransformContext['markdown'],
    locale,
    assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }

  const rendered = await transformDocument(source('```cpp\nfixture\n```'), context)

  assert.match(rendered.html, /data-line-count="3"/)
  assert.doesNotMatch(rendered.html, /\bv-pre\b/)
  assert.doesNotMatch(rendered.html, /<button\b|class="tabs"|<input\b|<label\b/)
  assert.deepEqual(
    Array.from(rendered.html.matchAll(/data-line-number="(\d+)"/g), (match) => match[1]),
    ['1', '2', '3'],
  )

  const { document } = parseHTML(`<main>${rendered.html}</main>`)
  const code = document.querySelector('pre[data-line-count] > code')
  assert.ok(code)
  assert.deepEqual(Array.from(code.childNodes, (node) => node.nodeType), [1, 1, 1])
  const renderedLines = Array.from(code.children)
  assert.equal(renderedLines.length, 3)
  assert.equal(renderedLines[1].textContent, '')
  assert.equal(renderedLines[1].getAttribute('data-line-number'), '2')
})

test('rejects executable or embedded-document link schemes', () => {
  for (const href of ['javascript:alert(1)', 'data:text/html,unsafe']) {
    const rendered = {
      ...source(''),
      html: `<p><a href="${href}">unsafe</a></p>`,
      headings: [],
      rootAnchor: 'doc-fixture-topic',
      endnotes: [],
      stats: {
        chapterNav: 0, chapterLink: 0, onlineCompilerDemo: 0, refLink: 0,
        referenceCard: 0, referenceItem: 0, talkInfoCard: 0, qqGroupCard: 0, remoteImages: 0,
        internalLinks: 0, crossBookLinks: 0, paperContext: 0,
      },
    }
    assert.throws(
      () => rewriteDocumentLinks(rendered, (value) => ({ kind: 'external', href: value })),
      /unsafe link scheme/,
    )
  }
})

function source(markdown: string): SourceDocument {
  return {
    sourcePath: '/tmp/pdf-transform-fixture/documents/fixture/topic.md',
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
    markdown,
    frontmatter: {},
  }
}
