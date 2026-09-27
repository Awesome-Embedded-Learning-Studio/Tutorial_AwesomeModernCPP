import { nextTick, onMounted } from 'vue'
import { subscribeAfterRouteChange } from './router-hooks'
import { openImageLightbox } from './image-lightbox'

// 正文图片点击放大:扫描 .vp-doc img,接管点击进 ImageLightbox 全屏模态。
// 范式抄 mermaid-client.ts(onMounted + 路由订阅器重扫,防重复标记)。

async function bindDocImages() {
  if (typeof window === 'undefined') return
  await nextTick()
  await new Promise<void>((r) => requestAnimationFrame(() => r()))

  // class 本身即 dedupe 标记:绑过的不再绑(mermaid 用 data-rendered 是因为
  // 容器和按钮分离,这里单元素场景 class 即状态)。
  const images = Array.from(
    document.querySelectorAll<HTMLImageElement>('.vp-doc img:not(.doc-img--zoomable)'),
  )

  for (const img of images) {
    // 排除三类:外层是 <a>(链接语义优先,team 页头像全是这种形态);
    // .no-zoom(QQGroupCard 二维码、WeeklyAcknowledgements 头像等组件图);
    // .mxgraph(drawio viewer 容器,自带缩放工具条——今天没有 img,防御性排除)。
    if (img.closest('a') || img.closest('.no-zoom') || img.closest('.mxgraph')) continue

    img.classList.add('doc-img--zoomable')
    img.setAttribute('tabindex', '0')
    img.setAttribute('role', 'button')
    img.setAttribute('aria-label', '放大查看图片')
    img.addEventListener('click', () => {
      openImageLightbox({ img, trigger: img })
    })
    img.addEventListener('keydown', (e: KeyboardEvent) => {
      if (e.key !== 'Enter' && e.key !== ' ') return
      e.preventDefault() // Space 不滚动页面
      openImageLightbox({ img, trigger: img })
    })
  }
}

export function setupDocImageZoom() {
  // 用订阅器而非直接赋值 router.onAfterRouteChange:后者是单值属性,
  // 会被 ReadingProgress 等组件覆盖,导致 SPA 跳转后图片不被接管。
  // .catch 治「静默失败」:扫描抛错时留下痕迹而不是无声消失。
  onMounted(() => bindDocImages().catch((e) => console.error('[image-zoom] onMounted 绑定失败', e)))
  subscribeAfterRouteChange(() => bindDocImages().catch((e) => console.error('[image-zoom] 路由切换绑定失败', e)))
}
