<template>
  <!-- Teleport 到 body:盖在整页最上层,不被正文 stacking context 裁剪。
       抄 MermaidLightbox.vue 的模态范式(body.overflow 锁 + ESC + onBeforeUnmount 清理),
       项目惯例是复制范式而非抽公共组件,不动已验收的 mermaid 代码。 -->
  <Teleport to="body">
    <div
      v-if="open"
      class="image-lightbox"
      role="dialog"
      aria-modal="true"
      aria-label="放大查看图片"
    >
      <div class="image-lightbox__toolbar" role="toolbar" aria-label="图片缩放控制">
        <button
          ref="firstBtnRef"
          type="button"
          class="image-lightbox__btn"
          title="放大"
          @click="zoomIn"
        >
          放大
        </button>
        <button
          type="button"
          class="image-lightbox__btn"
          title="缩小"
          @click="zoomOut"
        >
          缩小
        </button>
        <button
          type="button"
          class="image-lightbox__btn"
          title="复位 / 适应窗口"
          @click="reset"
        >
          复位
        </button>
        <span class="image-lightbox__hint">滚轮缩放 · 拖拽平移 · 双指缩放</span>
        <button
          type="button"
          class="image-lightbox__btn image-lightbox__close"
          title="关闭 (Esc)"
          aria-label="关闭"
          @click="close"
        >
          ✕
        </button>
      </div>
      <!-- @click.self:点舞台空白区关闭;点图片本身(panzoom 接管拖拽)不关闭。 -->
      <div
        ref="stageRef"
        class="image-lightbox__stage"
        @click.self="close"
      >
        <div ref="targetRef" class="image-lightbox__target" />
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { nextTick, onBeforeUnmount, ref } from 'vue'
import { registerImageLightboxOpener, type ImageLightboxPayload } from '../image-lightbox'

// panzoom 只在 mountImage 里动态 import,不进 SSR bundle,也不进首屏 chunk。
// 这里用本地最小类型,避免静态 import 类型把库拽进来。
interface PanzoomInstance {
  zoomIn: (opts?: unknown) => void
  zoomOut: (opts?: unknown) => void
  reset: (opts?: unknown) => void
  zoomWithWheel: (event: WheelEvent) => void
  destroy: () => void
}

const open = ref(false)
const stageRef = ref<HTMLElement | null>(null)
const targetRef = ref<HTMLElement | null>(null)
const firstBtnRef = ref<HTMLElement | null>(null)

let panzoom: PanzoomInstance | null = null
let payload: ImageLightboxPayload | null = null
let wheelHandler: ((e: WheelEvent) => void) | null = null
let keyHandler: ((e: KeyboardEvent) => void) | null = null
let prevOverflow = ''

// 挂载即注册 opener;image-client 点图片会触发 openDialog。
const unregister = registerImageLightboxOpener((p) => {
  payload = p
  openDialog()
})

function openDialog() {
  open.value = true
  prevOverflow = document.body.style.overflow
  document.body.style.overflow = 'hidden'

  keyHandler = (e: KeyboardEvent) => {
    if (!open.value) return
    if (e.key === 'Escape') {
      e.preventDefault()
      close()
    } else if (e.key === 'Tab') {
      trapFocus(e)
    }
  }
  window.addEventListener('keydown', keyHandler)

  // 等 Teleport 内容挂到 DOM 后再建 img + 挂 panzoom。
  void nextTick(() => {
    void mountImage().then(() => firstBtnRef.value?.focus())
  })
}

async function mountImage() {
  if (!payload || !stageRef.value || !targetRef.value) return
  // 新建 img 用原 src(浏览器缓存秒到),cloneNode 会带上正文里的约束样式,不如重建干净。
  const img = document.createElement('img')
  img.src = payload.img.currentSrc || payload.img.src
  img.alt = payload.img.alt
  img.draggable = false
  img.decoding = 'async'

  // 尺寸开箱即得:能被点到说明原图已加载,naturalWidth 直接可用,不用等 load,
  // 消灭「先塌再撑」的闪动。naturalWidth 为 0(Firefox 无固有尺寸 svg / 加载失败)
  // 时跳过显式宽高,靠 CSS max-width/max-height 兜底(flex 居中不塌)。
  const naturalW = payload.img.naturalWidth
  const naturalH = payload.img.naturalHeight
  if (naturalW > 0 && naturalH > 0) {
    // 居中(flex 已处理)+ 占舞台 75%,留出舒服边距;min(...,1) 大图缩到 75%、小图不放大。
    const maxW = Math.max(160, stageRef.value.clientWidth * 0.75)
    const maxH = Math.max(160, stageRef.value.clientHeight * 0.75)
    const scale = Math.min(maxW / naturalW, maxH / naturalH, 1)
    img.style.width = `${Math.round(naturalW * scale)}px`
    img.style.height = `${Math.round(naturalH * scale)}px`
  }

  targetRef.value.innerHTML = ''
  targetRef.value.appendChild(img)

  const { default: createPanzoom } = await import('@panzoom/panzoom')
  // targetRef(包 img 的 div)做 panzoom 目标:CSS transform 挂 HTML div,与 mermaid 同款。
  panzoom = createPanzoom(targetRef.value, {
    maxScale: 8,
    minScale: 0.3, // 允许缩到比 fit 更小(minScale:1 时缩小按钮被钳住、点了没反应)
    step: 0.25,
    cursor: 'grab',
  })

  wheelHandler = (e: WheelEvent) => panzoom?.zoomWithWheel(e)
  stageRef.value.addEventListener('wheel', wheelHandler, { passive: false })
}

function trapFocus(e: KeyboardEvent) {
  const root = stageRef.value?.parentElement
  if (!root) return
  const focusables = Array.from(
    root.querySelectorAll<HTMLElement>('button, [href], input, [tabindex]:not([tabindex="-1"])'),
  ).filter((el) => el.offsetParent !== null)
  if (focusables.length === 0) return
  const first = focusables[0]
  const last = focusables[focusables.length - 1]
  if (e.shiftKey && document.activeElement === first) {
    e.preventDefault()
    last.focus()
  } else if (!e.shiftKey && document.activeElement === last) {
    e.preventDefault()
    first.focus()
  }
}

function close() {
  if (!open.value) return
  open.value = false
  teardown()
  payload?.trigger?.focus?.()
  payload = null
}

function teardown() {
  if (wheelHandler && stageRef.value) {
    stageRef.value.removeEventListener('wheel', wheelHandler)
  }
  wheelHandler = null
  panzoom?.destroy()
  panzoom = null
  if (keyHandler) {
    window.removeEventListener('keydown', keyHandler)
    keyHandler = null
  }
  document.body.style.overflow = prevOverflow
  if (targetRef.value) targetRef.value.innerHTML = ''
}

function zoomIn() {
  panzoom?.zoomIn()
}
function zoomOut() {
  panzoom?.zoomOut()
}
function reset() {
  panzoom?.reset()
}

onBeforeUnmount(() => {
  unregister()
  if (open.value) teardown()
})
</script>
