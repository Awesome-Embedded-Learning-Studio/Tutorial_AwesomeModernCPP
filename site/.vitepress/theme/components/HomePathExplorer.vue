<script setup lang="ts">
/**
 * 首页「学习路径」：完整路线图入口 + 全部内容卡片。
 * 交互路径图集中在路线图页，首页卡片直接进入各卷，不再横向翻页。
 */
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'
import HomeFeatureGrid from './HomeFeatureGrid.vue'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))

const t = computed(() =>
  isEn.value
    ? {
        title: 'Learning Path',
        cardsNote: 'Pick a section and start reading',
        ctaTitle: 'Explore the full learning roadmap',
        ctaDescription: 'Find your starting point, see how the volumes connect, and plan your learning pace.',
        ctaAction: 'Open roadmap',
        ctaLink: '/en/roadmap/',
      }
    : {
        title: '学习路径',
        cardsNote: '选一张卡片开始阅读',
        ctaTitle: '完整学习路线图',
        ctaDescription: '按背景选起点，看各卷怎么衔接，安排适合自己的学习节奏。',
        ctaAction: '打开路线图',
        ctaLink: '/roadmap/',
      },
)
</script>

<template>
  <section id="roadmap" class="home-path">
    <header class="hp-head">
      <h2 class="hp-title">🧭 {{ t.title }}</h2>
      <p class="hp-note">{{ t.cardsNote }}</p>
    </header>

    <a class="hp-cta" :href="withBase(t.ctaLink)">
      <span class="hp-cta-icon" aria-hidden="true">
        <svg xmlns="http://www.w3.org/2000/svg" width="30" height="30" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="6" cy="5" r="2" />
          <circle cx="18" cy="19" r="2" />
          <path d="M8 5h8a4 4 0 0 1 0 8H8a3 3 0 0 0 0 6h8" />
        </svg>
      </span>
      <span class="hp-cta-copy">
        <span class="hp-cta-title">{{ t.ctaTitle }}</span>
        <span class="hp-cta-description">{{ t.ctaDescription }}</span>
      </span>
      <span class="hp-cta-action">
        {{ t.ctaAction }}
        <svg class="hp-cta-arrow" aria-hidden="true" xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14M12 5l7 7-7 7" /></svg>
      </span>
    </a>

    <HomeFeatureGrid />
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

.hp-cta {
  display: grid;
  grid-template-columns: 56px minmax(0, 1fr) auto;
  align-items: center;
  gap: 20px;
  margin-bottom: 12px;
  padding: 24px;
  border: 1px solid color-mix(in srgb, var(--vp-c-brand-1) 35%, var(--vp-c-divider));
  border-radius: 16px;
  background: linear-gradient(110deg, var(--vp-c-brand-soft), var(--vp-c-bg));
  text-decoration: none;
  box-shadow: 0 3px 14px color-mix(in srgb, var(--vp-c-brand-1) 7%, transparent);
  transition: border-color 0.2s ease, box-shadow 0.2s ease;
}

.hp-cta:hover {
  border-color: var(--vp-c-brand-1);
  box-shadow: 0 5px 20px color-mix(in srgb, var(--vp-c-brand-1) 13%, transparent);
}

.hp-cta:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 4px;
}

.hp-cta-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 56px;
  height: 56px;
  border-radius: 14px;
  color: var(--vp-c-brand-1);
  background: var(--vp-c-bg);
  border: 1px solid color-mix(in srgb, var(--vp-c-brand-1) 20%, var(--vp-c-divider));
}

.hp-cta-copy {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.hp-cta-title {
  color: var(--vp-c-text-1);
  font-size: 18px;
  font-weight: 700;
  line-height: 1.4;
}

.hp-cta-description {
  color: var(--vp-c-text-2);
  font-size: 13px;
  line-height: 1.7;
}

.hp-cta-action {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 10px 16px;
  border-radius: 10px;
  color: var(--vp-c-bg);
  background: var(--vp-c-brand-1);
  font-size: 13px;
  font-weight: 600;
  line-height: 1.5;
  white-space: nowrap;
}

.hp-cta-arrow {
  flex-shrink: 0;
  transition: transform 0.2s ease;
}

.hp-cta:hover .hp-cta-arrow {
  transform: translateX(3px);
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
    animation: none;
  }

  .hp-cta,
  .hp-cta-arrow {
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

  .hp-cta {
    grid-template-columns: 44px minmax(0, 1fr);
    align-items: start;
    gap: 14px;
    padding: 18px;
  }

  .hp-cta-icon {
    width: 44px;
    height: 44px;
    border-radius: 12px;
  }

  .hp-cta-title {
    font-size: 16px;
  }

  .hp-cta-action {
    grid-column: 2;
    justify-self: start;
    padding: 8px 12px;
  }
}
</style>
