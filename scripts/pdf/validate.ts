import { spawn } from 'node:child_process'
import { access, open, stat } from 'node:fs/promises'
import { parseHTML } from 'linkedom'
import type { RenderedDocument } from './model'

const COMPONENT_NAMES = [
  'ChapterNav', 'ChapterLink', 'OnlineCompilerDemo', 'RefLink',
  'ReferenceCard', 'ReferenceItem', 'TalkInfoCard',
]

const SAFE_FRAGMENT_HTML = new Set([
  'a', 'abbr', 'aside', 'b', 'bdi', 'bdo', 'blockquote', 'br', 'caption', 'cite', 'code',
  'col', 'colgroup', 'dd', 'del', 'details', 'dfn', 'div', 'dl', 'dt', 'em', 'figcaption',
  'figure', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'hr', 'i', 'img', 'ins', 'kbd', 'li',
  'mark', 'ol', 'p', 'picture', 'pre', 'q', 'rp', 'rt', 'ruby', 's', 'samp', 'section',
  'small', 'source', 'span', 'strong', 'sub', 'summary', 'sup', 'table', 'tbody', 'td',
  'tfoot', 'th', 'thead', 'time', 'tr', 'u', 'ul', 'var', 'wbr',
])

// MathJax output is trusted only inside the renderer-created container. Keep
// this list broad enough for future valid MathML while still excluding active
// HTML/SVG nodes such as script, foreignObject, iframe, and style.
const SAFE_MATHML = new Set([
  'annotation', 'annotation-xml', 'maction', 'math', 'menclose', 'mfenced', 'mfrac',
  'mglyph', 'mi', 'mlabeledtr', 'mlongdiv', 'mmultiscripts', 'mn', 'mo', 'mover', 'mpadded',
  'mphantom', 'mprescripts', 'mroot', 'mrow', 'ms', 'mscarries', 'mscarry', 'msgroup',
  'msline', 'mspace', 'msqrt', 'msrow', 'mstack', 'mstyle', 'msub', 'msubsup', 'msup',
  'mtable', 'mtd', 'mtext', 'mtr', 'munder', 'munderover', 'none', 'semantics',
])

const SAFE_MATHJAX_SVG = new Set([
  'circle', 'clippath', 'defs', 'ellipse', 'g', 'line', 'mask', 'path', 'polygon', 'polyline',
  'rect', 'svg', 'text', 'title', 'tspan', 'use',
])

const XHTML_NAMESPACE = 'http://www.w3.org/1999/xhtml'
const SVG_NAMESPACE = 'http://www.w3.org/2000/svg'

function describeElement(element: Element): string {
  const id = element.getAttribute('id') ? `#${element.getAttribute('id')}` : ''
  const classes = (element.getAttribute('class') ?? '').trim().split(/\s+/).filter(Boolean)
  return `<${element.localName}${id}${classes.map((name) => `.${name}`).join('')}>`
}

function mathJaxContainer(element: Element): Element | null {
  const container = element.closest('mjx-container.MathJax')
  return container?.getAttribute('jax') === 'SVG' ? container : null
}

function assertSafeAttribute(element: Element, attribute: Attr, source: RenderedDocument): void {
  const name = attribute.name.toLowerCase()
  const value = attribute.value.trim()
  if (/^on[a-z]+$/u.test(name) || name === 'srcdoc' || name === 'hidden' || name === 'inert') {
    throw new Error(`${source.repositoryPath}: unsafe rendered attribute ${name} on ${describeElement(element)}`)
  }
  if (name === 'data-mjx-error' || (name === 'class' && /(?:^|\s)mjx-merror(?:\s|$)/u.test(value))) {
    throw new Error(`${source.repositoryPath}: MathJax reported a formula error on ${describeElement(element)}`)
  }
  if (/^(?:v-|:|@)/u.test(name)) {
    throw new Error(`${source.repositoryPath}: unhandled Vue attribute ${name} on ${describeElement(element)}`)
  }
  if (name === 'srcset' || name === 'poster' || name === 'action' || name === 'formaction') {
    throw new Error(`${source.repositoryPath}: unsupported rendered URL attribute ${name} on ${describeElement(element)}`)
  }
  if (name === 'style') {
    if (/(?:url\s*\(|@import)/iu.test(value)) {
      throw new Error(`${source.repositoryPath}: URL-bearing inline style on ${describeElement(element)}`)
    }
    // MathJax intentionally clips its assistive accessibility copy. Outside a
    // renderer-owned MathJax container, inline layout suppression can make a
    // document look non-empty to the DOM checks while silently hiding text in
    // print. Generated Shiki colors/font-style and code-group display:block do
    // not match this denylist.
    const suppressesPrint = [
      /(?:^|;)\s*display\s*:\s*none(?:\s*!important)?\s*(?:;|$)/iu,
      /(?:^|;)\s*visibility\s*:\s*(?:hidden|collapse)\b/iu,
      /(?:^|;)\s*(?:height|min-height|max-height|content-visibility|clip|clip-path|transform|filter)\s*:/iu,
      /(?:^|;)\s*overflow(?:-y)?\s*:\s*(?:hidden|clip)\b/iu,
      /(?:^|;)\s*position\s*:\s*(?:absolute|fixed)\b/iu,
      /(?:^|;)\s*(?:opacity|font-size|line-height)\s*:\s*0(?:[^\d.]|$)/iu,
      /(?:^|;)\s*(?:width|max-width)\s*:\s*0(?:[^\d.]|$)/iu,
      /(?:^|;)\s*color\s*:\s*transparent\b/iu,
    ].some((pattern) => pattern.test(value))
    if (mathJaxContainer(element) === null && suppressesPrint) {
      throw new Error(`${source.repositoryPath}: print-suppressing inline style on ${describeElement(element)}`)
    }
  }
  if (name === 'href' || name === 'xlink:href') {
    if (/^(?:data|file|javascript|vbscript):/iu.test(value)) {
      throw new Error(`${source.repositoryPath}: unsafe rendered link "${value}" on ${describeElement(element)}`)
    }
    if (element.namespaceURI === SVG_NAMESPACE && value && !value.startsWith('#')) {
      throw new Error(`${source.repositoryPath}: non-local SVG link "${value}" on ${describeElement(element)}`)
    }
  }
  if (name === 'src') {
    if (element.localName !== 'img') {
      throw new Error(`${source.repositoryPath}: unprocessed src on ${describeElement(element)}`)
    }
    const isBookAsset = value.startsWith('/')
    const isInlineImage = /^data:image\/(?:avif|gif|jpeg|png|svg\+xml|webp)(?:;|,)/iu.test(value)
    if (!isBookAsset && !isInlineImage) {
      throw new Error(`${source.repositoryPath}: unsafe or unprocessed image src "${value}"`)
    }
  }
}

function assertSafeRenderedFragment(root: Element, source: RenderedDocument): void {
  for (const element of Array.from(root.querySelectorAll('*'))) {
    const tag = element.localName.toLowerCase()
    const mathJax = mathJaxContainer(element)
    const isHtml = element.namespaceURI === XHTML_NAMESPACE && SAFE_FRAGMENT_HTML.has(tag)
    const isMathJaxWrapper = (tag === 'mjx-container' || tag === 'mjx-assistive-mml') && mathJax !== null
    const isMathMl = SAFE_MATHML.has(tag) && mathJax !== null
    const isMathSvg = element.namespaceURI === SVG_NAMESPACE
      && SAFE_MATHJAX_SVG.has(tag)
      && mathJax !== null

    if (!isHtml && !isMathJaxWrapper && !isMathMl && !isMathSvg) {
      throw new Error(`${source.repositoryPath}: unsafe or unknown rendered element ${describeElement(element)}`)
    }
    for (const attribute of Array.from(element.attributes)) {
      assertSafeAttribute(element, attribute, source)
    }
  }
}

function command(command: string, args: string[]): Promise<{ code: number; stdout: string; stderr: string }> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { stdio: ['ignore', 'pipe', 'pipe'] })
    let stdout = ''
    let stderr = ''
    child.stdout.setEncoding('utf8').on('data', (chunk) => { stdout += chunk })
    child.stderr.setEncoding('utf8').on('data', (chunk) => { stderr += chunk })
    child.on('error', reject)
    child.on('close', (code) => resolve({ code: code ?? 1, stdout, stderr }))
  })
}

async function hasCommand(name: string): Promise<boolean> {
  try {
    const result = await command('sh', ['-c', `command -v "$1" >/dev/null 2>&1`, 'sh', name])
    return result.code === 0
  } catch {
    return false
  }
}

export function validateRenderedDocuments(documents: RenderedDocument[]): void {
  const ids = new Map<string, string>()
  const links: Array<{ source: string; target: string }> = []

  for (const source of documents) {
    const { document } = parseHTML(`<!doctype html><html><body><section id="${source.rootAnchor}">${source.html}</section></body></html>`)
    const root = document.body.firstElementChild!
    assertSafeRenderedFragment(root, source)
    const titles = Array.from(root.querySelectorAll('h1'))
      .filter((heading) => (heading.textContent ?? '').replace(/[\u200b\ufeff]/gu, '').trim())
    if (!titles.length) {
      throw new Error(`${source.repositoryPath}: rendered document has no usable h1 title`)
    }
    if (!(root.textContent ?? '').replace(/[\s\u200b\ufeff]/gu, '')) {
      throw new Error(`${source.repositoryPath}: rendered document has no visible content`)
    }
    for (const element of [root, ...Array.from(root.querySelectorAll('[id]'))]) {
      const id = element.getAttribute('id')!
      const previous = ids.get(id)
      if (previous) throw new Error(`Duplicate book id "${id}" in ${previous} and ${source.repositoryPath}`)
      ids.set(id, source.repositoryPath)
    }
    for (let index = 0; index < source.endnotes.length; index += 1) {
      const id = `${source.rootAnchor}-endnote-${index + 1}`
      const previous = ids.get(id)
      if (previous) throw new Error(`Duplicate book id "${id}" in ${previous} and ${source.repositoryPath}`)
      ids.set(id, source.repositoryPath)
    }
    for (const anchor of Array.from(root.querySelectorAll('a[href^="#"]'))) {
      links.push({ source: source.repositoryPath, target: decodeURIComponent(anchor.getAttribute('href')!.slice(1)) })
    }
    for (const name of COMPONENT_NAMES) {
      if (source.html.toLowerCase().includes(`<${name.toLowerCase()}`)) {
        throw new Error(`${source.repositoryPath}: unhandled ${name} tag remains in rendered HTML`)
      }
    }
  }

  for (const link of links) {
    if (!ids.has(link.target)) throw new Error(`${link.source}: missing in-book anchor #${link.target}`)
  }
}

export interface PdfPostflight {
  bytes: number
  pageCount?: number
  warnings: string[]
}

export async function postflightPdf(
  pdfPath: string,
  options: { expectCjk?: boolean } = {},
): Promise<PdfPostflight> {
  await access(pdfPath)
  const info = await stat(pdfPath)
  if (info.size < 1024) throw new Error(`Generated PDF is unexpectedly small (${info.size} bytes)`)
  const signatureBytes = Buffer.alloc(5)
  const handle = await open(pdfPath, 'r')
  try {
    const { bytesRead } = await handle.read(signatureBytes, 0, signatureBytes.length, 0)
    if (bytesRead !== signatureBytes.length) throw new Error(`Generated PDF is truncated: ${pdfPath}`)
  } finally {
    await handle.close()
  }
  const signature = signatureBytes.toString('ascii')
  if (signature !== '%PDF-') throw new Error(`Generated file is not a PDF: ${pdfPath}`)

  const warnings: string[] = []
  let pageCount: number | undefined

  if (await hasCommand('qpdf')) {
    const checked = await command('qpdf', ['--check', pdfPath])
    if (checked.code !== 0) throw new Error(`qpdf validation failed:\n${checked.stderr || checked.stdout}`)
  } else {
    warnings.push('qpdf is unavailable; structural PDF validation was skipped')
  }

  if (await hasCommand('pdfinfo')) {
    const result = await command('pdfinfo', [pdfPath])
    if (result.code !== 0) throw new Error(`pdfinfo failed:\n${result.stderr}`)
    const pages = result.stdout.match(/^Pages:\s+(\d+)/m)
    if (!pages || Number(pages[1]) < 1) throw new Error('pdfinfo returned no valid PDF page count')
    pageCount = Number(pages[1])
  } else {
    warnings.push('pdfinfo is unavailable; PDF page-count validation was skipped')
  }

  if (await hasCommand('pdffonts')) {
    const result = await command('pdffonts', [pdfPath])
    if (result.code !== 0) throw new Error(`pdffonts failed:\n${result.stderr}`)
    const fontRows = result.stdout.split('\n').slice(2).filter((line) => line.trim())
    if (!fontRows.length) throw new Error('PDF contains no detectable fonts')
    const bad = fontRows.filter((line) => {
      const fields = line.trim().split(/\s+/)
      // pdffonts ends every row with: emb, sub, uni, object, ID. Font type
      // itself may contain spaces, so index from the right rather than left.
      return fields.at(-5) !== 'yes'
    })
    if (bad.length) {
      throw new Error(`PDF contains ${bad.length} non-embedded font row(s):\n${bad.join('\n')}`)
    }
    if (options.expectCjk) {
      const expectedSerif = /(?:NotoSerif(?:CJK|SC)|SourceHanSerif)/iu
      if (!fontRows.some((line) => expectedSerif.test(line.split(/\s+/u)[0] ?? ''))) {
        throw new Error('PDF does not contain the expected embedded Noto/Source Han CJK serif body font')
      }
    }
  } else {
    warnings.push('pdffonts is unavailable; font embedding validation was skipped')
  }

  if (await hasCommand('pdftotext')) {
    const result = await command('pdftotext', ['-layout', pdfPath, '-'])
    if (result.code !== 0) throw new Error(`pdftotext failed:\n${result.stderr}`)
    if (options.expectCjk && !/[\u3400-\u9fff]/u.test(result.stdout)) {
      warnings.push('Extracted PDF text contains no CJK characters')
    }
    const residue = COMPONENT_NAMES.find((name) => result.stdout.includes(name))
    if (residue) throw new Error(`PDF text contains unhandled component name ${residue}`)
    const blankTextPages = result.stdout.split('\f').filter((page) => !page.trim()).length
    if (blankTextPages) warnings.push(`${blankTextPages} PDF page(s) contain no extractable text; inspect image-only/blank pages`)
  } else {
    warnings.push('pdftotext is unavailable; text extraction validation was skipped')
  }

  return { bytes: info.size, pageCount, warnings }
}
