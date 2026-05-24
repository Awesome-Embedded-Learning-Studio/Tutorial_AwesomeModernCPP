import { nextTick, onMounted } from 'vue'
import { useRouter } from 'vitepress'

declare global {
  interface Window {
    mermaid?: {
      initialize: (config: Record<string, unknown>) => void
      render: (id: string, text: string) => Promise<{ svg: string; bindFunctions?: (el: Element) => void }>
    }
    __mermaidLoadingPromise__?: Promise<void>
    __mermaidInitialized__?: boolean
  }
}

const MERMAID_CDN = 'https://cdn.jsdelivr.net/npm/mermaid@10.9.6/dist/mermaid.min.js'

function loadMermaid(): Promise<void> {
  if (typeof window === 'undefined') return Promise.resolve()

  if (window.mermaid) {
    initMermaid()
    return Promise.resolve()
  }

  if (window.__mermaidLoadingPromise__) return window.__mermaidLoadingPromise__

  window.__mermaidLoadingPromise__ = new Promise<void>((resolve, reject) => {
    const existing = document.querySelector<HTMLScriptElement>('script[data-mermaid-runtime]')
    if (existing) {
      existing.addEventListener('load', () => { initMermaid(); resolve() })
      existing.addEventListener('error', () => reject(new Error('Failed to load Mermaid')))
      return
    }

    const script = document.createElement('script')
    script.src = MERMAID_CDN
    script.async = true
    script.dataset.mermaidRuntime = 'true'
    script.onload = () => { initMermaid(); resolve() }
    script.onerror = () => reject(new Error(`Failed to load Mermaid from ${MERMAID_CDN}`))
    document.head.appendChild(script)
  })

  return window.__mermaidLoadingPromise__
}

function initMermaid() {
  if (!window.mermaid || window.__mermaidInitialized__) return
  window.mermaid.initialize({
    startOnLoad: false,
    securityLevel: 'loose',
    theme: 'default',
  })
  window.__mermaidInitialized__ = true
}

async function renderMermaidDiagrams() {
  if (typeof window === 'undefined') return

  await loadMermaid()
  await nextTick()
  await new Promise<void>((r) => requestAnimationFrame(() => r()))

  const mermaid = window.mermaid
  if (!mermaid) return

  const nodes = Array.from(
    document.querySelectorAll<HTMLElement>('.mermaid-diagram[data-rendered="false"]')
  )

  for (let i = 0; i < nodes.length; i++) {
    const el = nodes[i]
    const raw = el.dataset.mermaid
    if (!raw) continue

    const source = decodeURIComponent(raw)
    const id = `mermaid-${Date.now()}-${i}-${Math.random().toString(36).slice(2, 8)}`

    try {
      const { svg } = await mermaid.render(id, source)
      el.innerHTML = svg
      el.dataset.rendered = 'true'
    } catch {
      el.dataset.rendered = 'error'
      el.innerHTML = `<pre class="mermaid-error">${escapeHtml(source)}</pre>`
    }
  }
}

function escapeHtml(s: string) {
  return s.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;').replaceAll("'", '&#39;')
}

export function setupMermaid() {
  const router = useRouter()

  onMounted(() => renderMermaidDiagrams())
  router.onAfterRouteChange = () => renderMermaidDiagrams()
}
