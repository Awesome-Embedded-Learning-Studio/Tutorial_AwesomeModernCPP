<script setup lang="ts">
/**
 * 首页「学习路径」双视图区块（挂载于 home-features-after，替换旧 HomeRoadmap）。
 *
 * 视图一 = HomePathGraph（SVG 交互路径图，默认）；视图二 = HomeFeatureGrid
 * （原 16 张 feature 卡）。两页共用内容上方的方向 Banner 切换；移动端仍可左右滑动
 * （scroll-snap 原生翻页，无 JS 也可滚）。
 *
 * 手势分工：pager 在外层、panzoom 在视图一内部——图内横滑归图（panzoom 平移），
 * 翻页靠 Banner / 卡片视图区滑动。inert 屏蔽非活动页的焦点，但只在
 * 客户端 mounted 后设置（SSR HTML 里两页都可点，保证无 JS / 首屏可访问性）。
 */
import { computed, onMounted, ref } from 'vue'
import { useData, withBase } from 'vitepress'
import HomePathGraph from './HomePathGraph.vue'
import HomeFeatureGrid from './HomeFeatureGrid.vue'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))

const t = computed(() =>
  isEn.value
    ? {
        title: 'Learning Path',
        graphNote: 'Drag nodes to arrange · Hover to trace a path · Click to jump',
        cardsNote: '16 content sections · Pick a card and start reading',
        bannerCards: '16 sections at a glance 👉',
        bannerGraph: '👈 Back to the learning path',
        bannerCardsAria: 'Switch to all 16 content sections',
        bannerGraphAria: 'Return to the learning path map',
        graphLabel: 'Interactive learning path map',
        cardsLabel: 'All 16 content sections',
        cta: 'Full roadmap →',
        ctaLink: '/en/roadmap/',
      }
    : {
        title: '学习路径',
        graphNote: '拖动节点布置 · 悬停追溯路径 · 点击直达',
        cardsNote: '16 个内容板块 · 选一张卡片开始阅读',
        bannerCards: '16 个板块，一页爽读 👉',
        bannerGraph: '👈 回到学习路线图',
        bannerCardsAria: '切换到全部 16 个内容板块',
        bannerGraphAria: '返回学习路线图',
        graphLabel: '交互式学习路线图',
        cardsLabel: '全部 16 个内容板块',
        cta: '完整路线图 →',
        ctaLink: '/roadmap/',
      },
)

/* ── pager ↔ Banner 状态同步 ── */
const active = ref(0)
const mounted = ref(false)
const pagerEl = ref<HTMLElement | null>(null)
let rafPending = false

function onScroll() {
  if (rafPending || !pagerEl.value) return
  rafPending = true
  requestAnimationFrame(() => {
    rafPending = false
    const el = pagerEl.value
    if (!el || !el.clientWidth) return
    const i = Math.round(el.scrollLeft / el.clientWidth)
    if (i !== active.value) active.value = i
  })
}

function select(i: number) {
  const el = pagerEl.value
  if (!el) return
  /* Banner 点击时短暂关闭 snap，直接落到目标页；否则 Chromium 会在程序化反向滚动
     越过中点前吸回原页。下一帧恢复，手指横滑仍保留原生滚动与 snap 动效。 */
  el.style.scrollSnapType = 'none'
  el.style.scrollBehavior = 'auto'
  el.scrollLeft = i * el.clientWidth
  active.value = i
  requestAnimationFrame(() => {
    el.style.removeProperty('scroll-snap-type')
    el.style.removeProperty('scroll-behavior')
  })
}

function toggleView() {
  select(active.value === 0 ? 1 : 0)
}

onMounted(() => {
  /* mounted 后才设 inert：SSR HTML 保持两页可交互（无 JS / 爬虫可达） */
  mounted.value = true
})
</script>

<template>
  <section id="roadmap" class="home-path">
    <header class="hp-head">
      <h2 class="hp-title">🧭 {{ t.title }}</h2>
      <p class="hp-note">{{ active === 0 ? t.graphNote : t.cardsNote }}</p>

      <a class="hp-cta" :href="withBase(t.ctaLink)">{{ t.cta }}</a>
    </header>

    <button
      type="button"
      class="hp-switch"
      :class="active === 0 ? 'hp-switch--next' : 'hp-switch--back'"
      :aria-controls="active === 0 ? 'hp-panel-cards' : 'hp-panel-graph'"
      :aria-label="active === 0 ? t.bannerCardsAria : t.bannerGraphAria"
      @click="toggleView"
    >
      <span aria-live="polite">{{ active === 0 ? t.bannerCards : t.bannerGraph }}</span>
    </button>

    <div ref="pagerEl" class="hp-pager" @scroll.passive="onScroll">
      <div
        id="hp-panel-graph"
        role="region"
        :aria-label="t.graphLabel"
        :aria-hidden="mounted && active !== 0"
        class="hp-slide"
        :inert="mounted && active !== 0"
      >
        <HomePathGraph />
      </div>
      <div
        id="hp-panel-cards"
        role="region"
        :aria-label="t.cardsLabel"
        :aria-hidden="mounted && active !== 1"
        class="hp-slide"
        :inert="mounted && active !== 1"
      >
        <HomeFeatureGrid />
      </div>
    </div>
  </section>
</template>

<style scoped>
.home-path {
  max-width: 1152px;
  margin: 40px auto 56px;
  padding: 0 24px;
  scroll-margin-top: 80px;
  animation: hp-fade-up 0.7s cubic-bezier(0.25, 0.46, 0.45, 0.94) both;
}

.hp-head {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px 16px;
  margin-bottom: 16px;
}

.hp-title {
  margin: 0;
  font-size: 20px;
  font-weight: 700;
  line-height: 1.4;
  color: var(--vp-c-text-1);
}

.hp-note {
  margin: 0;
  font-size: 13px;
  color: var(--vp-c-text-3);
}

/* 双页方向 Banner：内容靠目标页方向对齐，替代传统 tab 的“工具栏感”。 */
.hp-switch {
  display: flex;
  align-items: center;
  width: 100%;
  min-height: 44px;
  margin: 0 0 12px;
  padding: 10px 16px;
  border: 1px solid color-mix(in srgb, var(--vp-c-brand-1) 24%, var(--vp-c-divider));
  border-radius: 12px;
  background: linear-gradient(90deg, var(--vp-c-bg-soft), var(--vp-c-brand-soft));
  color: var(--vp-c-text-2);
  font-family: var(--vp-font-family);
  font-size: 13.5px;
  font-weight: 600;
  line-height: 1.4;
  cursor: pointer;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.04);
  transition: border-color 0.25s ease, color 0.25s ease, background 0.25s ease;
}

.hp-switch--next {
  justify-content: flex-end;
  text-align: right;
  background: linear-gradient(90deg, var(--vp-c-bg-soft), var(--vp-c-brand-soft));
}

.hp-switch--back {
  justify-content: flex-start;
  text-align: left;
  background: linear-gradient(90deg, var(--vp-c-brand-soft), var(--vp-c-bg-soft));
}

.hp-switch:hover {
  border-color: var(--vp-c-brand-1);
  color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft-2);
}

.hp-switch:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}

.hp-switch span {
  transition: transform 0.25s ease;
}

.hp-switch--next:hover span {
  transform: translateX(4px);
}

.hp-switch--back:hover span {
  transform: translateX(-4px);
}

.hp-cta {
  margin-left: auto;
  font-size: 13px;
  font-weight: 600;
  color: var(--vp-c-brand-1);
  text-decoration: none;
  white-space: nowrap;
}

.hp-cta:hover {
  text-decoration: underline;
}

/* 双页 pager：scroll-snap 原生横滑翻页（无 JS 也可滚），Banner 同步活动页 */
.hp-pager {
  display: flex;
  overflow-x: auto;
  scroll-snap-type: x mandatory;
  overscroll-behavior-x: contain;
  scrollbar-width: none;
  -webkit-overflow-scrolling: touch;
}

.hp-pager::-webkit-scrollbar {
  display: none;
}

.hp-slide {
  flex: 0 0 100%;
  min-width: 100%;
  scroll-snap-align: center;
}

@keyframes hp-fade-up {
  from {
    opacity: 0;
    transform: translateY(18px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

@media (prefers-reduced-motion: reduce) {
  .home-path {
    animation: none !important;
  }

  .hp-switch,
  .hp-switch span {
    transition: none;
  }
}

@media (max-width: 639px) {
  .home-path {
    padding: 0 16px;
    margin: 28px auto 36px;
  }

  .hp-note {
    display: none;
  }

  .hp-switch {
    min-height: 42px;
    padding: 9px 13px;
    font-size: 13px;
  }

  .hp-cta {
    margin-left: auto;
  }
}
</style>
