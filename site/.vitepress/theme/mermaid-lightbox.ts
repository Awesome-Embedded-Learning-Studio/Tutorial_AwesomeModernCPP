// MermaidLightbox 的 opener 桥接:命令式渲染的 mermaid-client.ts(非 Vue 组件)
// 需要触发一个挂在 Layout 上的 Vue 模态组件。这里用一个模块级单例函数,
// 组件挂载时 registerMermaidLightboxOpener 注册回调,mermaid-client 点 maximize 时
// 调 openMermaidLightbox。不引入 vue 的 reactive,避免 SSR 阶段对 DOM 引用做代理。

export interface MermaidLightboxPayload {
  /** 已渲染好的内联 SVG(模态里会 cloneNode,不动原图) */
  svg: SVGElement
  /** mermaid 源码,用于无障碍描述 */
  source: string
  /** 触发按钮,模态关闭后焦点还回这里 */
  trigger: HTMLElement
}

type Opener = (payload: MermaidLightboxPayload) => void

let opener: Opener | null = null

/** MermaidLightbox 组件挂载时调用,返回卸载函数。 */
export function registerMermaidLightboxOpener(fn: Opener): () => void {
  opener = fn
  return () => {
    if (opener === fn) opener = null
  }
}

/** mermaid-client 的 maximize 按钮点击时调用。组件未挂载时静默 no-op。 */
export function openMermaidLightbox(payload: MermaidLightboxPayload): void {
  opener?.(payload)
}
