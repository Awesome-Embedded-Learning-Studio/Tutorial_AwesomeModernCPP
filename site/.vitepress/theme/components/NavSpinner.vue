<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { subscribeBeforeRouteChange, subscribeAfterRouteChange } from '../router-hooks'

// 跳转加载指示（issue #167）：SPA 点链接后 vitepress 要先去拉目标页的 JS
// chunk，慢网/冷缓存时旧页面会「死寂几秒 → 内容瞬间换掉 + 弹回顶部」，没有
// 任何加载反馈。两级反馈：
//   220ms 起 —— 左下角「加载中」小卡（目标页模块已缓存时 loadPage 是微任务
//               级的，阈值内不弹，弹了反而闪）
//   3s 起   —— 整站蒙半透明浮层，说明站点部署在 GitHub 上可能有访问延迟
// 浮层不拦点击（pointer-events: none）：卡住时读者仍可点其它链接自救，
// 新跳转会复用仍在的浮层。收到 onAfterRouteChange 两级一起收。
//
// 小卡放左下角是因为其余三角都有主：左上有 logo、右上有导航按钮、右下有
// 返回顶部。第一版做过顶部进度条，和常驻 top:0 的 ReadingProgress（阅读
// 进度）叠在一起分不清，弃用——反馈挪到角落就不抢地盘了。
const SHOW_DELAY_MS = 220
const VEIL_DELAY_MS = 3000

const chipVisible = ref(false)
const veilVisible = ref(false)
let chipTimer: ReturnType<typeof setTimeout> | undefined
let veilTimer: ReturnType<typeof setTimeout> | undefined

function start() {
  clearTimeout(chipTimer)
  clearTimeout(veilTimer)
  chipTimer = setTimeout(() => {
    if (!veilVisible.value) chipVisible.value = true
  }, SHOW_DELAY_MS)
  // 浮层已经蒙着（上一跳还没完又点了新链接）就继续用，别闪掉重来
  if (!veilVisible.value) {
    veilTimer = setTimeout(() => {
      chipVisible.value = false // 圆环放大进浮层，角落小卡退役
      veilVisible.value = true
    }, VEIL_DELAY_MS)
  }
}

function finish() {
  clearTimeout(chipTimer)
  clearTimeout(veilTimer)
  chipVisible.value = false
  veilVisible.value = false
}

// 后退/前进也走 loadPage，但 vitepress 不发 onBeforeRouteChange（router.js 的
// popstate 监听只调 loadPage + onAfterRouteChange），自己补弹。state 为 null 的
// 纯 hash 回退没有后续 onAfterRouteChange 来收，不能弹（否则永远转）。
function onPopState(e: PopStateEvent) {
  if (e.state === null) return
  start()
}

subscribeBeforeRouteChange(start)
subscribeAfterRouteChange(finish)

onMounted(() => window.addEventListener('popstate', onPopState))
onBeforeUnmount(() => {
  window.removeEventListener('popstate', onPopState)
  clearTimeout(chipTimer)
  clearTimeout(veilTimer)
})
</script>

<template>
  <Transition name="nav-spinner">
    <div v-if="chipVisible" class="nav-spinner" role="status" aria-label="页面加载中">
      <span class="nav-spinner__ring" />
      <span class="nav-spinner__label">加载中…</span>
    </div>
  </Transition>
  <Transition name="nav-veil">
    <div v-if="veilVisible" class="nav-veil" role="status" aria-live="polite">
      <div class="nav-veil__box">
        <span class="nav-veil__ring" />
        <p class="nav-veil__title">似乎有些卡顿……</p>
        <p class="nav-veil__sub">网站部署在 GitHub 上，可能访问会有延迟</p>
        <p class="nav-veil__hint">长时间没动静的话，刷新页面试试</p>
      </div>
    </div>
  </Transition>
</template>

<style scoped>
/* ── 第一级：左下角小卡 ─────────────────────────────────── */

.nav-spinner {
  position: fixed;
  left: 16px;
  bottom: 16px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 14px 6px 10px;
  border-radius: 999px;
  background: var(--vp-c-bg);
  border: 1px solid var(--vp-c-divider);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.14);
  font-size: 13px;
  color: var(--vp-c-text-2);
  z-index: 101;
  pointer-events: none;
}
.nav-spinner__ring {
  width: 16px;
  height: 16px;
  flex: none;
  border-radius: 50%;
  border: 2px solid var(--vp-c-divider);
  border-top-color: var(--vp-c-brand-1);
  animation: nav-spinner-rotate 0.8s linear infinite;
}
.nav-spinner__label {
  line-height: 1;
}

.nav-spinner-enter-active,
.nav-spinner-leave-active {
  transition: opacity 0.2s ease, transform 0.2s ease;
}
.nav-spinner-enter-from,
.nav-spinner-leave-to {
  opacity: 0;
  transform: translateY(6px);
}

/* ── 第二级：整站半透明浮层 ─────────────────────────────── */

.nav-veil {
  position: fixed;
  inset: 0;
  z-index: 101;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(255, 255, 255, 0.72);
  backdrop-filter: blur(2px);
  pointer-events: none;
}
html.dark .nav-veil {
  background: rgba(18, 18, 20, 0.72);
}
.nav-veil__box {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  max-width: 420px;
  padding: 0 24px;
  text-align: center;
}
.nav-veil__ring {
  width: 28px;
  height: 28px;
  margin-bottom: 4px;
  border-radius: 50%;
  border: 3px solid var(--vp-c-divider);
  border-top-color: var(--vp-c-brand-1);
  animation: nav-spinner-rotate 0.9s linear infinite;
}
.nav-veil__title {
  margin: 0;
  font-size: 18px;
  font-weight: 600;
  color: var(--vp-c-text-1);
}
.nav-veil__sub {
  margin: 0;
  font-size: 14px;
  color: var(--vp-c-text-2);
}
.nav-veil__hint {
  margin: 0;
  font-size: 13px;
  color: var(--vp-c-text-3);
}

.nav-veil-enter-active,
.nav-veil-leave-active {
  transition: opacity 0.25s ease;
}
.nav-veil-enter-from,
.nav-veil-leave-to {
  opacity: 0;
}

@keyframes nav-spinner-rotate {
  to {
    transform: rotate(360deg);
  }
}
@media (prefers-reduced-motion: reduce) {
  /* 放慢而不是冻住：静止的转圈像坏了的加载动画 */
  .nav-spinner__ring,
  .nav-veil__ring {
    animation-duration: 1.8s;
  }
}
@media print {
  .nav-spinner,
  .nav-veil {
    display: none;
  }
}
</style>
