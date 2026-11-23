import assert from 'node:assert/strict'
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises'
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
    animationNote: '此处为一段可分步播放的动画，纸质版从略，线上版：',
    videoNote: '此处为一段演示视频，纸质版从略，线上版：',
    interactiveNote: '此处为在线版的交互视图（路径图与成熟度板），纸质版从略，线上版：',
    contributors: '本文贡献者',
  },
}

test('preserves known components, standard HTML, and CommonMark autolinks', () => {
  const markdown = `<ChapterNav variant="sub">
  <ChapterLink num="1" href="/fixture/topic">Topic</ChapterLink>
</ChapterNav>
<OnlineCompilerDemo source-path="code/example.cpp" />
<Anim id="compilation-pipeline" />
<RefLink id="ref-1" />
<ReferenceCard title="References"><ReferenceItem id="ref-1" /></ReferenceCard>
<TalkInfoCard speaker="Ada" />

<details open><summary>Details</summary><p>Body<br></p></details>
<https://example.com/reference?q=cpp>
<reader@example.com>`

  assert.equal(prepareSourceMarkdown(source(markdown)), markdown)
})

test('accepts path placeholders such as /proc/self/fdinfo/<epfd> as literal text', () => {
  const markdown = '其实 /proc/self/fdinfo/<epfd> 把它整个摊开了:tfd 行就是在册的 fd。'
  const prepared = prepareSourceMarkdown(source(markdown))
  assert.equal(prepared, '其实 /proc/self/fdinfo/&lt;epfd&gt; 把它整个摊开了:tfd 行就是在册的 fd。')
})

test('accepts bare fundamental-type tokens such as <int> used as nouns in prose', () => {
  const markdown = '模板的实参也被编了进去,里面的 Ii,对应的就是 <int>。'
  const prepared = prepareSourceMarkdown(source(markdown))
  assert.equal(prepared, '模板的实参也被编了进去,里面的 Ii,对应的就是 &lt;int&gt;。')
})

test('accepts CMake generator expressions such as $<TARGET_FILE:app> in prose', () => {
  const markdown = 'CMake 备了生成器表达式:$<TARGET_FILE:app> 在配置阶段还是一串字面量。'
  const prepared = prepareSourceMarkdown(source(markdown))
  assert.equal(prepared, 'CMake 备了生成器表达式:$&lt;TARGET_FILE:app&gt; 在配置阶段还是一串字面量。')
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

test('renders an Anim player as a pointer to the live article', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(source('<h1>Fixture</h1><p>先看动画：</p><Anim id="opp1-vector-growth" />'), context)

  const { document } = parseHTML(result.html)
  const note = document.querySelector('p.book-media-note')
  assert.ok(note, 'expected a .book-media-note paragraph')
  assert.equal(note.querySelector('a')?.getAttribute('href'), 'https://example.test/fixture/topic')
  assert.equal(note.querySelector('a')?.textContent, 'Fixture topic')
  assert.doesNotMatch(result.html, /<anim\b/i)
  assert.equal(result.stats.anim, 1)
})

test('renders a local video demo as a pointer to the live article', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(
    source('<h1>Fixture</h1><p>跑起来是这副样子：</p><video controls muted src="./blinky.mp4" width="640"></video>'),
    context,
  )

  assert.doesNotMatch(result.html, /<video\b/i)
  assert.match(result.html, /class="book-media-note"/)
  assert.match(result.html, /href="https:\/\/example\.test\/fixture\/topic"/)
  assert.equal(result.stats.anim, 1)
})

test('renders ArticleContributors as a plain signature block from frontmatter', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(source('<h1>Fixture</h1><p>正文。</p>\n<ArticleContributors />', {
    contributors: [
      { github: 'xiaoshuaijie', roles: ['author'], note: { zh: '撰写并投稿原稿', en: 'Wrote the original' }, pr: 276 },
      { github: 'charliechen114514', name: 'Charlie', roles: ['revision', 'review'] },
    ],
  }), context)

  assert.doesNotMatch(result.html, /articlecontributors/i)
  assert.match(result.html, /class="article-contributors"/)
  assert.match(result.html, /本文贡献者/)
  assert.match(result.html, /href="https:\/\/github\.com\/xiaoshuaijie"/)
  assert.match(result.html, /修订 \/ 审校/)
  assert.match(result.html, /撰写并投稿原稿/)
  assert.match(result.html, /href="https:\/\/github\.com\/Awesome-Embedded-Learning-Studio\/Tutorial_AwesomeModernCPP\/pull\/276"/)
  assert.match(result.html, />Charlie</)
  assert.equal(result.stats.articleContributors, 1)
})

test('removes ArticleContributors silently when frontmatter has no contributor data', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(source('<h1>Fixture</h1><p>正文。</p>\n<ArticleContributors />'), context)

  assert.doesNotMatch(result.html, /articlecontributors/i)
  assert.doesNotMatch(result.html, /article-contributors/)
  assert.equal(result.stats.articleContributors, 1)
})

test('falls back to the default author for numbered articles without credits', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(
    source('<h1>Fixture</h1><p>正文。</p>\n<ArticleContributors />', { chapter: 3 }),
    context,
  )

  assert.match(result.html, /class="article-contributors"/)
  assert.match(result.html, /Charliechen114514/)
  assert.match(result.html, /文章撰写与长期维护/)
})

test('EN articles inherit the Chinese source credits', async () => {
  const repositoryRoot = await mkdtemp(join(tmpdir(), 'pdf-en-inherit-'))
  try {
    await mkdir(join(repositoryRoot, 'documents', 'fixture'), { recursive: true })
    await writeFile(
      join(repositoryRoot, 'documents', 'fixture', 'topic.md'),
      '---\ncontributors:\n  - github: zhauthor\n    roles: [author]\n---\n\n# 中文源\n',
      'utf8',
    )
    const en = {
      ...source('<h1>Fixture</h1><p>Body.</p>\n<ArticleContributors />', { chapter: 1 }),
      relativePath: 'fixture/topic.md',
      repositoryPath: 'documents/en/fixture/topic.md',
    }
    const context: TransformContext = {
      repositoryRoot,
      markdown: { render: (value: string) => value } as TransformContext['markdown'],
      locale, assets: {} as TransformContext['assets'],
      resolveLink: (href) => ({ kind: 'external', href }),
    }
    const result = await transformDocument(en, context)

    assert.match(result.html, /class="article-contributors"/)
    assert.match(result.html, /zhauthor/)
  } finally {
    await rm(repositoryRoot, { recursive: true, force: true })
  }
})

test('community articles stay uncredited without an explicit list', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const communitySource = {
    ...source('<h1>Fixture</h1><p>正文。</p>\n<ArticleContributors />', { chapter: 2 }),
    unit: { ...unit, id: 'community' },
  }
  const result = await transformDocument(communitySource, context)

  assert.doesNotMatch(result.html, /article-contributors/)
})

test('unwraps ConcurrencyTimeline into its slot fallback with a caption', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(
    source(`<h1>Fixture</h1><ConcurrencyTimeline
  title="单核 · 交替执行"
  axis-hint="时间往右 →"
  :ticks="['第 1 段']"
  :lanes="['一个核心']"
>
\`\`\`mermaid
graph LR
    A["发出下载请求"] --> B["处理列表滚动"]
\`\`\`

</ConcurrencyTimeline>`),
    context,
  )

  assert.doesNotMatch(result.html, /concurrencytimeline/i)
  assert.match(result.html, /class="book-timeline"/)
  assert.match(result.html, /<figcaption>单核 · 交替执行<\/figcaption>/)
  assert.ok(result.html.includes('graph LR'))
  assert.ok(result.html.includes('处理列表滚动'))
  assert.equal(result.stats.concurrencyTimeline, 1)
})

test('strips hand-written v-pre directives but keeps literal braces', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(
    source('<h1>Fixture</h1><p><span v-pre>`{{`</span> 是字面量。</p>'),
    context,
  )

  assert.doesNotMatch(result.html, /v-pre/i)
  assert.ok(result.html.includes('{{'))
})

test('renders AsmExecutor payloads as a static disassembly listing', async () => {
  const repositoryRoot = await mkdtemp(join(tmpdir(), 'pdf-asm-executor-'))
  try {
    await mkdir(join(repositoryRoot, 'documents', 'public', 'payloads'), { recursive: true })
    const payload = {
      format: 'asm-executor/payload@1',
      meta: { isa: 'x86-64', syntax: 'att', title: 'i++ 与 ++i：同一段代码的两副指令', entry: '0x1000' },
      code: [
        { addr: '0x1000', bytes: '55', text: 'push   %rbp' },
        { addr: '0x1001', bytes: '4889e5', text: 'mov    %rsp, %rbp' },
      ],
    }
    await writeFile(
      join(repositoryRoot, 'documents', 'public', 'payloads', 'demo.json'),
      JSON.stringify(payload),
      'utf8',
    )
    const context: TransformContext = {
      repositoryRoot,
      markdown: { render: (value: string) => value } as TransformContext['markdown'],
      locale, assets: {} as TransformContext['assets'],
      resolveLink: (href) => ({ kind: 'external', href }),
    }
    const result = await transformDocument(
      source('<h1>Fixture</h1><AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/demo.json" />'),
      context,
    )

    assert.doesNotMatch(result.html, /asmexecutor/i)
    assert.match(result.html, /class="asm-listing"/)
    assert.match(result.html, /<figcaption>i\+\+ 与 \+\+i：同一段代码的两副指令<\/figcaption>/)
    assert.ok(result.html.includes('push   %rbp'))
    assert.ok(result.html.includes('0x1000'))
    assert.match(result.html, /online-demo-note/)
    assert.equal(result.stats.asmExecutor, 1)
  } finally {
    await rm(repositoryRoot, { recursive: true, force: true })
  }
})

test('fails closed on malformed frontmatter contributors', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  await assert.rejects(
    transformDocument(source('<h1>Fixture</h1><ArticleContributors />', { contributors: [{ github: 'not ok!' }] }), context),
    /invalid "github"/,
  )
  await assert.rejects(
    transformDocument(source('<h1>Fixture</h1><ArticleContributors />', { contributors: 'nope' }), context),
    /must be a list/,
  )
})

test('publishes nested details as static asides and keeps title links and body intact', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale, assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(source(`<h1>Fixture</h1><details id="note"><summary id="caption">点开看：<em>解释</em></summary>
<p>Before</p><details><summary>Click to expand: Nested</summary><p>Inside</p></details><p>After</p></details>`), context)
  const { document } = parseHTML(result.html)
  assert.equal(document.querySelectorAll('details, summary').length, 0)
  assert.equal(document.querySelectorAll('aside.book-details').length, 2)
  assert.equal(document.querySelector('.custom-block-title em')?.textContent, '解释')
  assert.match(result.html, /doc-fixture-topic--note/)
  assert.match(result.html, /doc-fixture-topic--caption/)
  assert.doesNotMatch(result.html, /点开看|Click to expand/)
  for (const text of ['Before', 'Inside', 'After', 'Nested']) assert.ok(result.html.includes(text))
})

test('separates an image from its preceding text without dropping inline markup or anchors', async () => {
  const context: TransformContext = {
    repositoryRoot: '/tmp/pdf-transform-fixture',
    markdown: { render: (value: string) => value } as TransformContext['markdown'],
    locale,
    assets: {
      resolveSourceAsset: (_from: string, raw: string) => `/tmp/pdf-transform-fixture/${raw}`,
      copyLocalAsset: async () => '/assets/image.png',
    } as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }
  const result = await transformDocument(source('<h1>Fixture</h1><blockquote><p id="intro">说明 <em>重点</em>\n<img src="image.png" alt="图注">后文</p></blockquote>'), context)
  const { document } = parseHTML(result.html)
  assert.equal(document.querySelector('.book-image-intro em')?.textContent, '重点')
  assert.equal(document.querySelector('figure figcaption')?.textContent, '图注')
  assert.equal(document.querySelectorAll('img').length, 1)
  assert.equal(document.querySelectorAll('[id$="--intro"]').length, 1)
  assert.ok(result.html.includes('后文'))
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

test('renders semantic output fences in the PDF pipeline', async () => {
  const repositoryRoot = resolve(import.meta.dirname, '..', '..', '..')
  const markdown = await createBookMarkdownRenderer(repositoryRoot)
  const context: TransformContext = {
    repositoryRoot,
    markdown,
    locale,
    assets: {} as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }

  const rendered = await transformDocument(
    source('# Fixture topic\n\n```output:no-line-numbers\nvalue = 42\n```'),
    context,
  )
  assert.match(rendered.html, /class="language-output"/)
  assert.match(rendered.html, /data-output-label="运行结果"/)
  assert.match(rendered.html, /data-code-caption="运行结果"/)
  assert.match(rendered.html, /value = 42/)
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

function source(markdown: string, frontmatter: SourceDocument['frontmatter'] = {}): SourceDocument {
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
    frontmatter,
  }
}
