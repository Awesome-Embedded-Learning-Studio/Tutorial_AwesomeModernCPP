<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount } from 'vue'
import { useRouter } from 'vitepress'

// 顶部阅读进度条：滚动时按 scrollTop / (scrollHeight - clientHeight) 算百分比。
// brand 渐变 + 钢蓝微光，固定在视口顶部。路由切换 rAF+300ms 双重重算（短页归零坑）。
// scroll/resize 用 rAF 合并，一帧最多算一次（比 anatomy 原版无节流更省，审计点名的性能债）。
const progress = ref(0)
const router = useRouter()
let raf: number | null = null

function update() {
  raf = null
  const el = document.documentElement
  const scrollTop = el.scrollTop || document.body.scrollTop
  const scrollHeight = el.scrollHeight - el.clientHeight
  progress.value = scrollHeight > 0 ? Math.min(100, (scrollTop / scrollHeight) * 100) : 0
}

function scheduleUpdate() {
  if (raf !== null) return
  raf = requestAnimationFrame(update)
}

onMounted(() => {
  update()
  window.addEventListener('scroll', scheduleUpdate, { passive: true })
  window.addEventListener('resize', scheduleUpdate, { passive: true })
})

router.onAfterRouteChange = () => {
  // 路由切换：新页 DOM 立即可测 + 300ms 后（图片/mermaid 撑高）再补一次，防短页/异步内容归零失败
  requestAnimationFrame(update)
  setTimeout(update, 300)
}

onBeforeUnmount(() => {
  if (raf !== null) cancelAnimationFrame(raf)
  window.removeEventListener('scroll', scheduleUpdate)
  window.removeEventListener('resize', scheduleUpdate)
})
</script>

<template>
  <div class="reading-progress" aria-hidden="true">
    <div class="reading-progress__bar" :style="{ width: progress + '%' }" />
  </div>
</template>

<style scoped>
.reading-progress {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  z-index: 100;
  pointer-events: none;
  background: transparent;
}

.reading-progress__bar {
  height: 100%;
  background: linear-gradient(90deg, var(--vp-c-brand-1), var(--vp-c-brand-3));
  box-shadow: 0 0 8px rgba(37, 99, 235, 0.35);
  transition: width 0.12s ease-out;
}

@media print {
  .reading-progress { display: none; }
}
</style>
