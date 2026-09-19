<script setup lang="ts">
import { onBeforeUnmount, onMounted, onUpdated, provide, ref, toRef } from 'vue'

const props = withDefaults(defineProps<{
  variant?: 'main' | 'sub'
}>(), {
  variant: 'main'
})

provide('chapterNavVariant', toRef(props, 'variant'))

const map = ref<HTMLElement>()
const trail = ref({
  width: 0,
  height: 0,
  path: '',
  arrows: [] as { x: number; y: number; angle: number }[],
})
let observer: ResizeObserver | undefined
let frame = 0
const observedLinks = new Set<HTMLElement>()
const round = (value: number) => Math.round(value * 100) / 100

function measureTrail() {
  frame = 0
  if (!map.value || props.variant !== 'sub') return

  const bounds = map.value.getBoundingClientRect()
  if (!bounds.width || !bounds.height) return

  const links = Array.from(map.value.querySelectorAll<HTMLElement>('.chapter-links > .chapter-link--sub'))
  for (const link of observedLinks) {
    if (!links.includes(link)) {
      observer?.unobserve(link)
      observedLinks.delete(link)
    }
  }

  const stops: { x: number; y: number; top: number; bottom: number }[] = []
  for (const link of links) {
    if (!observedLinks.has(link)) {
      observer?.observe(link)
      observedLinks.add(link)
    }
    const node = link.querySelector<HTMLElement>('.chapter-node')
    if (!node) continue
    const nodeBounds = node.getBoundingClientRect()
    const linkBounds = link.getBoundingClientRect()
    stops.push({
      x: round(nodeBounds.left + nodeBounds.width / 2 - bounds.left),
      y: round(nodeBounds.top + nodeBounds.height / 2 - bounds.top),
      top: round(linkBounds.top - bounds.top),
      bottom: round(linkBounds.bottom - bounds.top),
    })
  }

  const compact = getComputedStyle(map.value).getPropertyValue('--chapter-trail-compact').trim() === '1'
  const arrows: { x: number; y: number; angle: number }[] = []
  let path = stops.length > 1 ? `M ${stops[0].x} ${stops[0].y}` : ''

  for (let i = 1; i < stops.length; i++) {
    const from = stops[i - 1]
    const to = stops[i]
    const middle = round((from.bottom + to.top) / 2)
    // 宽屏的横向弯道放在两排文字之间；窄屏只在左侧轻轻起伏。
    const exit = compact ? from.y : round(Math.max(from.y, from.bottom - 8))
    const entry = compact ? to.y : round(Math.min(to.y, to.top + 8))
    path += ` L ${from.x} ${exit} C ${from.x} ${middle} ${to.x} ${middle} ${to.x} ${entry} L ${to.x} ${to.y}`
    arrows.push({
      x: round((from.x + to.x) / 2),
      y: round((exit + 6 * middle + entry) / 8),
      angle: Math.atan2(entry - exit, 2 * (to.x - from.x)) * 180 / Math.PI,
    })
  }

  const width = round(bounds.width)
  const height = round(bounds.height)
  // onUpdated 也会由 SVG 自身的更新触发，几何未变时不再写入响应式状态。
  if (trail.value.path !== path || trail.value.width !== width || trail.value.height !== height) {
    trail.value = { width, height, path, arrows }
  }
}

function scheduleMeasure() {
  if (!frame) frame = requestAnimationFrame(measureTrail)
}

onMounted(() => {
  observer = new ResizeObserver(scheduleMeasure)
  if (map.value) observer.observe(map.value)
  scheduleMeasure()
})
onUpdated(scheduleMeasure)
onBeforeUnmount(() => {
  observer?.disconnect()
  cancelAnimationFrame(frame)
})
</script>

<template>
  <div class="chapter-nav" :class="[`chapter-nav--${variant}`]">
    <div ref="map" class="chapter-map">
      <svg
        v-if="variant === 'sub' && trail.path"
        class="chapter-trail"
        :viewBox="`0 0 ${trail.width} ${trail.height}`"
        aria-hidden="true"
        focusable="false"
      >
        <path class="chapter-trail-bed" :d="trail.path" />
        <path class="chapter-trail-line" :d="trail.path" />
        <path
          v-for="(arrow, index) in trail.arrows"
          :key="index"
          class="chapter-trail-direction"
          d="M -4 -4 L 0 0 L -4 4"
          :transform="`translate(${arrow.x} ${arrow.y}) rotate(${arrow.angle})`"
        />
      </svg>
      <div class="chapter-links">
        <slot />
      </div>
    </div>
  </div>
</template>

<style scoped>
.chapter-nav {
  margin: 1.5em 0;
}

.chapter-links {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 12px;
}

/* 站点保持正常文档流，SVG 只负责在它们之间铺路。 */
.chapter-nav--sub {
  --chapter-map-bg: var(--vp-c-bg-soft-up);
  --chapter-trail-compact: 0;
  container: chapter-trail / inline-size;
  max-width: 860px;
  margin-inline: auto;
  padding: 28px clamp(16px, 4%, 36px);
  border: 1px solid var(--vp-c-divider);
  border-radius: 20px;
  background-color: var(--chapter-map-bg);
  background-image: radial-gradient(var(--vp-c-divider) 0.7px, transparent 0.7px);
  background-size: 20px 20px;
}

.chapter-nav--sub .chapter-map {
  position: relative;
}

.chapter-nav--sub .chapter-links {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 64px;
  padding: 0 12px;
  counter-reset: chapter-stop;
}

.chapter-trail {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  overflow: visible;
  pointer-events: none;
}

.chapter-trail path {
  fill: none;
  stroke-linecap: round;
  stroke-linejoin: round;
}

.chapter-trail-bed {
  stroke: var(--vp-c-brand-soft);
  stroke-width: 12px;
}

.chapter-trail-line {
  stroke: var(--vp-c-brand-1);
  stroke-opacity: 0.45;
  stroke-width: 1.5px;
  stroke-dasharray: 2 7;
}

.chapter-trail-direction {
  stroke: var(--vp-c-brand-1);
  stroke-width: 2px;
}

/* 单个补充链接不需要铺设路线。 */
.chapter-nav--sub:has(.chapter-link:only-child) {
  padding: 16px 20px;
  background-image: none;
}

@container chapter-trail (max-width: 560px) {
  .chapter-map {
    --chapter-trail-compact: 1;
  }

  .chapter-nav--sub .chapter-links {
    gap: 28px;
    padding: 0;
  }
}

@media (max-width: 639px) {
  .chapter-links {
    grid-template-columns: 1fr;
  }
}
</style>
