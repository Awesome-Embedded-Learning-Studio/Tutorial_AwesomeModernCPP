<script setup lang="ts">
/**
 * 路线图页「按背景选起点」：三条学习路径卡。
 *
 * 数据源：roadmap-data.ts 的 ROADMAP_ROUTES（唯一事实源，CN/EN 双语）。
 * 胶囊 = 站点内链接：core（主线必经）实心、opt（按需选修）虚线，
 * 词汇与交互图的「建议主线 / 按需选修」图例对齐。
 */
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'
import { ROADMAP_ROUTES, type Bi } from '../roadmap-data'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))
const L = (b: Bi) => (isEn.value ? b.en : b.cn)

function pageHref(href: string): string {
  return withBase(isEn.value ? `/en${href}` : href)
}
</script>

<template>
  <div class="pch-list">
    <div v-for="r in ROADMAP_ROUTES" :key="r.id" class="pch-route">
      <div class="pch-side">
        <p class="pch-title">{{ L(r.title) }}</p>
        <p class="pch-who">{{ L(r.who) }}</p>
      </div>

      <div class="pch-main">
        <div class="pch-chips">
          <a
            v-for="chip in r.chips"
            :key="chip.href + L(chip.label)"
            class="pch-chip"
            :class="`pch-chip--${chip.kind}`"
            :href="pageHref(chip.href)"
          >{{ L(chip.label) }}</a>
        </div>
        <p class="pch-note">{{ L(r.note) }}</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.pch-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin: 12px 0 8px;
}

.pch-route {
  display: flex;
  gap: 20px;
  padding: 16px 18px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  background: var(--vp-c-bg);
  transition: border-color 0.2s ease;
}

.pch-route:hover {
  border-color: var(--vp-c-brand-1);
}

@media (max-width: 767px) {
  .pch-route {
    flex-direction: column;
    gap: 10px;
  }
}

.pch-side {
  flex: 0 0 218px;
}

.pch-title {
  margin: 0;
  font-size: 14.5px;
  font-weight: 600;
  line-height: 1.45;
  color: var(--vp-c-text-1);
}

.pch-who {
  margin: 6px 0 0;
  font-size: 12.5px;
  line-height: 1.6;
  color: var(--vp-c-text-3);
}

.pch-main {
  flex: 1;
  min-width: 0;
}

.pch-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 7px;
}

.pch-chip {
  display: inline-block;
  padding: 4px 12px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 500;
  line-height: 1.5;
  transition: border-color 0.2s ease, color 0.2s ease, background-color 0.2s ease;
}

/* core：主线必经，实心胶囊 */
.pch-chip--core {
  color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft);
}

.pch-chip--core:hover {
  background: var(--vp-c-brand-soft);
  filter: brightness(0.96);
}

/* opt：按需选修，虚线胶囊 */
.pch-chip--opt {
  color: var(--vp-c-text-2);
  border: 1px dashed var(--vp-c-border);
  background: transparent;
}

.pch-chip--opt:hover {
  color: var(--vp-c-brand-1);
  border-color: var(--vp-c-brand-1);
}

.pch-note {
  margin: 10px 0 0;
  font-size: 12.5px;
  line-height: 1.7;
  color: var(--vp-c-text-2);
}
</style>
