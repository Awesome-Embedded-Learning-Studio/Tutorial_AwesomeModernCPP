// ImageLightbox 的 opener 桥接:命令式扫描的 image-client.ts(非 Vue 组件)
// 需要触发一个挂在 Layout 上的 Vue 模态组件。这里用一个模块级单例函数,
// 组件挂载时 registerImageLightboxOpener 注册回调,image-client 点图片时
// 调 openImageLightbox。不引入 vue 的 reactive,避免 SSR 阶段对 DOM 引用做代理。

export interface ImageLightboxPayload {
  /** 正文中被点击的原图(模态里新建 img 用其 src,不动原图) */
  img: HTMLImageElement
  /** 触发元素(即原图),模态关闭后焦点还回这里 */
  trigger: HTMLElement
}

type Opener = (payload: ImageLightboxPayload) => void

let opener: Opener | null = null

/** ImageLightbox 组件挂载时调用,返回卸载函数。 */
export function registerImageLightboxOpener(fn: Opener): () => void {
  opener = fn
  return () => {
    if (opener === fn) opener = null
  }
}

/** image-client 的图片点击时调用。组件未挂载时静默 no-op。 */
export function openImageLightbox(payload: ImageLightboxPayload): void {
  opener?.(payload)
}
