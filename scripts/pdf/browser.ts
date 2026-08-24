import { createReadStream } from 'node:fs'
import { lstat, mkdir, realpath, rename, rm, stat } from 'node:fs/promises'
import { createServer, type ServerResponse } from 'node:http'
import type { AddressInfo, Socket } from 'node:net'
import { dirname, extname, isAbsolute, relative, resolve, sep } from 'node:path'
import puppeteer, { type Browser, type Page } from 'puppeteer'

const DEFAULT_TIMEOUT_MS = 15 * 60 * 1000

const MIME_TYPES: Readonly<Record<string, string>> = {
  '.avif': 'image/avif',
  '.css': 'text/css; charset=utf-8',
  '.drawio': 'application/xml; charset=utf-8',
  '.gif': 'image/gif',
  '.htm': 'text/html; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.ico': 'image/x-icon',
  '.jpeg': 'image/jpeg',
  '.jpg': 'image/jpeg',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.otf': 'font/otf',
  '.png': 'image/png',
  '.svg': 'image/svg+xml; charset=utf-8',
  '.ttf': 'font/ttf',
  '.txt': 'text/plain; charset=utf-8',
  '.wasm': 'application/wasm',
  '.webp': 'image/webp',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.xml': 'application/xml; charset=utf-8',
}

export interface PdfBrowserOptions {
  /** Directory containing the assembled HTML and all of its local assets. */
  stagingDir: string
  /** Absolute path or path relative to stagingDir. Defaults to index.html. */
  entryHtml?: string
  outputPath: string
  timeoutMs?: number
  executablePath?: string
  launchArgs?: readonly string[]
}

export interface PdfBrowserResult {
  pageCount: number
  chromeVersion: string
  elapsedMs: number
}

export interface LoopbackStaticServer {
  origin: string
  rootDir: string
  close: () => Promise<void>
}

interface BrowserDiagnostics {
  blockedRequests: string[]
  failedRequests: string[]
  failedResponses: string[]
  pageErrors: string[]
  consoleErrors: string[]
}

interface PaginationState {
  declaredPageCount: number
  pagedPageCount: number
  brokenImages: string[]
}

function sendText(response: ServerResponse, status: number, message: string) {
  response.writeHead(status, {
    'Cache-Control': 'no-store',
    'Content-Type': 'text/plain; charset=utf-8',
    'X-Content-Type-Options': 'nosniff',
  })
  response.end(message)
}

function isInside(rootDir: string, candidate: string): boolean {
  const rel = relative(rootDir, candidate)
  return rel === '' || (!rel.startsWith(`..${sep}`) && rel !== '..' && !isAbsolute(rel))
}

async function resolveServedFile(rootDir: string, pathname: string): Promise<string | undefined> {
  let decoded: string
  try {
    decoded = decodeURIComponent(pathname)
  } catch {
    return undefined
  }

  if (decoded.includes('\0')) return undefined
  const candidate = resolve(rootDir, decoded.replace(/^[/\\]+/, ''))
  if (!isInside(rootDir, candidate)) return undefined

  let candidateStat
  try {
    candidateStat = await lstat(candidate)
  } catch {
    return undefined
  }

  const selected = candidateStat.isDirectory() ? resolve(candidate, 'index.html') : candidate
  let selectedStat
  let canonical
  try {
    selectedStat = await stat(selected)
    canonical = await realpath(selected)
  } catch {
    return undefined
  }

  if (!selectedStat.isFile() || !isInside(rootDir, canonical)) return undefined
  return canonical
}

/**
 * Serve a staging tree without exposing it on a LAN interface. Symlinks that
 * escape the staging root and path-traversal requests are rejected.
 */
export async function startLoopbackStaticServer(stagingDir: string): Promise<LoopbackStaticServer> {
  const rootDir = await realpath(resolve(stagingDir))
  const sockets = new Set<Socket>()

  const server = createServer(async (request, response) => {
    try {
      if (request.method !== 'GET' && request.method !== 'HEAD') {
        response.setHeader('Allow', 'GET, HEAD')
        sendText(response, 405, 'Method not allowed')
        return
      }

      const requestUrl = new URL(request.url ?? '/', 'http://127.0.0.1')
      const filePath = await resolveServedFile(rootDir, requestUrl.pathname)
      if (!filePath) {
        sendText(response, 404, 'Not found')
        return
      }

      const fileStat = await stat(filePath)
      response.writeHead(200, {
        'Cache-Control': 'no-store',
        'Content-Length': String(fileStat.size),
        'Content-Type': MIME_TYPES[extname(filePath).toLowerCase()] ?? 'application/octet-stream',
        'X-Content-Type-Options': 'nosniff',
      })
      if (request.method === 'HEAD') {
        response.end()
        return
      }

      const stream = createReadStream(filePath)
      stream.on('error', (error) => response.destroy(error))
      stream.pipe(response)
    } catch (error) {
      if (!response.headersSent) sendText(response, 500, 'Internal server error')
      else response.destroy(error instanceof Error ? error : undefined)
    }
  })

  server.on('connection', (socket) => {
    sockets.add(socket)
    socket.once('close', () => sockets.delete(socket))
  })

  await new Promise<void>((resolveListen, rejectListen) => {
    const onError = (error: Error) => {
      server.off('listening', onListening)
      rejectListen(error)
    }
    const onListening = () => {
      server.off('error', onError)
      resolveListen()
    }
    server.once('error', onError)
    server.once('listening', onListening)
    server.listen(0, '127.0.0.1')
  })

  const address = server.address() as AddressInfo | null
  if (!address || address.address !== '127.0.0.1') {
    server.close()
    throw new Error('Static server did not bind to 127.0.0.1')
  }

  let closed = false
  return {
    origin: `http://127.0.0.1:${address.port}`,
    rootDir,
    close: async () => {
      if (closed) return
      closed = true
      await new Promise<void>((resolveClose, rejectClose) => {
        server.close((error) => error ? rejectClose(error) : resolveClose())
        for (const socket of sockets) socket.destroy()
      })
    },
  }
}

function isAllowedRequestUrl(rawUrl: string, allowedOrigin: string): boolean {
  let url: URL
  try {
    url = new URL(rawUrl)
  } catch {
    return false
  }

  if (url.protocol === 'about:' || url.protocol === 'blob:' || url.protocol === 'data:') return true
  return url.origin === allowedOrigin
}

function entryUrl(server: LoopbackStaticServer, entryHtml: string): string {
  const absoluteEntry = isAbsolute(entryHtml) ? resolve(entryHtml) : resolve(server.rootDir, entryHtml)
  if (!isInside(server.rootDir, absoluteEntry)) {
    throw new Error(`Entry HTML is outside the staging directory: ${entryHtml}`)
  }
  const rel = relative(server.rootDir, absoluteEntry)
  const encodedPath = rel.split(sep).map(encodeURIComponent).join('/')
  return `${server.origin}/${encodedPath}`
}

/**
 * The vendored draw.io GraphViewer decodes real-world diagrams leniently: it
 * logs "Could not add object ..." console errors for geometry it cannot
 * attach, then renders the diagram anyway. Diagram completeness is enforced
 * separately by the runtime's per-diagram SVG assertions, so only these
 * exact messages from the vendored script are tolerated. Everything else —
 * including these messages from any other script — stays fatal.
 */
function isToleratedDrawioDecodeLog(text: string, url: string): boolean {
  return /^Could not add object\b/.test(text) && url.includes('/vendor/drawio-viewer-')
}

function installDiagnostics(page: Page, diagnostics: BrowserDiagnostics, allowedOrigin: string) {
  page.on('console', (message) => {
    if (message.type() !== 'error') return
    const location = message.location()
    const url = location.url ?? ''
    if (isToleratedDrawioDecodeLog(message.text(), url)) return
    const source = url ? ` (${url}:${location.lineNumber ?? 0})` : ''
    diagnostics.consoleErrors.push(`${message.text()}${source}`)
  })
  page.on('pageerror', (value) => {
    const message = value instanceof Error ? value.stack ?? value.message : String(value)
    diagnostics.pageErrors.push(message)
  })
  page.on('requestfailed', (request) => {
    const failure = request.failure()?.errorText ?? 'unknown request failure'
    diagnostics.failedRequests.push(`${request.method()} ${request.url()} — ${failure}`)
  })
  page.on('response', (response) => {
    if (response.status() >= 400) {
      diagnostics.failedResponses.push(`${response.status()} ${response.request().method()} ${response.url()}`)
    }
  })
  page.on('request', (request) => {
    if (isAllowedRequestUrl(request.url(), allowedOrigin)) {
      void request.continue().catch((error) => {
        diagnostics.failedRequests.push(`${request.method()} ${request.url()} — ${String(error)}`)
      })
      return
    }

    diagnostics.blockedRequests.push(`${request.method()} ${request.url()} (${request.resourceType()})`)
    void request.abort('blockedbyclient').catch((error) => {
      diagnostics.failedRequests.push(`${request.method()} ${request.url()} — ${String(error)}`)
    })
  })
}

function formatDiagnostics(diagnostics: BrowserDiagnostics): string {
  const groups: Array<[string, string[]]> = [
    ['blocked external requests', diagnostics.blockedRequests],
    ['failed requests', diagnostics.failedRequests],
    ['HTTP error responses', diagnostics.failedResponses],
    ['page errors', diagnostics.pageErrors],
    ['console errors', diagnostics.consoleErrors],
  ]
  return groups
    .filter(([, values]) => values.length > 0)
    .map(([label, values]) => `${label}:\n${values.map((value) => `  - ${value}`).join('\n')}`)
    .join('\n')
}

function assertNoDiagnostics(diagnostics: BrowserDiagnostics) {
  const details = formatDiagnostics(diagnostics)
  if (details) throw new Error(`Browser rendering was not clean:\n${details}`)
}

async function waitForBookReady(page: Page, timeoutMs: number): Promise<PaginationState> {
  await page.waitForFunction(
    () => Object.prototype.hasOwnProperty.call(globalThis, '__BOOK_READY__'),
    { timeout: timeoutMs },
  )

  await Promise.race([
    page.evaluate(async () => {
      const ready = (globalThis as typeof globalThis & { __BOOK_READY__?: unknown }).__BOOK_READY__
      if (ready === true) return
      if (!ready || typeof (ready as PromiseLike<unknown>).then !== 'function') {
        throw new Error('window.__BOOK_READY__ must be true or a Promise')
      }
      await ready
    }),
    new Promise<never>((_, reject) => {
      const timer = setTimeout(() => reject(new Error(`window.__BOOK_READY__ timed out after ${timeoutMs}ms`)), timeoutMs)
      timer.unref()
    }),
  ])

  return page.evaluate(async () => {
    await document.fonts.ready

    const declaredNode = document.querySelector<HTMLElement>('[data-page-count]')
    const declaredRaw = declaredNode?.dataset.pageCount
    const declaredPageCount = Number.parseInt(declaredRaw ?? '', 10)
    const pagedPageCount = document.querySelectorAll('.pagedjs_page').length
    const brokenImages = Array.from(document.images)
      .filter((image) => !image.complete || image.naturalWidth === 0)
      .map((image) => image.currentSrc || image.src || image.alt || '<unnamed image>')

    return { declaredPageCount, pagedPageCount, brokenImages }
  })
}

function assertPagination(state: PaginationState) {
  if (!Number.isSafeInteger(state.declaredPageCount) || state.declaredPageCount < 1) {
    throw new Error('Missing or invalid data-page-count after pagination')
  }
  if (state.pagedPageCount < 1) throw new Error('Paged.js produced no .pagedjs_page elements')
  if (state.declaredPageCount !== state.pagedPageCount) {
    throw new Error(
      `Pagination count mismatch: data-page-count=${state.declaredPageCount}, .pagedjs_page=${state.pagedPageCount}`,
    )
  }
  if (state.brokenImages.length > 0) {
    throw new Error(`Broken images after pagination:\n${state.brokenImages.map((url) => `  - ${url}`).join('\n')}`)
  }
}

function launchArguments(additional: readonly string[] | undefined): string[] {
  const args = ['--disable-dev-shm-usage', '--font-render-hinting=none']
  if (typeof process.getuid === 'function' && process.getuid() === 0) {
    args.push('--no-sandbox', '--disable-setuid-sandbox')
  }
  if (additional) args.push(...additional)
  return [...new Set(args)]
}

/** Export an already assembled, self-contained book HTML using Chromium. */
export async function exportBookPdf(options: PdfBrowserOptions): Promise<PdfBrowserResult> {
  const startedAt = Date.now()
  const timeoutMs = options.timeoutMs ?? DEFAULT_TIMEOUT_MS
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) throw new Error(`Invalid timeout: ${timeoutMs}`)

  const server = await startLoopbackStaticServer(options.stagingDir)
  const diagnostics: BrowserDiagnostics = {
    blockedRequests: [],
    failedRequests: [],
    failedResponses: [],
    pageErrors: [],
    consoleErrors: [],
  }
  const outputPath = resolve(options.outputPath)
  const temporaryPath = `${outputPath}.tmp-${process.pid}-${Date.now()}`
  let browser: Browser | undefined
  let page: Page | undefined

  try {
    const absoluteEntry = isAbsolute(options.entryHtml ?? '')
      ? resolve(options.entryHtml as string)
      : resolve(server.rootDir, options.entryHtml ?? 'index.html')
    const entryStat = await stat(absoluteEntry)
    if (!entryStat.isFile()) throw new Error(`Entry HTML is not a file: ${absoluteEntry}`)

    browser = await puppeteer.launch({
      headless: true,
      executablePath: options.executablePath,
      args: launchArguments(options.launchArgs),
      timeout: timeoutMs,
    })
    page = await browser.newPage()
    page.setDefaultNavigationTimeout(timeoutMs)
    page.setDefaultTimeout(timeoutMs)
    await page.setViewport({ width: 1440, height: 900, deviceScaleFactor: 1 })
    await page.emulateMediaType('print')
    await page.setRequestInterception(true)
    installDiagnostics(page, diagnostics, server.origin)

    await page.goto(entryUrl(server, absoluteEntry), { waitUntil: 'networkidle0', timeout: timeoutMs })
    const pagination = await waitForBookReady(page, timeoutMs)
    assertPagination(pagination)
    assertNoDiagnostics(diagnostics)

    await mkdir(dirname(outputPath), { recursive: true })
    const pdf = await page.pdf({
      path: temporaryPath,
      displayHeaderFooter: false,
      preferCSSPageSize: true,
      printBackground: true,
      waitForFonts: true,
      timeout: timeoutMs,
    })
    if (pdf.byteLength < 5 || String.fromCharCode(...pdf.subarray(0, 5)) !== '%PDF-') {
      throw new Error('Chromium returned an invalid PDF payload')
    }

    assertNoDiagnostics(diagnostics)
    await rename(temporaryPath, outputPath)
    return {
      pageCount: pagination.pagedPageCount,
      chromeVersion: await browser.version(),
      elapsedMs: Date.now() - startedAt,
    }
  } catch (error) {
    const details = formatDiagnostics(diagnostics)
    const message = error instanceof Error ? error.message : String(error)
    const combined = details && !message.includes(details) ? `${message}\n${details}` : message
    throw new Error(combined, { cause: error })
  } finally {
    if (page) await page.close().catch(() => undefined)
    if (browser) await browser.close().catch(() => undefined)
    await server.close().catch(() => undefined)
    await rm(temporaryPath, { force: true }).catch(() => undefined)
  }
}
