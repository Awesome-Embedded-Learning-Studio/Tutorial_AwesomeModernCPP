import { readFile } from 'node:fs/promises'
import { extname, relative, resolve } from 'node:path'
import type MarkdownIt from 'markdown-it'
import { parseHTML } from 'linkedom'
import type { AssetManager } from './assets'
import { languageForPath } from './assets'
import type { BookLocale, HeadingInfo, RenderedDocument, SourceDocument, TransformStats } from './model'
import { renderCodeFence } from './markdown'
import { canonicalRepositoryPath } from './path-safety'

const KNOWN_COMPONENT_NAMES = [
  'ChapterNav', 'ChapterLink', 'OnlineCompilerDemo', 'RefLink',
  'ReferenceCard', 'ReferenceItem', 'TalkInfoCard',
] as const
const KNOWN_COMPONENTS = new Map(
  KNOWN_COMPONENT_NAMES.map((name) => [name.toLowerCase(), name]),
)

const PASSIVE_HTML = new Set([
  'a', 'abbr', 'b', 'bdi', 'bdo', 'blockquote', 'br', 'caption', 'cite', 'code', 'col', 'colgroup',
  'dd', 'del', 'details', 'dfn', 'div', 'dl', 'dt', 'em', 'figcaption', 'figure', 'h1', 'h2', 'h3',
  'h4', 'h5', 'h6', 'hr', 'i', 'img', 'ins', 'kbd', 'li', 'mark', 'ol', 'p', 'picture', 'pre', 'q',
  'rp', 'rt', 'ruby', 's', 'samp', 'small', 'source', 'span', 'strong', 'sub', 'summary', 'sup',
  'table', 'tbody', 'td', 'tfoot', 'th', 'thead', 'time', 'tr', 'u', 'ul', 'var', 'wbr',
])

// A lone lower-case angle token is otherwise indistinguishable from an
// unknown HTML/Vue tag. Only the extensionless C/C++ standard-library header
// spellings used in prose are accepted without surrounding C++ context.
// Project/platform headers should be written as inline code; dotted/slashed
// include paths remain unambiguous and are handled separately below.
const CPP_BARE_HEADER_NAMES = new Set([
  'algorithm', 'any', 'array', 'atomic', 'barrier', 'bit', 'bitset', 'cassert', 'cctype',
  'cerrno', 'cfenv', 'cfloat', 'charconv', 'chrono', 'cinttypes', 'climits', 'clocale',
  'cmath', 'codecvt', 'compare', 'complex', 'concepts', 'condition_variable', 'coroutine',
  'csetjmp', 'csignal', 'cstdarg', 'cstddef', 'cstdint', 'cstdio', 'cstdlib', 'cstring',
  'ctgmath', 'ctime', 'cuchar', 'cwchar', 'cwctype', 'deque', 'exception', 'execution',
  'expected', 'filesystem', 'flat_map', 'flat_set', 'format', 'forward_list', 'fstream',
  'functional', 'future', 'generator', 'hazard_pointer', 'initializer_list', 'iomanip',
  'ios', 'iosfwd', 'iostream', 'istream', 'iterator', 'latch', 'linalg', 'limits', 'list',
  'locale', 'map', 'mdspan', 'memory', 'memory_resource', 'mutex', 'new', 'numbers',
  'numeric', 'optional', 'ostream', 'print', 'queue', 'random', 'ranges', 'ratio', 'rcu',
  'regex', 'scoped_allocator', 'semaphore', 'set', 'shared_mutex', 'simd', 'source_location',
  'span', 'spanstream', 'sstream', 'stack', 'stacktrace', 'stdexcept', 'stdfloat', 'stop_token',
  'streambuf', 'string', 'string_view', 'strstream', 'syncstream', 'system_error', 'text_encoding',
  'thread', 'tuple', 'type_traits', 'typeindex', 'typeinfo', 'unordered_map', 'unordered_set',
  'utility', 'valarray', 'variant', 'vector', 'version',
])

const statsTemplate: TransformStats = {
  chapterNav: 0,
  chapterLink: 0,
  onlineCompilerDemo: 0,
  refLink: 0,
  referenceCard: 0,
  referenceItem: 0,
  talkInfoCard: 0,
  remoteImages: 0,
  internalLinks: 0,
  crossBookLinks: 0,
  paperContext: 0,
}

export interface LinkResolution {
  kind: 'same-book' | 'cross-book' | 'external'
  href: string
  target?: SourceDocument
  fragment?: string
}

export interface TransformContext {
  repositoryRoot: string
  markdown: MarkdownIt
  locale: BookLocale
  assets: AssetManager
  resolveLink: (href: string, from: SourceDocument) => LinkResolution
}

function escapeHtml(value: unknown): string {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;')
}

function attr(element: Element, ...names: string[]): string {
  for (const name of names) {
    const direct = element.getAttribute(name)
    if (direct !== null) return direct
    const bound = element.getAttribute(`:${name}`)
    if (bound !== null) return bound.replace(/^['"]|['"]$/g, '')
  }
  return ''
}

function replaceWithHtml(document: Document, element: Element, html: string): void {
  const template = document.createElement('template')
  template.innerHTML = html
  element.replaceWith(...Array.from(template.content.childNodes))
}

const RAW_TAG_PATTERN = /<\/?([A-Za-z][A-Za-z0-9:.-]*)([^<>]*)>/g
const KNOWN_COMPONENT_PATTERN = KNOWN_COMPONENT_NAMES.join('|')

function isComponentSyntax(raw: string, tag: string, tail: string, markdown: string): boolean {
  if (raw.startsWith('</') || /\/\s*>$/.test(raw)) return true
  if (/['"]|(?:^|\s)(?:[:@#]|v-)?[A-Za-z_][\w:.-]*\s*=/.test(tail)) return true
  if (new RegExp(`<\\/${tag.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\s*>`, 'i').test(markdown)) return true
  return false
}

function looksLikeCppAngleExpression(tag: string, tail: string): boolean {
  const inner = `${tag}${tail}`.replace(/\\$/, '').trim()
  return /^[A-Za-z_][A-Za-z0-9_:,\s*&.()\[\]+\-/]*(?:\.\.\.)?$/.test(inner)
}

function maskCharacters(value: string): string {
  return value.replace(/[^\n]/g, ' ')
}

function markdownFenceOpening(line: string): { marker: string; length: number } | undefined {
  const opening = line.match(/^ {0,3}(`{3,}|~{3,})(.*)$/)
  if (!opening) return undefined
  // CommonMark forbids a backtick in the info string of a backtick fence. If
  // such a line were masked as a fence here while the renderer parsed it as
  // ordinary text, a live component on following lines could evade the audit.
  if (opening[1][0] === '`' && opening[2].includes('`')) return undefined
  return { marker: opening[1][0], length: opening[1].length }
}

/**
 * Preserve source positions while hiding fenced, indented, and inline code.
 * The resulting string can be scanned across line boundaries without treating
 * component examples inside code as live VitePress syntax.
 */
function maskMarkdownCode(markdown: string): string {
  let fence: { marker: string; length: number } | undefined
  const lines = markdown.split('\n').map((line) => {
    if (fence) {
      const closing = new RegExp(`^ {0,3}${fence.marker === '`' ? '`' : '~'}{${fence.length},}[ \\t]*$`)
      if (closing.test(line)) fence = undefined
      return maskCharacters(line)
    }

    const opening = markdownFenceOpening(line)
    if (opening) {
      fence = opening
      return maskCharacters(line)
    }
    if (/^(?: {4}|\t)/.test(line)) return maskCharacters(line)
    return line
  })

  const masked = lines.join('\n').split('')
  for (let index = 0; index < masked.length;) {
    if (masked[index] !== '`') {
      index += 1
      continue
    }
    let escapingBackslashes = 0
    for (let cursor = index - 1; cursor >= 0 && masked[cursor] === '\\'; cursor -= 1) {
      escapingBackslashes += 1
    }
    if (escapingBackslashes % 2 === 1) {
      index += 1
      continue
    }
    let runLength = 1
    while (masked[index + runLength] === '`') runLength += 1
    let closing = index + runLength
    let closingEnd = -1
    while (closing < masked.length) {
      if (masked[closing] !== '`') {
        closing += 1
        continue
      }
      let candidateLength = 1
      while (masked[closing + candidateLength] === '`') candidateLength += 1
      if (candidateLength === runLength) {
        closingEnd = closing + candidateLength
        break
      }
      closing += candidateLength
    }
    if (closingEnd === -1) {
      index += runLength
      continue
    }
    for (let cursor = index; cursor < closingEnd; cursor += 1) {
      if (masked[cursor] !== '\n') masked[cursor] = ' '
    }
    index = closingEnd
  }
  return masked.join('')
}

function auditSourceTags(source: SourceDocument, auditableMarkdown: string): string[] {
  const errors: string[] = []
  for (const match of auditableMarkdown.matchAll(RAW_TAG_PATTERN)) {
    const raw = match[0]
    const original = match[1]
    const tail = match[2]
    const tag = original.toLowerCase()
    const offset = match.index ?? 0
    const lineNumber = auditableMarkdown.slice(0, offset).split('\n').length

    if (tag === 'http:' || tag === 'https:' || tag === 'mailto:') continue
    if (/^<[^<>\s@]+@[^<>\s@]+>$/u.test(raw)) continue

    const canonicalComponent = KNOWN_COMPONENTS.get(tag)
    if (canonicalComponent) {
      if (original !== canonicalComponent) {
        errors.push(
          `${source.repositoryPath}:${lineNumber}: component <${original}> must use <${canonicalComponent}> casing`,
        )
      }
      continue
    }
    if (PASSIVE_HTML.has(tag)) {
      if (/\s(?:on[a-z]+|srcdoc)\s*=/iu.test(tail)) {
        errors.push(`${source.repositoryPath}:${lineNumber}: unsafe HTML attribute on <${original}>`)
      }
      continue
    }

    const lineStart = auditableMarkdown.lastIndexOf('\n', offset - 1) + 1
    const nextLineBreak = auditableMarkdown.indexOf('\n', offset + raw.length)
    const lineEnd = nextLineBreak === -1 ? auditableMarkdown.length : nextLineBreak
    const standalone = auditableMarkdown.slice(lineStart, offset).trim() === ''
      && auditableMarkdown.slice(offset + raw.length, lineEnd).trim() === ''
    const preceding = offset > 0 ? auditableMarkdown[offset - 1] : ''
    const attachedToTemplateName = /[\p{Letter}\p{Number}_:>)]/u.test(preceding)
    const explicitlyEscapedCpp = preceding === '\\' || /\\>$/.test(raw)
    const componentLikeBareTag = !explicitlyEscapedCpp && original.length > 1 && !attachedToTemplateName
      && (original.includes('-') || /^[A-Z]/.test(original))
    const unambiguousCppAngle = explicitlyEscapedCpp
      || attachedToTemplateName
      || original.length === 1
      || CPP_BARE_HEADER_NAMES.has(tag)
      || /[.:/]/u.test(`${original}${tail}`)

    if (
      isComponentSyntax(raw, original, tail, auditableMarkdown)
      || (standalone && original.length > 1 && (original.includes('-') || /^[A-Z]/.test(original)))
      || componentLikeBareTag
      || !unambiguousCppAngle
    ) {
      errors.push(`${source.repositoryPath}:${lineNumber}: unknown component <${original}>`)
      continue
    }
    if (!looksLikeCppAngleExpression(original, tail)) {
      errors.push(`${source.repositoryPath}:${lineNumber}: unknown component <${original}>`)
    }
  }
  return errors
}

/**
 * Audit the Markdown dialect before rendering and escape ambiguous C++ angle
 * expressions such as Number<T>. A bare <T> is indistinguishable from an
 * opening HTML tag in CommonMark, while real Vue components in this project
 * have attributes, a closing tag, or self-closing syntax. Unknown component
 * syntax therefore remains fail-closed.
 */
export function prepareSourceMarkdown(source: SourceDocument): string {
  const auditableMarkdown = maskMarkdownCode(source.markdown)
  const errors = auditSourceTags(source, auditableMarkdown)
  const prepared = source.markdown.replace(
    RAW_TAG_PATTERN,
    (raw, original: string, tail: string, offset: number) => {
      // The audit mask has exactly the same length as the source. A space here
      // means this tag-shaped token belongs to fenced, indented, or inline code
      // and must remain byte-for-byte unchanged, including multiline spans.
      if (auditableMarkdown[offset] !== '<') return raw

      const tag = original.toLowerCase()
      if (tag === 'http:' || tag === 'https:' || tag === 'mailto:') return raw
      if (/^<[^<>\s@]+@[^<>\s@]+>$/u.test(raw)) return raw
      if (KNOWN_COMPONENTS.has(tag) || PASSIVE_HTML.has(tag)) return raw

      // A backslash-escaped opening bracket is already a deliberate CommonMark
      // literal (for example vector\<bool>); let Markdown remove the escape.
      if (offset > 0 && source.markdown[offset - 1] === '\\') return raw
      if (looksLikeCppAngleExpression(original, tail)) {
        const inner = `${original}${tail}`.replace(/\\$/, '').trim()
        return `&lt;${inner}&gt;`
      }
      return raw
    },
  )

  if (errors.length) throw new Error([...new Set(errors)].join('\n'))
  return prepared
}

export function assertKnownSourceTags(source: SourceDocument): void {
  prepareSourceMarkdown(source)
}

function expandSelfClosingComponents(html: string): string {
  // HTML only has a fixed set of void elements. Browsers and HTML parsers do
  // not honor XML-style self-closing syntax for custom elements, so without
  // this normalization <RefLink /> can consume every following sibling.
  const pattern = new RegExp(`<(${KNOWN_COMPONENT_PATTERN})(\\s[^<>]*?)?\\s*\\/>`, 'gi')
  return html.replace(pattern, (_raw, tag: string, attributes = '') => (
    `<${tag}${attributes}></${tag}>`
  ))
}

function sourceCodeBlock(
  document: Document,
  markdown: MarkdownIt,
  source: SourceDocument,
  path: string,
  code: string,
  label: string,
): DocumentFragment {
  const template = document.createElement('template')
  const language = languageForPath(path)
  template.innerHTML = `<section class="online-demo-source"><h4>${escapeHtml(label)}</h4>${renderCodeFence(markdown, code, language, source.relativePath)}</section>`
  return template.content
}

function replaceText(element: Element, pattern: RegExp, replacement: string): number {
  let changes = 0
  const visit = (node: Node): void => {
    if (node.nodeType === 3) {
      const value = node.textContent ?? ''
      const next = value.replace(pattern, (...args) => {
        changes += 1
        return typeof replacement === 'string' ? replacement : args[0]
      })
      if (next !== value) node.textContent = next
      return
    }
    if (node.nodeType !== 1) return
    const tag = (node as Element).tagName.toLowerCase()
    if (tag === 'code' || tag === 'pre' || tag === 'kbd' || tag === 'samp') return
    for (const child of Array.from(node.childNodes)) visit(child)
  }
  visit(element)
  return changes
}

function hasInteractivePaperContext(value: string, locale: BookLocale): boolean {
  return locale.language === 'zh'
    ? /(?:在线(?:运行|编译|编辑)|在浏览器中|点击(?:右侧|下方|下面)|点开(?:下方|下面)|切换到.{0,24}(?:汇编|运行)|编辑并运行)/u.test(value)
    : /(?:(?:run|edit|try|open|click).{0,32}\bonline\b|\bin (?:the )?browser\b|click (?:the )?(?:button|below|right)|switch to .{0,24}assembly)/iu.test(value)
}

function paperizeOnlineDemo(section: Element, locale: BookLocale, stats: TransformStats): void {
  const isChinese = locale.language === 'zh'
  const previous = section.previousElementSibling
  const heading = previous?.tagName.toLowerCase() === 'p'
    ? previous.previousElementSibling
    : previous

  if (heading && /^h[1-6]$/i.test(heading.tagName)) {
    const normalized = (heading.textContent ?? '').replace(/[\u200b\ufeff]/gu, '').trim()
    if (isChinese ? /^(在线运行|在线编译)$/u.test(normalized) : /^(run|try)( it)? online$/i.test(normalized)) {
      heading.textContent = locale.strings.sourceCode
      stats.paperContext += 1
    }
  }

  if (previous?.tagName.toLowerCase() === 'p') {
    if (hasInteractivePaperContext(previous.textContent ?? '', locale)) {
      previous.textContent = isChinese
        ? '以下为配套静态源码；修改参数、执行程序等交互功能请参见在线版。'
        : 'The companion static source follows. Use the online edition for editing, execution, and other interactive features.'
      stats.paperContext += 1
    }
  }

  const description = Array.from(section.children).find((child) => (
    child.tagName.toLowerCase() === 'p' && !child.classList.contains('online-demo-note')
  ))
  if (description) {
    if (isChinese) {
      stats.paperContext += replaceText(description, /在浏览器中编辑并运行/gu, '阅读')
      stats.paperContext += replaceText(description, /在线(?:编译并)?运行并观察/gu, '源码用于观察')
      stats.paperContext += replaceText(description, /在线(?:编译并)?运行/gu, '源码用于演示')
      stats.paperContext += replaceText(description, /切换到\s*ARM\s*汇编查看/giu, '另附 ARM 版本源码，可用于对照')
    } else {
      stats.paperContext += replaceText(description, /edit and run (?:it|this example) in (?:the )?browser/giu, 'read the example source')
      stats.paperContext += replaceText(description, /run online and (?:inspect|observe)/giu, 'use the source to inspect')
      stats.paperContext += replaceText(description, /run online/giu, 'study the source')
      stats.paperContext += replaceText(description, /switch to (?:the )?ARM assembly (?:view )?to (?:inspect|see)/giu, 'compare the accompanying ARM source to')
    }
    if (hasInteractivePaperContext(description.textContent ?? '', locale)) {
      description.textContent = isChinese
        ? '配套静态源码如下；交互执行功能请参见在线版。'
        : 'The companion static source follows; interactive execution is available in the online edition.'
      stats.paperContext += 1
    }
  }
}

function isDisposableNavigationHeading(element: Element | null): boolean {
  // The heading directly attached to a ChapterNav labels that web-only block.
  // Remove any subordinate heading, but never the document's H1/title.
  return Boolean(element && /^h[2-6]$/i.test(element.tagName))
}

async function transformComponents(
  document: Document,
  root: Element,
  source: SourceDocument,
  context: TransformContext,
  stats: TransformStats,
): Promise<void> {
  for (const navigation of Array.from(root.querySelectorAll('chapternav'))) {
    stats.chapterNav += 1
    stats.chapterLink += navigation.querySelectorAll('chapterlink').length
    const navigationHeading = navigation.previousElementSibling
    if (isDisposableNavigationHeading(navigationHeading)) navigationHeading!.remove()
    navigation.remove()
  }

  for (const component of Array.from(root.querySelectorAll('chapterlink'))) {
    stats.chapterLink += 1
    const href = attr(component, 'href')
    replaceWithHtml(document, component, `<a class="chapter-xref" href="${escapeHtml(href)}">${component.innerHTML}</a>`)
  }

  for (const component of Array.from(root.querySelectorAll('onlinecompilerdemo'))) {
    stats.onlineCompilerDemo += 1
    const title = attr(component, 'title') || context.locale.strings.sourceCode
    const description = attr(component, 'description')
    const sourcePath = attr(component, 'source-path', 'sourcePath')
    const armSourcePath = attr(component, 'arm-source-path', 'armSourcePath')
    if (!sourcePath) throw new Error(`${source.repositoryPath}: OnlineCompilerDemo is missing source-path`)

    const paths = [
      { path: sourcePath, label: context.locale.strings.sourceCode },
      ...(armSourcePath && armSourcePath !== sourcePath
        ? [{ path: armSourcePath, label: context.locale.strings.armSourceCode }]
        : []),
    ]
    const section = document.createElement('section')
    section.className = 'online-demo'
    section.innerHTML = `<h3>${escapeHtml(title)}</h3>${description ? `<p>${escapeHtml(description)}</p>` : ''}`
    for (const item of paths) {
      const resolved = resolve(context.repositoryRoot, item.path.replace(/^\/+/, ''))
      const rel = relative(context.repositoryRoot, resolved)
      if (rel.startsWith('..')) throw new Error(`${source.repositoryPath}: demo source escapes repository: ${item.path}`)
      let code: string
      try {
        const canonical = await canonicalRepositoryPath(context.repositoryRoot, resolved, 'Demo source path')
        code = await readFile(canonical, 'utf8')
      } catch (error) {
        throw new Error(`${source.repositoryPath}: cannot read demo source ${item.path}: ${String(error)}`)
      }
      section.append(sourceCodeBlock(document, context.markdown, source, item.path, code, item.label))
    }
    const online = document.createElement('p')
    online.className = 'online-demo-note'
    const onlineUrl = `${context.locale.onlinePrefix.replace(/\/$/, '')}${source.canonicalPath}`
    online.innerHTML = `${escapeHtml(context.locale.strings.onlineEdition)}：<a href="${escapeHtml(onlineUrl)}">${escapeHtml(source.title)}</a>`
    section.append(online)
    component.replaceWith(section)
    paperizeOnlineDemo(section, context.locale, stats)
  }

  for (const component of Array.from(root.querySelectorAll('reflink'))) {
    stats.refLink += 1
    const id = attr(component, 'id')
    const preview = attr(component, 'preview')
    replaceWithHtml(document, component, `<sup class="reference-link"><a href="#${escapeHtml(source.docId)}--ref-${escapeHtml(id)}"${preview ? ` title="${escapeHtml(preview)}"` : ''}>[${escapeHtml(id)}]</a></sup>`)
  }

  const renderReferenceItem = (item: Element): string => {
    stats.referenceItem += 1
    const id = attr(item, 'id')
    const author = attr(item, 'author')
    const title = attr(item, 'title')
    const publisher = attr(item, 'publisher')
    const year = attr(item, 'year')
    const chapter = attr(item, 'chapter')
    const url = attr(item, 'url')
    const quotes = attr(item, 'quotes').split('||').map((quote) => quote.trim()).filter(Boolean)
    const publication = [publisher, year, chapter].filter(Boolean).join(', ')
    const linkedTitle = url ? `<a href="${escapeHtml(url)}"><em>${escapeHtml(title)}</em></a>` : `<em>${escapeHtml(title)}</em>`
    return `<li id="${escapeHtml(source.docId)}--ref-${escapeHtml(id)}"><span class="reference-number">[${escapeHtml(id)}]</span> ${author ? `<strong>${escapeHtml(author)}</strong>. ` : ''}${linkedTitle}${publication ? `. ${escapeHtml(publication)}` : ''}${quotes.map((quote) => `<blockquote>${escapeHtml(quote)}</blockquote>`).join('')}</li>`
  }

  for (const card of Array.from(root.querySelectorAll('referencecard'))) {
    stats.referenceCard += 1
    const title = attr(card, 'title') || context.locale.strings.references
    const items = Array.from(card.querySelectorAll('referenceitem')).map(renderReferenceItem).join('')
    replaceWithHtml(document, card, `<section class="reference-card"><h2>${escapeHtml(title)}</h2><ol>${items}</ol></section>`)
  }
  for (const item of Array.from(root.querySelectorAll('referenceitem'))) {
    replaceWithHtml(document, item, `<ol class="reference-card standalone-reference">${renderReferenceItem(item)}</ol>`)
  }

  for (const card of Array.from(root.querySelectorAll('talkinfocard'))) {
    stats.talkInfoCard += 1
    const talkTitle = attr(card, 'talktitle', 'talk-title', 'talkTitle')
    const speaker = attr(card, 'speaker')
    const conference = attr(card, 'conference')
    const year = attr(card, 'year')
    const links = [
      ['Bilibili', attr(card, 'videobilibili', 'video-bilibili', 'videoBilibili')],
      ['YouTube', attr(card, 'videoyoutube', 'video-youtube', 'videoYoutube')],
      ['Slides', attr(card, 'slidesurl', 'slides-url', 'slidesUrl')],
    ].filter((entry) => entry[1])
    replaceWithHtml(document, card, `<aside class="talk-info-card"><p class="talk-kicker">${escapeHtml(context.locale.strings.lectureResources)} · ${escapeHtml(conference)} ${escapeHtml(year)}</p><p class="talk-title">${escapeHtml(talkTitle)}</p><p>${escapeHtml(speaker)}</p>${links.length ? `<p>${links.map(([label, url]) => `<a href="${escapeHtml(url)}">${label}</a>`).join(' · ')}</p>` : ''}</aside>`)
  }
}

async function transformImages(
  document: Document,
  root: Element,
  source: SourceDocument,
  context: TransformContext,
  stats: TransformStats,
): Promise<void> {
  for (const image of Array.from(root.querySelectorAll('img'))) {
    const raw = image.getAttribute('src') ?? ''
    if (!raw || raw.startsWith('data:')) continue
    if (/^https?:\/\//i.test(raw)) {
      stats.remoteImages += 1
      const alt = image.getAttribute('alt') || raw
      replaceWithHtml(document, image, `<span class="remote-image-placeholder">[${escapeHtml(alt)}：<a href="${escapeHtml(raw)}">${escapeHtml(context.locale.strings.onlineEdition)}</a>]</span>`)
      continue
    }
    const sourceAsset = context.assets.resolveSourceAsset(source.sourcePath, raw)
    const stagedUrl = await context.assets.copyLocalAsset(sourceAsset)
    if (extname(sourceAsset).toLowerCase() === '.drawio') {
      const data = JSON.stringify({ url: stagedUrl, resize: true, nav: false, toolbar: '' })
      const figure = document.createElement('figure')
      figure.className = 'book-figure drawio-figure'
      const diagram = document.createElement('div')
      diagram.className = 'mxgraph drawio-diagram'
      diagram.setAttribute('data-mxgraph', data)
      figure.append(diagram)
      const alt = image.getAttribute('alt') || ''
      if (alt) {
        const caption = document.createElement('figcaption')
        caption.textContent = alt
        figure.append(caption)
      }
      const parent = image.parentElement
      if (
        parent?.tagName.toLowerCase() === 'p'
        && parent.textContent?.trim() === ''
        && parent.children.length === 1
        && parent.firstElementChild === image
      ) {
        parent.replaceWith(figure)
      } else {
        image.replaceWith(figure)
      }
      continue
    }
    image.setAttribute('src', stagedUrl)
    const parent = image.parentElement
    const alt = image.getAttribute('alt') || ''
      if (
        parent?.tagName.toLowerCase() === 'p'
        && parent.textContent?.trim() === ''
        && parent.children.length === 1
        && parent.firstElementChild === image
      ) {
      const figure = document.createElement('figure')
      figure.className = 'book-figure'
      parent.replaceWith(figure)
      figure.append(image)
      if (alt) {
        const caption = document.createElement('figcaption')
        caption.textContent = alt
        figure.append(caption)
      }
    }
  }
}

function normalizeStructure(root: Element): void {
  for (const details of Array.from(root.querySelectorAll('details'))) details.setAttribute('open', '')
  for (const tabs of Array.from(root.querySelectorAll('.vp-code-group .tabs'))) tabs.remove()
  for (const copyButton of Array.from(root.querySelectorAll('button.copy'))) copyButton.remove()
  for (const group of Array.from(root.querySelectorAll('.vp-code-group'))) {
    group.classList.add('book-code-group')
    for (const input of Array.from(group.querySelectorAll('input'))) input.remove()
    for (const block of Array.from(group.querySelectorAll('[class*="language-"]'))) {
      ;(block as HTMLElement).style.display = 'block'
    }
  }
  for (const pre of Array.from(root.querySelectorAll('pre'))) {
    // VitePress marks highlighted code with v-pre for Vue hydration. The book
    // HTML is static, so retaining it would look like an unhandled Vue directive.
    pre.removeAttribute('v-pre')
    pre.classList.add('book-code-block')
    const lines = Array.from(pre.querySelectorAll('code > .line'))
    lines.forEach((line, index) => line.setAttribute('data-line-number', String(index + 1)))
    if (lines.length) pre.setAttribute('data-line-count', String(lines.length))
  }
}

function ensureDocumentTitle(document: Document, root: Element, source: SourceDocument): void {
  const existing = root.querySelector('h1')
  if (existing) {
    if (!(existing.textContent ?? '').replace(/[\u200b\ufeff]/gu, '').trim()) existing.textContent = source.title
    return
  }

  const firstHeading = root.querySelector('h2, h3, h4, h5, h6')
  const compact = (value: string): string => value.normalize('NFKC').toLowerCase().replace(/[^\p{Letter}\p{Number}]+/gu, '')
  const title = document.createElement('h1')
  if (firstHeading && compact(firstHeading.textContent ?? '') === compact(source.title)) {
    for (const attribute of Array.from(firstHeading.attributes)) {
      title.setAttribute(attribute.name, attribute.value)
    }
    title.append(...Array.from(firstHeading.childNodes))
    firstHeading.replaceWith(title)
  } else {
    title.textContent = source.title
    root.prepend(title)
  }
}

function prefixIdentifiers(root: Element, source: SourceDocument): HeadingInfo[] {
  const idMap = new Map<string, string>()
  for (const element of Array.from(root.querySelectorAll('[id]'))) {
    const oldId = element.getAttribute('id')!
    if (oldId.startsWith(`${source.docId}--`)) continue
    const nextId = `${source.docId}--${oldId}`
    if (idMap.has(oldId)) throw new Error(`${source.repositoryPath}: duplicate rendered id ${oldId}`)
    idMap.set(oldId, nextId)
    element.setAttribute('id', nextId)
  }

  const compact = (id: string): string => id.normalize('NFKC').toLowerCase().replace(/[^\p{Letter}\p{Number}]+/gu, '')
  const compactIds = new Map<string, string | undefined>()
  for (const [oldId, nextId] of idMap) {
    const alias = compact(oldId)
    if (!alias) continue
    compactIds.set(alias, compactIds.has(alias) ? undefined : nextId)
  }
  const headingElements = Array.from(root.querySelectorAll('h1, h2, h3, h4, h5, h6'))
  const headingTextIds = new Map<string, string | undefined>()
  for (const heading of headingElements) {
    const id = heading.getAttribute('id')
    const alias = compact(heading.textContent ?? '')
    if (!id || !alias) continue
    headingTextIds.set(alias, headingTextIds.has(alias) ? undefined : id)
  }
  for (const anchor of Array.from(root.querySelectorAll('a[href^="#"]'))) {
    const oldId = decodeURIComponent((anchor.getAttribute('href') ?? '').slice(1))
    // Translated pages occasionally retain a source-language fragment while
    // translating the visible navigation label and heading. If the label maps
    // to exactly one heading, repair that stale fragment deterministically.
    const mapped = idMap.get(oldId)
      ?? compactIds.get(compact(oldId))
      ?? headingTextIds.get(compact(anchor.textContent ?? ''))
    if (mapped) anchor.setAttribute('href', `#${mapped}`)
  }
  const headings: HeadingInfo[] = []
  for (const heading of headingElements) {
    const level = Number(heading.tagName.slice(1))
    const id = heading.getAttribute('id') || `${source.docId}--heading-${headings.length + 1}`
    heading.setAttribute('id', id)
    if (headings.length === 0) heading.classList.add('document-title')
    headings.push({ originalId: idMap.size ? Array.from(idMap.entries()).find(([, value]) => value === id)?.[0] ?? id : id, id, level, text: heading.textContent?.trim() ?? '' })
  }
  return headings
}

function assertNoComponentResidue(root: Element, source: SourceDocument): void {
  const residues: string[] = []
  for (const component of KNOWN_COMPONENTS.keys()) {
    if (root.querySelector(component)) residues.push(component)
  }
  for (const element of Array.from(root.querySelectorAll('*'))) {
    for (const attribute of Array.from(element.attributes)) {
      if (/^(v-|:|@)/.test(attribute.name)) residues.push(`${element.tagName.toLowerCase()}[${attribute.name}]`)
    }
  }
  if (residues.length) throw new Error(`${source.repositoryPath}: unhandled component/Vue residue: ${[...new Set(residues)].join(', ')}`)
}

export async function transformDocument(source: SourceDocument, context: TransformContext): Promise<RenderedDocument> {
  const preparedMarkdown = prepareSourceMarkdown(source)
  const rawHtml = expandSelfClosingComponents(
    context.markdown.render(preparedMarkdown, { relativePath: source.relativePath }),
  )
  const { document } = parseHTML('<!doctype html><html><body><main id="book-fragment"></main></body></html>')
  const root = document.querySelector('#book-fragment')!
  root.innerHTML = rawHtml
  const stats = { ...statsTemplate }

  await transformComponents(document, root, source, context, stats)
  await transformImages(document, root, source, context, stats)
  normalizeStructure(root)
  ensureDocumentTitle(document, root, source)
  const headings = prefixIdentifiers(root, source)
  assertNoComponentResidue(root, source)

  return {
    ...source,
    html: root.innerHTML,
    headings,
    rootAnchor: source.docId,
    endnotes: [],
    stats,
  }
}

export function rewriteDocumentLinks(document: RenderedDocument, resolveLink: TransformContext['resolveLink']): RenderedDocument {
  const parsed = parseHTML('<!doctype html><html><body><main id="book-fragment"></main></body></html>')
  const root = parsed.document.querySelector('#book-fragment')!
  root.innerHTML = document.html
  const endnotes: string[] = []
  const endnoteNumbers = new Map<string, number>()

  for (const anchor of Array.from(root.querySelectorAll('a[href]'))) {
    const href = anchor.getAttribute('href') ?? ''
    if (/^(?:data|javascript):/i.test(href)) {
      throw new Error(`${document.repositoryPath}: unsafe link scheme in "${href}"`)
    }
    if (!href || href.startsWith('#') || /^(?:mailto|tel):/i.test(href)) continue
    const resolution = resolveLink(href, document)
    if (resolution.kind === 'same-book' && resolution.target) {
      const fragment = resolution.fragment
        ? `--${decodeURIComponent(resolution.fragment.replace(/^#/, ''))}`
        : ''
      anchor.setAttribute('href', `#${resolution.target.docId}${fragment}`)
      anchor.classList.add('internal-xref')
      document.stats.internalLinks += 1
    } else if (resolution.kind === 'cross-book') {
      anchor.setAttribute('href', resolution.href)
      anchor.classList.add('cross-book-xref')
      let noteNumber = endnoteNumbers.get(resolution.href)
      if (noteNumber === undefined) {
        noteNumber = endnotes.push(resolution.href)
        endnoteNumbers.set(resolution.href, noteNumber)
      }
      const marker = parsed.document.createElement('sup')
      marker.className = 'online-endnote-marker'
      const noteLink = parsed.document.createElement('a')
      noteLink.setAttribute('href', `#${document.rootAnchor}-endnote-${noteNumber}`)
      noteLink.setAttribute('role', 'doc-noteref')
      noteLink.textContent = `[${noteNumber}]`
      marker.append(noteLink)
      anchor.after(marker)
      document.stats.crossBookLinks += 1
    } else {
      anchor.setAttribute('href', resolution.href)
    }
  }

  document.html = root.innerHTML
  document.endnotes = endnotes
  return document
}

export function emptyTransformStats(): TransformStats {
  return { ...statsTemplate }
}
