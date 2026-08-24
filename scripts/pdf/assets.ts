import { createHash } from 'node:crypto'
import { access, copyFile, cp, mkdir, readFile, writeFile } from 'node:fs/promises'
import { basename, dirname, extname, isAbsolute, join, relative, resolve, sep } from 'node:path'
import { canonicalRepositoryPath } from './path-safety'

const DRAWIO_VIEWER_VERSION = '31.3.2'
const DRAWIO_VIEWER_SHA256 = '2fabaaa3e28d5f80f943285a2ce19c22cf870857203255f1e0347ef93693a297'
const DRAWIO_VIEWER_URL = `https://raw.githubusercontent.com/jgraph/drawio/v${DRAWIO_VIEWER_VERSION}/src/main/webapp/js/viewer-static.min.js`

async function exists(path: string): Promise<boolean> {
  try {
    await access(path)
    return true
  } catch {
    return false
  }
}

function sha256(data: Uint8Array | string): string {
  return createHash('sha256').update(data).digest('hex')
}

function assertInside(root: string, path: string): void {
  const rel = relative(resolve(root), resolve(path))
  if (rel === '..' || rel.startsWith(`..${sep}`) || isAbsolute(rel)) {
    throw new Error(`Asset path escapes the repository: ${path}`)
  }
}

export interface StagedRuntimeAssets {
  stylesheet: string
  runtimeScript: string
  pagedPolyfillScript: string
  mermaidModule: string
  drawioViewerScript?: string
}

export class AssetManager {
  readonly repositoryRoot: string
  readonly stagingDir: string
  readonly assetDir: string
  readonly vendorDir: string
  readonly warnings: string[] = []
  #copied = new Map<string, string>()

  constructor(repositoryRoot: string, stagingDir: string) {
    this.repositoryRoot = resolve(repositoryRoot)
    this.stagingDir = resolve(stagingDir)
    this.assetDir = join(this.stagingDir, 'assets')
    this.vendorDir = join(this.stagingDir, 'vendor')
  }

  async initialize(options: { needsDrawio: boolean }): Promise<StagedRuntimeAssets> {
    await mkdir(this.assetDir, { recursive: true })
    await mkdir(this.vendorDir, { recursive: true })

    const pdfRoot = join(this.repositoryRoot, 'scripts', 'pdf')
    const stylesheet = join(this.stagingDir, 'book.css')
    const runtimeScript = join(this.stagingDir, 'book-runtime.js')
    await copyFile(join(pdfRoot, 'styles', 'book.css'), stylesheet)
    await copyFile(join(pdfRoot, 'runtime', 'book-runtime.js'), runtimeScript)

    const pagedTarget = join(this.vendorDir, 'paged.polyfill.js')
    await copyFile(join(this.repositoryRoot, 'node_modules', 'pagedjs', 'dist', 'paged.polyfill.js'), pagedTarget)

    const mermaidTarget = join(this.vendorDir, 'mermaid')
    await cp(join(this.repositoryRoot, 'node_modules', 'mermaid', 'dist'), mermaidTarget, { recursive: true })

    let drawioViewerScript: string | undefined
    if (options.needsDrawio) {
      const source = await this.#ensureDrawioViewer()
      const target = join(this.vendorDir, `drawio-viewer-${DRAWIO_VIEWER_VERSION}.min.js`)
      await copyFile(source, target)
      drawioViewerScript = `/${basename(target)}`.replace(/^\//, '/vendor/')
    }

    return {
      stylesheet: '/book.css',
      runtimeScript: '/book-runtime.js',
      pagedPolyfillScript: '/vendor/paged.polyfill.js',
      mermaidModule: '/vendor/mermaid/mermaid.esm.min.mjs',
      drawioViewerScript,
    }
  }

  async copyLocalAsset(sourcePath: string): Promise<string> {
    const lexicalSource = resolve(sourcePath)
    assertInside(this.repositoryRoot, lexicalSource)
    if (!(await exists(lexicalSource))) {
      throw new Error(`Missing local asset: ${relative(this.repositoryRoot, lexicalSource)}`)
    }
    let source: string
    try {
      source = await canonicalRepositoryPath(this.repositoryRoot, lexicalSource, 'Asset path')
    } catch (error) {
      throw new Error(`Unsafe local asset ${relative(this.repositoryRoot, lexicalSource)}: ${String(error)}`)
    }
    const cached = this.#copied.get(source)
    if (cached) return cached

    const relativePath = relative(this.repositoryRoot, source).replaceAll(sep, '/')
    const digest = sha256(relativePath).slice(0, 12)
    const safeName = basename(source).replace(/[^A-Za-z0-9._-]+/g, '-')
    const targetName = `${digest}-${safeName}`
    await copyFile(source, join(this.assetDir, targetName))
    const url = `/assets/${targetName}`
    this.#copied.set(source, url)
    return url
  }

  resolveSourceAsset(fromMarkdown: string, rawPath: string): string {
    const withoutQuery = decodeURIComponent(rawPath.split(/[?#]/, 1)[0])
    const candidate = withoutQuery.startsWith('/')
      ? resolve(this.repositoryRoot, withoutQuery.replace(/^\/+/, ''))
      : resolve(dirname(fromMarkdown), withoutQuery)
    assertInside(this.repositoryRoot, candidate)
    return candidate
  }

  async #ensureDrawioViewer(): Promise<string> {
    const override = process.env.DRAWIO_VIEWER_PATH
    const cachePath = override
      ? resolve(override)
      : join(this.repositoryRoot, '.cache', 'pdf', `drawio-viewer-${DRAWIO_VIEWER_VERSION}.min.js`)

    if (await exists(cachePath)) {
      const bytes = await readFile(cachePath)
      if (sha256(bytes) !== DRAWIO_VIEWER_SHA256) {
        throw new Error(`draw.io viewer checksum mismatch: ${cachePath}`)
      }
      return cachePath
    }

    await mkdir(dirname(cachePath), { recursive: true })
    let response: Response
    try {
      response = await fetch(DRAWIO_VIEWER_URL, { redirect: 'follow' })
    } catch (error) {
      throw new Error(
        `Unable to download pinned draw.io viewer ${DRAWIO_VIEWER_VERSION}. `
        + `Set DRAWIO_VIEWER_PATH to a cached file. ${String(error)}`,
      )
    }
    if (!response.ok) throw new Error(`draw.io viewer download failed: HTTP ${response.status}`)
    const bytes = new Uint8Array(await response.arrayBuffer())
    const actual = sha256(bytes)
    if (actual !== DRAWIO_VIEWER_SHA256) {
      throw new Error(`draw.io viewer checksum mismatch: expected ${DRAWIO_VIEWER_SHA256}, got ${actual}`)
    }
    await writeFile(cachePath, bytes)
    return cachePath
  }
}

export function languageForPath(path: string): string {
  const extension = extname(path).toLowerCase()
  const mapping: Record<string, string> = {
    '.c': 'c', '.cc': 'cpp', '.cpp': 'cpp', '.cxx': 'cpp', '.h': 'cpp', '.hh': 'cpp', '.hpp': 'cpp',
    '.s': 'asm', '.asm': 'asm', '.cmake': 'cmake', '.json': 'json', '.toml': 'toml', '.yaml': 'yaml',
    '.yml': 'yaml', '.sh': 'bash', '.py': 'python', '.rs': 'rust', '.js': 'javascript', '.ts': 'typescript',
  }
  return mapping[extension] ?? 'text'
}
