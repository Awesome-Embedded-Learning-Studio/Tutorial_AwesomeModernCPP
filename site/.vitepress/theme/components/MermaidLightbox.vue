<template>
  <!-- Teleport 到 body:盖在整页最上层,不被正文 stacking context 裁剪。
       抄 OnlineCompilerDemo.vue 的模态范式(body.overflow 锁 + ESC + onBeforeUnmount 清理)。 -->
  <Teleport to="body">
    <div
      v-if="open"
      class="mermaid-lightbox"
      role="dialog"
      aria-modal="true"
      aria-label="放大查看图表"
    >
      <div class="mermaid-lightbox__toolbar" role="toolbar" aria-label="图表缩放控制">
        <button
          ref="firstBtnRef"
          type="button"
          class="mermaid-lightbox__btn"
          title="放大"
          @click="zoomIn"
        >
          放大
        </button>
        <button
          type="button"
          class="mermaid-lightbox__btn"
          title="缩小"
          @click="zoomOut"
        >
          缩小
        </button>
        <button
          type="button"
          class="mermaid-lightbox__btn"
          title="复位 / 适应窗口"
          @click="reset"
        >
          复位
        </button>
        <span class="mermaid-lightbox__hint">滚轮缩放 · 拖拽平移 · 双指缩放</span>
        <button
          type="button"
          class="mermaid-lightbox__btn mermaid-lightbox__close"
          title="关闭 (Esc)"
          aria-label="关闭"
          @click="close"
        >
          ✕
        </button>
      </div>
      <!-- @click.self:点舞台空白区关闭;点 SVG 本身(panzoom 接管拖拽)不关闭。 -->
      <div
        ref="stageRef"
        class="mermaid-lightbox__stage"
        @click.self="close"
      >
        <div ref="targetRef" class="mermaid-lightbox__target" />
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { nextTick, onBeforeUnmount, ref } from 'vue'
import { registerMermaidLightboxOpener, type MermaidLightboxPayload } from '../mermaid-lightbox'

// panzoom 只在 mountDiagram 里动态 import,不进 SSR bundle,也不进首屏 chunk。
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
let payload: MermaidLightboxPayload | null = null
let wheelHandler: ((e: WheelEvent) => void) | null = null
let keyHandler: ((e: KeyboardEvent) => void) | null = null
let prevOverflow = ''

// 挂载即注册 opener;mermaid-client 点 maximize 会触发 openDialog。
const unregister = registerMermaidLightboxOpener((p) => {
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

  // 等 Teleport 内容挂到 DOM 后再 clone SVG + 挂 panzoom。
  void nextTick(() => {
    void mountDiagram().then(() => firstBtnRef.value?.focus())
  })
}

async function mountDiagram() {
  if (!payload || !stageRef.value || !targetRef.value) return
  const svg = payload.svg.cloneNode(true) as SVGElement
  targetRef.value.innerHTML = ''
  targetRef.value.appendChild(svg)

  // 关键:必须给 clone 显式像素宽高,否则 SVG 要么塌成 300x150、要么按 mermaid 的
  // width="100%" 撑成内禀尺寸(巨大、只剩左上角)。注意 svg.viewBox.baseVal 在某些浏览器
  // 的 detached clone 上会返回 0 → 直接解析 viewBox 属性字符串最稳("minX minY W H")。
  svg.removeAttribute('style')
  const vbAttr = svg.getAttribute('viewBox')
  let vbW = 0
  let vbH = 0
  if (vbAttr) {
    const p = vbAttr.trim().split(/[\s,]+/).map(Number)
    if (p.length >= 4 && Number.isFinite(p[2]) && Number.isFinite(p[3])) {
      vbW = p[2]
      vbH = p[3]
    }
  }
  if (vbW > 0 && vbH > 0) {
    // 居中(flex 已处理)+ 占舞台 75%,留出舒服边距;min(...,1) 大图缩到 75%、小图不放大。
    const maxW = Math.max(160, stageRef.value.clientWidth * 0.75)
    const maxH = Math.max(160, stageRef.value.clientHeight * 0.75)
    const scale = Math.min(maxW / vbW, maxH / vbH, 1)
    svg.setAttribute('width', String(Math.round(vbW * scale)))
    svg.setAttribute('height', String(Math.round(vbH * scale)))
  }
  svg.style.display = 'block'

  const { default: createPanzoom } = await import('@panzoom/panzoom')
  // targetRef(包 SVG 的 div)做 panzoom 目标:CSS transform 挂 HTML div,
  // 绕开 SVG 坐标系/viewBox/foreignObject 一切争议(研究阶段核验过)。
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
