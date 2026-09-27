<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { useData, withBase } from 'vitepress'
// 截图来自 documents/images/screenshots/,跨 srcDir 边界用相对路径 import,
// Vite 会在构建期处理成带 hash 的资源 URL(自动带 base)。
import img01 from '../../../../documents/images/screenshots/01-home.png'
import img02 from '../../../../documents/images/screenshots/02-article-code.png'
import img03 from '../../../../documents/images/screenshots/03-embedded.png'
import img04 from '../../../../documents/images/screenshots/04-cpp-reference.png'
import img05 from '../../../../documents/images/screenshots/05-roadmap.png'
import img06 from '../../../../documents/images/screenshots/06-dark-search.png'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))

interface Slide {
  img: string
  href: string
  cn: string
  en: string
}

const slides = computed<Slide[]>(() => [
  { img: img01, href: withBase('/'), cn: '首页 · 开始学习', en: 'Home · start here' },
  {
    img: img02,
    href: withBase('/vol2-modern-features/ch01-smart-pointers/02-unique-ptr/'),
    cn: '正文 + 可编译的代码示例',
    en: 'Articles with compilable code',
  },
  {
    img: img03,
    href: withBase('/vol8-domains/embedded/02-type-safe-register-access/'),
    cn: '嵌入式实战 · 类型安全寄存器访问',
    en: 'Embedded · type-safe register access',
  },
  { img: img04, href: withBase('/cpp-reference/'), cn: 'C++ 特性速查卡', en: 'C++ feature reference cards' },
  { img: img05, href: withBase('/roadmap/'), cn: '学习路线图', en: 'Learning roadmap' },
  { img: img06, href: withBase('/vol5-concurrency/'), cn: '暗色模式 + 全文搜索', en: 'Dark mode + full-text search' },
])

const active = ref(0)
const count = computed(() => slides.value.length)
const current = computed(() => slides.value[active.value] ?? slides.value[0])

// 每张图相对当前 active 的最短环绕偏移,据此决定它的位置类。
// 用模运算算「最短距离」,翻到头会从另一侧无缝接上,不会跳变。
function position(i: number): string {
  const n = count.value
  let d = i - active.value
  if (d > n / 2) d -= n
  else if (d < -n / 2) d += n
  if (d === 0) return 'is-center'
  if (d === 1) return 'is-right'
  if (d === -1) return 'is-left'
  return d > 0 ? 'is-far-right' : 'is-far-left'
}

function go(dir: 1 | -1) {
  active.value = (active.value + dir + count.value) % count.value
  restart()
}
function select(i: number) {
  if (i === active.value) return
  active.value = i
  restart()
}

// 5s 自动翻页;悬停 / 标签页隐藏时暂停(reduced-motion 下改成无动效切换,仍轮播)
const INTERVAL_MS = 5000
let timer: ReturnType<typeof setInterval> | null = null
function stop() {
  if (timer) {
    clearInterval(timer)
    timer = null
  }
}
function restart() {
  stop()
  timer = setInterval(() => {
    active.value = (active.value + 1) % count.value
  }, INTERVAL_MS)
}
function onVisibility() {
  if (document.hidden) stop()
  else restart()
}

onMounted(() => {
  restart()
  document.addEventListener('visibilitychange', onVisibility)
})
onBeforeUnmount(() => {
  stop()
  document.removeEventListener('visibilitychange', onVisibility)
})
</script>

<template>
  <section class="shot-carousel" aria-roledescription="carousel">
    <header class="shot-carousel__head">
      <h2 class="shot-carousel__title">{{ isEn ? 'Take a quick look' : '先睹为快' }}</h2>
      <p class="shot-carousel__sub">
        {{ isEn
          ? 'Auto-playing every 5s · hover to pause · center opens the page'
          : '每 5 秒自动翻页 · 悬停暂停 · 点中间打开该页' }}
      </p>
    </header>

    <div
      class="shot-carousel__stage"
      @mouseenter="stop"
      @mouseleave="restart"
    >
      <button
        class="shot-carousel__arrow shot-carousel__arrow--prev"
        :aria-label="isEn ? 'Previous slide' : '上一张'"
        @click="go(-1)"
      >
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><polyline points="15 18 9 12 15 6"/></svg>
      </button>

      <div
        v-for="(s, i) in slides"
        :key="i"
        class="shot-carousel__cell"
        :class="position(i)"
      >
        <a
          class="shot-carousel__link"
          :href="i === active ? s.href : undefined"
          :aria-label="isEn ? s.en : s.cn"
          :tabindex="i === active ? 0 : -1"
        >
          <img :src="s.img" :alt="isEn ? s.en : s.cn" loading="lazy" decoding="async" draggable="false" />
        </a>
      </div>

      <button
        class="shot-carousel__arrow shot-carousel__arrow--next"
        :aria-label="isEn ? 'Next slide' : '下一张'"
        @click="go(1)"
      >
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
      </button>
    </div>

    <div class="shot-carousel__caption">
      <span class="shot-carousel__cap-text">{{ isEn ? current.en : current.cn }}</span>
      <a class="shot-carousel__cap-link" :href="current.href">{{ isEn ? 'Open →' : '查看 →' }}</a>
    </div>

    <div class="shot-carousel__dots" role="tablist">
      <button
        v-for="(s, i) in slides"
        :key="i"
        class="shot-carousel__dot"
        :class="{ 'is-active': i === active }"
        :aria-label="isEn ? `Go to slide ${i + 1}` : `第 ${i + 1} 张`"
        :aria-selected="i === active ? 'true' : 'false'"
        @click="select(i)"
      />
    </div>
  </section>
</template>

<style scoped>
.shot-carousel {
  max-width: 1152px;
  margin: 8px auto 40px;
  padding: 0 24px;
  animation: shot-fade-up 0.7s cubic-bezier(0.25, 0.46, 0.45, 0.94) 0.1s both;
}

.shot-carousel__head {
  text-align: center;
  margin-bottom: 14px;
}
.shot-carousel__title {
  margin: 0;
  font-size: 22px;
  font-weight: 700;
  letter-spacing: 0.3px;
  color: var(--vp-c-text-1);
}
.shot-carousel__sub {
  margin: 6px 0 0;
  font-size: 13px;
  color: var(--vp-c-text-2);
}

/* ── Stage:coverflow 容器,带透视 ─────────────────────────── */
.shot-carousel__stage {
  position: relative;
  height: clamp(300px, 42vw, 430px);
  perspective: 1400px;
}

/* 每个 cell 绝对居中,再按位置类施加变换 */
.shot-carousel__cell {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  margin: 0 auto;
  width: min(600px, 86%);
  height: 100%;
  transform-origin: center center;
  transition: transform 0.6s cubic-bezier(0.4, 0, 0.2, 1), opacity 0.45s ease;
  will-change: transform, opacity;
  cursor: pointer;
}
.shot-carousel__cell.is-center {
  transform: translateX(0) scale(1) rotateY(0deg);
  opacity: 1;
  z-index: 3;
  cursor: pointer;
}
.shot-carousel__cell.is-left {
  transform: translateX(-58%) scale(0.76) rotateY(20deg);
  opacity: 0.5;
  z-index: 2;
  pointer-events: none; /* 两侧纯展示、不可点击,杜绝点击→变正中→默认动作跳转的竞态 */
}
.shot-carousel__cell.is-right {
  transform: translateX(58%) scale(0.76) rotateY(-20deg);
  opacity: 0.5;
  z-index: 2;
  pointer-events: none;
}
.shot-carousel__cell.is-far-left {
  transform: translateX(-95%) scale(0.6) rotateY(20deg);
  opacity: 0;
  z-index: 0;
  pointer-events: none;
}
.shot-carousel__cell.is-far-right {
  transform: translateX(95%) scale(0.6) rotateY(-20deg);
  opacity: 0;
  z-index: 0;
  pointer-events: none;
}

.shot-carousel__link {
  display: block;
  width: 100%;
  height: 100%;
  border-radius: 14px;
  overflow: hidden;
  border: 1px solid var(--vp-c-divider);
  background: var(--vp-c-bg-soft);
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.12);
  transition: border-color 0.25s, box-shadow 0.25s;
}
.shot-carousel__cell.is-center:hover .shot-carousel__link {
  border-color: var(--vp-c-brand-1);
  box-shadow: 0 20px 48px rgba(15, 52, 96, 0.19);
}
.shot-carousel__link img {
  display: block;
  width: 100%;
  height: 100%;
  object-fit: cover;
  pointer-events: none; /* 点击交给 cell 处理 */
}

/* ── Arrows ─────────────────────────────────────────────── */
.shot-carousel__arrow {
  position: absolute;
  top: 50%;
  transform: translateY(-50%);
  z-index: 5;
  width: 42px;
  height: 42px;
  border-radius: 50%;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  color: var(--vp-c-brand-1);
  background: var(--vp-c-bg-soft);
  border: 1px solid var(--vp-c-divider);
  transition: background 0.2s, color 0.2s, border-color 0.2s, transform 0.1s;
}
.shot-carousel__arrow:hover {
  background: var(--vp-c-brand-soft);
  border-color: var(--vp-c-brand-1);
}
.shot-carousel__arrow:active {
  transform: translateY(-50%) scale(0.94);
}
.shot-carousel__arrow:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}
.shot-carousel__arrow--prev { left: max(4px, 1.5%); }
.shot-carousel__arrow--next { right: max(4px, 1.5%); }

/* ── Caption + dots ─────────────────────────────────────── */
.shot-carousel__caption {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  margin-top: 16px;
  min-height: 22px;
}
.shot-carousel__cap-text {
  font-size: 14px;
  font-weight: 600;
  color: var(--vp-c-text-1);
}
.shot-carousel__cap-link {
  font-size: 13px;
  font-weight: 600;
  color: var(--vp-c-brand-1);
  text-decoration: underline;
  text-underline-offset: 2px;
}
.shot-carousel__cap-link:hover {
  color: var(--vp-c-brand-2);
}

.shot-carousel__dots {
  display: flex;
  justify-content: center;
  gap: 8px;
  margin-top: 14px;
}
.shot-carousel__dot {
  width: 8px;
  height: 8px;
  padding: 0;
  border: none;
  border-radius: 50%;
  background: var(--vp-c-divider);
  cursor: pointer;
  transition: background 0.2s, width 0.2s, border-radius 0.2s;
}
.shot-carousel__dot.is-active {
  background: var(--vp-c-brand-1);
  width: 22px;
  border-radius: 999px;
}

@keyframes shot-fade-up {
  from { opacity: 0; transform: translateY(16px); }
  to   { opacity: 1; transform: translateY(0); }
}

@media (prefers-reduced-motion: reduce) {
  .shot-carousel { animation: none !important; }
  /* 仍自动轮播(见 JS),但切换不带动效,尊重减少动态偏好 */
  .shot-carousel__cell { transition: none !important; }
}

@media (max-width: 639px) {
  .shot-carousel {
    padding: 0 16px;
    margin-bottom: 24px;
  }
  .shot-carousel__stage {
    height: clamp(240px, 56vw, 320px);
  }
  .shot-carousel__cell.is-left {
    transform: translateX(-46%) scale(0.66) rotateY(16deg);
    opacity: 0.42;
  }
  .shot-carousel__cell.is-right {
    transform: translateX(46%) scale(0.66) rotateY(-16deg);
    opacity: 0.42;
  }
  .shot-carousel__cell.is-far-left {
    transform: translateX(-78%) scale(0.55);
  }
  .shot-carousel__cell.is-far-right {
    transform: translateX(78%) scale(0.55);
  }
  .shot-carousel__arrow {
    width: 38px;
    height: 38px;
  }
  .shot-carousel__title { font-size: 19px; }
  .shot-carousel__sub { font-size: 12px; }
}
</style>
