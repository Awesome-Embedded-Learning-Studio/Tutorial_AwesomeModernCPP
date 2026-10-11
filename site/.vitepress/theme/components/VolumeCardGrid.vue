<script setup lang="ts">
/**
 * 路线图页「十卷总览」卡片墙。
 *
 * 数据源：roadmap-data.ts 的 ROADMAP_CARDS（唯一事实源，CN/EN 双语，组件只渲染）。
 * 卡片主体是「进入」链接（拉伸链接盖满卡面，整卡可点）；右下角「本页详解」
 * 浮在链接层之上，指向页内详解锚点，两个动作互不抢点击。
 * 状态点配色与 HomePathGraph 同源：绿 = 已成型，黄 = 在建·持续更新。
 * 链接走 VitePress 路由器的全局接管做 SPA 跳转，不套 vp-raw。
 */
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'
import { ROADMAP_CARDS, ROADMAP_UI, type Bi, type VolumeCard } from '../roadmap-data'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))
const L = (b: Bi) => (isEn.value ? b.en : b.cn)

function pageHref(href: string): string {
  return withBase(isEn.value ? `/en${href}` : href)
}

const ui = computed(() => ({
  enter: L(ROADMAP_UI.enter),
  detail: L(ROADMAP_UI.detail),
  difficulty: L(ROADMAP_UI.difficultyLabel),
  prereq: L(ROADMAP_UI.prereqLabel),
  done: L(ROADMAP_UI.doneLabel),
  doing: L(ROADMAP_UI.doingLabel),
}))

const statusLabel = (c: VolumeCard) => (c.status === 'done' ? ui.value.done : ui.value.doing)
</script>

<template>
  <div class="rcg-grid">
    <div
      v-for="c in ROADMAP_CARDS"
      :key="c.id"
      class="rcg-card"
      :class="`rcg-card--${c.status}`"
    >
      <div class="rcg-head">
        <span class="rcg-badge">{{ c.badge }}</span>
        <span class="rcg-status" :class="`rcg-status--${c.status}`">
          <i class="rcg-dot" aria-hidden="true" />{{ statusLabel(c) }}
        </span>
      </div>

      <p class="rcg-name">{{ L(c.name) }}</p>
      <p class="rcg-tagline">{{ L(c.tagline) }}</p>

      <div class="rcg-meta">
        <span><em>{{ ui.difficulty }}</em>{{ L(c.difficulty) }}</span>
        <span><em>{{ ui.prereq }}</em>{{ L(c.prereq) }}</span>
      </div>

      <div class="rcg-foot">
        <!-- 拉伸链接：盖满卡面，整卡即「进入」 -->
        <a class="rcg-go" :href="pageHref(c.href)">
          {{ ui.enter }}
          <svg class="rcg-arrow" xmlns="http://www.w3.org/2000/svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14M12 5l7 7-7 7" /></svg>
        </a>
        <a class="rcg-detail" :href="`#${c.id}`">{{ ui.detail }}</a>
      </div>
    </div>
  </div>
</template>

<style scoped>
.rcg-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(232px, 1fr));
  gap: 12px;
  margin: 12px 0 8px;
}

.rcg-card {
  position: relative;
  display: flex;
  flex-direction: column;
  padding: 14px 16px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  background: var(--vp-c-bg);
  transition: border-color 0.2s ease, box-shadow 0.2s ease, transform 0.2s ease;
}

.rcg-card:hover {
  border-color: var(--vp-c-brand-1);
  box-shadow: 0 3px 10px rgba(0, 0, 0, 0.07);
  transform: translateY(-2px);
}

.rcg-card:focus-within {
  border-color: var(--vp-c-brand-1);
}

/* ── 头部：徽章 + 状态 ── */
.rcg-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 10px;
}

.rcg-badge {
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.6px;
  line-height: 1;
  padding: 4px 7px;
  border-radius: 6px;
  color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft);
}

.rcg-status {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 11.5px;
  color: var(--vp-c-text-3);
}

.rcg-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}

.rcg-status--done .rcg-dot {
  background: var(--vp-c-green-1);
}

.rcg-status--doing .rcg-dot {
  background: var(--vp-c-yellow-1, #f59e0b);
}

/* ── 主体 ── */
.rcg-name {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
  line-height: 1.4;
  color: var(--vp-c-text-1);
}

.rcg-tagline {
  flex-grow: 1;
  margin: 6px 0 12px;
  font-size: 12.5px;
  line-height: 1.55;
  color: var(--vp-c-text-2);
}

.rcg-meta {
  display: flex;
  flex-wrap: wrap;
  gap: 4px 14px;
  margin-bottom: 10px;
  font-size: 11.5px;
  color: var(--vp-c-text-2);
}

.rcg-meta em {
  font-style: normal;
  margin-right: 4px;
  padding: 1px 5px;
  border-radius: 4px;
  font-size: 10.5px;
  color: var(--vp-c-text-3);
  background: var(--vp-c-bg-soft);
}

/* ── 底部双动作 ── */
.rcg-foot {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding-top: 9px;
  border-top: 1px dashed var(--vp-c-border);
}

.rcg-go {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 12.5px;
  font-weight: 600;
  color: var(--vp-c-brand-1);
}

/* 拉伸链接盖满卡面：整卡可点进卷页 */
.rcg-go::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 12px;
}

.rcg-arrow {
  transition: transform 0.2s ease;
}

.rcg-card:hover .rcg-arrow {
  transform: translateX(3px);
}

/* 详解链接浮在拉伸链接之上，保住自己的点击 */
.rcg-detail {
  position: relative;
  z-index: 1;
  font-size: 11.5px;
  color: var(--vp-c-text-3);
  transition: color 0.2s ease;
}

.rcg-detail:hover {
  color: var(--vp-c-brand-1);
}
</style>
