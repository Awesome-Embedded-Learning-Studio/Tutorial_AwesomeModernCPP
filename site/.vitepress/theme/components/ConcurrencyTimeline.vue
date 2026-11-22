<script setup lang="ts">
import { ref } from 'vue'

// 并发教学的时间线图：行是执行者（核心/线程），列是时间段，格子是工作段。
// 富环境渲染本体的网格视图；插槽内容是给纯 Markdown 阅读器的降级图
// （Typora/GitHub/VS Code 不认本组件，会剥掉标签直接显示插槽里的 mermaid），
// 站点上也保留为「看静态图」切换视图，所以降级内容不是死重。
interface TimelineBlock {
  lane: number
  from: number
  to: number
  label: string
  kind?: string
}

const props = defineProps<{
  /** 图标题，可省 */
  title?: string
  /** 时间刻度标签，每格一列 */
  ticks: string[]
  /** 行名（核心名/线程名），每行一条泳道 */
  lanes: string[]
  /** 工作段。from/to 是刻度下标，左闭右开，from < to <= ticks.length */
  blocks: TimelineBlock[]
  /** kind 到图例文案的映射，如 { download: '下载类工作', ui: '界面类工作' } */
  kindLabels?: Record<string, string>
  /** 左上角提示文字，如「时间往右 →」 */
  axisHint?: string
}>()

// hover 的列号（刻度下标），-1 表示没有。整列高亮用来演示「同一小段时间」
const hoverCol = ref(-1)
// 富图与插槽静态图的切换
const showStatic = ref(false)

// 图例按文章里 kind 首次出现的顺序排，读起来跟正文顺序一致
const usedKinds: string[] = []
for (const b of props.blocks) {
  const k = b.kind ?? 'generic'
  if (!usedKinds.includes(k)) usedKinds.push(k)
}

function kindLabel(k: string): string {
  return props.kindLabels?.[k] ?? k
}
</script>

<template>
  <figure class="ct">
    <div v-if="title || $slots.default" class="ct-head">
      <span v-if="title" class="ct-title">{{ title }}</span>
      <button
        v-if="$slots.default"
        class="ct-toggle"
        type="button"
        @click="showStatic = !showStatic"
      >
        {{ showStatic ? '看时间线' : '看静态图' }}
      </button>
    </div>

    <!-- 所有格子显式定位（gridRow/gridColumn），不用自动放置：
         跨列的工作段和空格子要叠在同一格，自动放置算法会绕开已占格导致错位 -->
    <div
      v-show="!showStatic"
      class="ct-grid"
      :style="{ gridTemplateColumns: `auto repeat(${ticks.length}, 1fr)` }"
    >
      <div class="ct-corner" :style="{ gridRow: 1, gridColumn: 1 }">{{ axisHint }}</div>
      <div
        v-for="(t, ci) in ticks"
        :key="'t' + ci"
        class="ct-tick"
        :class="{ 'ct-hl': hoverCol === ci }"
        :style="{ gridRow: 1, gridColumn: ci + 2 }"
        @mouseenter="hoverCol = ci"
        @mouseleave="hoverCol = -1"
      >
        {{ t }}
      </div>

      <template v-for="(lane, li) in lanes" :key="'lane' + li">
        <div class="ct-lane" :style="{ gridRow: li + 2, gridColumn: 1 }">{{ lane }}</div>
        <div
          v-for="(_, ci) in ticks"
          :key="`cell-${li}-${ci}`"
          class="ct-cell"
          :class="{ 'ct-hl': hoverCol === ci }"
          :style="{ gridRow: li + 2, gridColumn: ci + 2 }"
          @mouseenter="hoverCol = ci"
          @mouseleave="hoverCol = -1"
        ></div>
        <div
          v-for="(b, bi) in blocks.filter((x) => x.lane === li)"
          :key="`blk-${li}-${bi}`"
          class="ct-block"
          :class="'ct-k-' + (b.kind ?? 'generic')"
          :style="{ gridRow: li + 2, gridColumn: `${b.from + 2} / ${b.to + 2}` }"
        >
          {{ b.label }}
        </div>
      </template>
    </div>

    <!-- 静态降级图：Markdown 阅读器直接看到；站点上由按钮切换 -->
    <div v-if="$slots.default" v-show="showStatic" class="ct-static">
      <slot></slot>
    </div>

    <figcaption v-if="!showStatic && usedKinds.length > 1" class="ct-legend">
      <span v-for="k in usedKinds" :key="k" class="ct-legend-item">
        <span class="ct-swatch" :class="'ct-k-' + k"></span>{{ kindLabel(k) }}
      </span>
      <span class="ct-legend-hint">悬停格子可对齐同一列</span>
    </figcaption>
  </figure>
</template>

<style scoped>
.ct {
  margin: 16px 0;
}
.ct-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
  gap: 8px;
}
.ct-title {
  font-weight: 600;
  font-size: 14px;
  color: var(--vp-c-text-1);
}
.ct-toggle {
  font-size: 12px;
  border: 1px solid var(--vp-c-border);
  background: var(--vp-c-bg-soft);
  color: var(--vp-c-text-2);
  border-radius: 6px;
  padding: 2px 10px;
  cursor: pointer;
  white-space: nowrap;
}
.ct-toggle:hover {
  color: var(--vp-c-text-1);
  border-color: var(--vp-c-brand-1);
}
.ct-grid {
  display: grid;
  gap: 4px;
  align-items: stretch;
}
.ct-corner {
  font-size: 11px;
  color: var(--vp-c-text-3);
  align-self: center;
  padding-right: 8px;
  white-space: nowrap;
}
.ct-tick {
  font-size: 12px;
  color: var(--vp-c-text-2);
  text-align: center;
  padding: 2px 4px;
  border-radius: 6px;
  transition: box-shadow 0.15s;
}
.ct-lane {
  font-size: 12px;
  font-weight: 600;
  color: var(--vp-c-text-1);
  align-self: center;
  text-align: right;
  padding-right: 10px;
  white-space: nowrap;
}
.ct-cell {
  border-radius: 6px;
  min-height: 46px;
  background: var(--vp-c-default-soft);
  border: 1px dashed var(--vp-c-border);
  transition: box-shadow 0.15s;
}
.ct-hl {
  box-shadow: 0 0 0 2px var(--vp-c-brand-1);
}
.ct-block {
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  text-align: center;
  font-size: 12px;
  line-height: 1.35;
  color: var(--vp-c-text-1);
  padding: 4px 6px;
  z-index: 1;
}
/* kind 色板：半透明底加同色边框，明暗主题下都读得清 */
.ct-k-download {
  background: rgba(94, 106, 210, 0.16);
  border: 1px solid rgba(94, 106, 210, 0.55);
}
.ct-k-ui {
  background: rgba(56, 161, 105, 0.14);
  border: 1px solid rgba(56, 161, 105, 0.5);
}
.ct-k-wait {
  background: rgba(213, 160, 63, 0.14);
  border: 1px solid rgba(213, 160, 63, 0.5);
}
.ct-k-switch {
  background: rgba(130, 140, 150, 0.16);
  border: 1px solid rgba(130, 140, 150, 0.5);
}
.ct-k-generic {
  background: var(--vp-c-default-soft);
  border: 1px solid var(--vp-c-border);
}
.ct-static {
  border: 1px dashed var(--vp-c-border);
  border-radius: 8px;
  padding: 12px;
}
.ct-legend {
  display: flex;
  gap: 14px;
  margin-top: 8px;
  font-size: 12px;
  color: var(--vp-c-text-2);
  flex-wrap: wrap;
}
.ct-swatch {
  display: inline-block;
  width: 12px;
  height: 12px;
  border-radius: 4px;
  margin-right: 5px;
  vertical-align: -1px;
}
.ct-legend-hint {
  color: var(--vp-c-text-3);
}
</style>
