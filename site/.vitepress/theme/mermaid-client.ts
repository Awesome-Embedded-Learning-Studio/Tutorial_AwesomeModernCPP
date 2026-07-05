import { nextTick, onMounted } from 'vue'
import { subscribeAfterRouteChange } from './router-hooks'

// mermaid 的 API 形状(只取我们用到的两个方法),避免依赖整包类型
type MermaidApi = {
  initialize: (config: Record<string, unknown>) => void
  render: (id: string, text: string) => Promise<{ svg: string; bindFunctions?: (el: Element) => void }>
}

// 模块级缓存:整个应用生命周期只 initialize 一次。
let mermaidPromise: Promise<MermaidApi> | null = null

function loadMermaid(): Promise<MermaidApi> {
  if (typeof window === 'undefined') return Promise.reject(new Error('SSR 环境不加载 mermaid'))
  if (!mermaidPromise) {
    // 动态 import → Vite 把 mermaid 打进客户端异步 chunk(同源 + hash 缓存,离线/被墙都能渲染);
    // config 里的 ssr.external: ['mermaid'] 让 SSR build 不求值 mermaid,运行时 onMounted 才取。
    mermaidPromise = import('mermaid').then((mod) => {
      const mermaid = ((mod as { default?: MermaidApi }).default ?? (mod as unknown as MermaidApi))
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: 'loose',
        theme: 'default',
        flowchart: {
          htmlLabels: true,
          nodeSpacing: 50,
          rankSpacing: 50,
          padding: 15,
        },
        themeVariables: {
          fontSize: '15px',
        },
      })
      return mermaid
    })
  }
  return mermaidPromise
}

async function renderMermaidDiagrams() {
  if (typeof window === 'undefined') return

  let mermaid: MermaidApi
  try {
    mermaid = await loadMermaid()
  } catch (e) {
    console.error('[mermaid] 运行时加载失败', e)
    return
  }
  await nextTick()
  await new Promise<void>((r) => requestAnimationFrame(() => r()))

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
  // 用订阅器而非直接赋值 router.onAfterRouteChange:后者是单值属性,
  // 会被 ReadingProgress 等组件覆盖,导致 SPA 跳转后 mermaid 不渲染。
  // .catch 治「静默失败」:之前 onMounted 调用没接住 reject,加载失败时图直接消失无痕。
  onMounted(() => renderMermaidDiagrams().catch((e) => console.error('[mermaid] onMounted 渲染失败', e)))
  subscribeAfterRouteChange(() => renderMermaidDiagrams().catch((e) => console.error('[mermaid] 路由切换渲染失败', e)))
}
