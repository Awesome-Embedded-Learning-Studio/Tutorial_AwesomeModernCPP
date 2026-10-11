<script setup lang="ts">
/**
 * 路线图页「内容成熟度」板：已成型 / 在建 / 规划中三组。
 *
 * 数据源：roadmap-data.ts 的 ROADMAP_MATURITY（唯一事实源，CN/EN 双语）。
 * 组点配色与交互图状态点同源（绿 = 已成型，黄 = 在建），规划中用中性灰。
 * 每条带一句「现在能读到什么」，读者据此判断从哪里进最划算。
 */
import { computed } from 'vue'
import { useData } from 'vitepress'
import { ROADMAP_MATURITY, type Bi } from '../roadmap-data'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))
const L = (b: Bi) => (isEn.value ? b.en : b.cn)
</script>

<template>
  <div class="mb-board">
    <section
      v-for="g in ROADMAP_MATURITY"
      :key="g.key"
      class="mb-group"
      :class="`mb-group--${g.key}`"
    >
      <div class="mb-head">
        <i class="mb-dot" aria-hidden="true" />
        <span class="mb-title">{{ L(g.title) }}</span>
      </div>

      <ul class="mb-items">
        <li v-for="it in g.items" :key="L(it.label)" class="mb-item">
          <span class="mb-name">{{ L(it.label) }}</span>
          <span class="mb-note">{{ L(it.note) }}</span>
        </li>
      </ul>
    </section>
  </div>
</template>

<style scoped>
.mb-board {
  display: grid;
  grid-template-columns: 1fr;
  gap: 12px;
  margin: 12px 0 8px;
}

@media (min-width: 768px) {
  .mb-board {
    grid-template-columns: repeat(3, 1fr);
    align-items: start;
  }
}

.mb-group {
  padding: 14px 16px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  background: var(--vp-c-bg);
}

.mb-head {
  display: flex;
  align-items: center;
  gap: 7px;
  padding-bottom: 10px;
  margin-bottom: 4px;
  border-bottom: 1px solid var(--vp-c-bg-soft);
}

.mb-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}

.mb-group--done .mb-dot {
  background: var(--vp-c-green-1);
}

.mb-group--doing .mb-dot {
  background: var(--vp-c-yellow-1, #f59e0b);
}

.mb-group--planned .mb-dot {
  background: var(--vp-c-text-3);
}

.mb-title {
  font-size: 13.5px;
  font-weight: 600;
  color: var(--vp-c-text-1);
}

.mb-items {
  margin: 0;
  padding: 0;
  list-style: none;
}

.mb-item {
  padding: 8px 0;
}

.mb-item + .mb-item {
  border-top: 1px dashed var(--vp-c-bg-soft);
}

.mb-name {
  display: block;
  font-size: 12.5px;
  font-weight: 600;
  line-height: 1.5;
  color: var(--vp-c-text-1);
}

.mb-note {
  display: block;
  margin-top: 2px;
  font-size: 11.5px;
  line-height: 1.55;
  color: var(--vp-c-text-3);
}
</style>
