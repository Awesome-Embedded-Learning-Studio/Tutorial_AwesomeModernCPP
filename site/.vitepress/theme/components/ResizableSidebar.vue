<script setup lang="ts">
import { onMounted, onBeforeUnmount, ref } from 'vue'

// 可拖拽侧栏宽度:左导航树 + 右大纲栏(TOC)。
// 左栏 --vp-sidebar-width 由 VitePress 全链路消费(sidebar 自身 / 正文 padding-left / 顶栏对齐),
//   改这一个变量即全联动。但 handle 的定位与拖动计算必须读 .VPSidebar 的实际几何 —— 宽屏
//   浮动 panel 左缘带 (vw-maxW)/2 偏移,且受滚动条宽度影响,
//   CSS 公式推算总有小偏差。故 left 一律用 JS 读 getBoundingClientRect 精确设定(与右 handle 同策略)。
// 右栏 --vp-aside-width 自定义变量(在 custom.css 覆盖 aside max-width);右 handle absolute
//   注入 aside 内,MutationObserver 在路由切换重建 aside 时重新注入。
// 宽度持久化 localStorage;首屏防闪由 config head 内联脚本(hydration 前注入变量)负责。

type Side = 'left' | 'right'
interface Dim { min: number; max: number; def: number; key: string; cssVar: string }

const CONF: Record<Side, Dim> = {
  // 280px 是可用下限；420px 可完整显示绝大多数文章标题，仅整句式超长标题省略。
  left: { min: 280, max: 480, def: 420, key: 'vp-sidebar-width', cssVar: '--vp-sidebar-width' },
  right: { min: 180, max: 360, def: 256, key: 'vp-aside-width', cssVar: '--vp-aside-width' },
}

const leftHandle = ref<HTMLElement | null>(null)
const RIGHT_HANDLE_ID = 'rs-right-handle'

const clamp = (v: number, min: number, max: number) => Math.min(max, Math.max(min, v))
const applyVar = (side: Side, px: number) =>
  document.documentElement.style.setProperty(CONF[side].cssVar, px + 'px')
const persist = (side: Side, px: number) => {
  try { localStorage.setItem(CONF[side].key, String(px)) } catch { /* 隐私模式 / 配额 */ }
}

interface DragCtx {
  side: Side
  dim: Dim
  lastV: number
  handle: HTMLElement
  onMove: (e: MouseEvent) => void
  onUp: () => void
}
let drag: DragCtx | null = null

function startDrag(side: Side, e: MouseEvent) {
  e.preventDefault()
  const dim = CONF[side]
  const handle = e.currentTarget as HTMLElement
  handle.classList.add('is-active')
  document.body.classList.add('rs-resizing')

  // 用「位移」而非「绝对坐标」算新宽度。超宽屏浮动 panel 带居中偏移，按绝对坐标
  // 反推宽度会把偏移也算进去，导致宽度暴涨。位移法与布局无关，恒正确。
  const startX = e.clientX
  const startWidth =
    parseInt(getComputedStyle(document.documentElement).getPropertyValue(dim.cssVar)) || dim.def

  const onMove = (ev: MouseEvent) => {
    const delta = ev.clientX - startX
    // 左栏:鼠标右移加宽;右栏:鼠标左移加宽(右栏左缘向左拖)
    const v = clamp(
      Math.round(side === 'left' ? startWidth + delta : startWidth - delta),
      dim.min,
      dim.max
    )
    if (drag) drag.lastV = v
    applyVar(side, v)
    if (side === 'left' && drag?.handle) {
      drag.handle.style.left = ev.clientX + 'px' // handle 跟随鼠标(按下点在右缘,故 = 新右缘)
    }
  }
  const onUp = () => {
    if (drag) persist(side, drag.lastV)
    handle.classList.remove('is-active')
    document.body.classList.remove('rs-resizing')
    document.removeEventListener('mousemove', onMove)
    document.removeEventListener('mouseup', onUp)
    drag = null
    if (side === 'left') updateLeftPosition() // 拖动结束重新精确对齐(offsetLeft+offsetWidth)
  }
  drag = { side, dim, lastV: startWidth, handle, onMove, onUp }
  document.addEventListener('mousemove', onMove)
  document.addEventListener('mouseup', onUp)
}

function reset(side: Side) {
  applyVar(side, CONF[side].def)
  persist(side, CONF[side].def)
  if (side === 'left') updateLeftPosition()
}

// 仅在有侧栏的页(文档页)显示左 handle;首页等无侧栏页隐藏
function updateLeftVisibility() {
  if (!leftHandle.value) return
  leftHandle.value.style.display = document.querySelector('.VPSidebar') ? '' : 'none'
}

// 单行省略是导航树在有限宽度内的必要退化；给完整标题补原生 tooltip，避免信息丢失。
function updateSidebarTitles() {
  document.querySelectorAll<HTMLElement>('.VPSidebar .text').forEach((el) => {
    const label = el.textContent?.trim()
    if (label) el.title = label
  })
}

// 左 handle 精确定位:用 offsetLeft + offsetWidth(不含 transform),避开 sidebar 入场过渡
// (translateX(-100%)→0)对 getBoundingClientRect 的干扰 —— 首屏即读到最终右边缘,无需等动画结束。
function updateLeftPosition() {
  if (!leftHandle.value) return
  const sb = document.querySelector('.VPSidebar') as HTMLElement | null
  if (sb) leftHandle.value.style.left = (sb.offsetLeft + sb.offsetWidth) + 'px'
}

function injectRightHandle() {
  const aside = document.querySelector('.VPDoc.has-aside .aside') as HTMLElement | null
  if (!aside || aside.querySelector('#' + RIGHT_HANDLE_ID)) return
  const handle = document.createElement('div')
  handle.id = RIGHT_HANDLE_ID
  handle.className = 'rs-handle rs-handle--right'
  handle.setAttribute('role', 'separator')
  handle.setAttribute('aria-orientation', 'vertical')
  handle.setAttribute('aria-label', '拖动调整右侧大纲栏宽度(双击重置)')
  handle.addEventListener('mousedown', (ev) => startDrag('right', ev))
  handle.addEventListener('dblclick', () => reset('right'))
  aside.style.position = 'relative'
  aside.appendChild(handle)
}

let observer: MutationObserver | null = null
let leftTimer = 0
const onMutate = () => {
  updateLeftVisibility()
  updateLeftPosition()
  updateSidebarTitles()
  injectRightHandle()
}

onMounted(() => {
  // 恢复已存宽度(防闪脚本已在首屏注入,这里做内存兜底)
  ;(['left', 'right'] as Side[]).forEach((side) => {
    const dim = CONF[side]
    try {
      const v = parseInt(localStorage.getItem(dim.key) || '')
      // 320px 是上一版过窄的默认值，自动迁移；其他手动拖拽宽度保持不变。
      if (v >= dim.min && v <= dim.max && !(side === 'left' && v === 320)) {
        applyVar(side, v)
      } else {
        applyVar(side, dim.def)
        persist(side, dim.def)
      }
    } catch {}
  })
  onMutate()
  // sidebar 桌面端有 transform 入场过渡(translateX(-100%)→0,约 0.25s),过渡中
  // getBoundingClientRect 偏左,导致 handle 初始错位(拖动后才贴合)。
  // 多时机补校准,确保首屏即贴合:下一帧 / 过渡结束后(~350ms)/ 页面 load 后。
  requestAnimationFrame(updateLeftPosition)
  leftTimer = window.setTimeout(updateLeftPosition, 350)
  window.addEventListener('resize', updateLeftPosition, { passive: true })
  if (document.readyState !== 'complete') {
    window.addEventListener('load', updateLeftPosition)
  }
  const root = document.querySelector('.VPContent') || document.body
  if ('MutationObserver' in window) {
    observer = new MutationObserver(onMutate)
    observer.observe(root, { childList: true, subtree: true })
  }
})

onBeforeUnmount(() => {
  observer?.disconnect()
  observer = null
  window.clearTimeout(leftTimer)
  window.removeEventListener('resize', updateLeftPosition)
  window.removeEventListener('load', updateLeftPosition)
  document.querySelectorAll('#' + RIGHT_HANDLE_ID).forEach((n) => n.remove())
  if (drag) {
    document.removeEventListener('mousemove', drag.onMove)
    document.removeEventListener('mouseup', drag.onUp)
    drag = null
  }
})
</script>

<template>
  <div
    ref="leftHandle"
    class="rs-handle rs-handle--left"
    role="separator"
    aria-orientation="vertical"
    aria-label="拖动调整左侧导航栏宽度(双击重置)"
    @mousedown="startDrag('left', $event)"
    @dblclick="reset('left')"
  ></div>
</template>
