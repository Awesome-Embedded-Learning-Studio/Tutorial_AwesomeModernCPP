<script setup lang="ts">
/**
 * 首页「全部内容」视图（HomePathExplorer 的视图二）。
 *
 * 数据直接读当前首页 frontmatter 的 features（与 VitePress 默认 VPFeatures 同源，
 * CN/EN 两套首页各取各的，不漂移）；标记刻意复用 .VPFeatures/.VPFeature 类名，
 * 让 custom.css 里既有的 rail / tier 三档配色 / hover / 入场动画全部免费生效。
 * VitePress 自带的基础布局样式是 scoped 的、作用不到自渲染标记，故在本组件
 * scoped 内复制等价规则（VPFeatures.vue / VPFeature.vue 的布局部分）；
 * 箭头图标内联 SVG，不依赖 .vpi-* 图标类（规避主题升级类名漂移）。
 */
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'

interface Feature {
  icon?: string
  title: string
  details?: string
  link?: string
  linkText?: string
}

const { frontmatter } = useData()
const features = computed<Feature[]>(() =>
  (frontmatter.value.features as Feature[] | undefined) ?? [],
)

/* 栅格档位判定照抄 VitePress VPFeatures.vue：16 项 → grid-4 */
const grid = computed(() => {
  const length = features.value.length
  if (!length) return undefined
  if (length === 2) return 'grid-2'
  if (length === 3) return 'grid-3'
  if (length % 3 === 0) return 'grid-6'
  if (length > 3) return 'grid-4'
  return undefined
})
</script>

<template>
  <div v-if="features.length" class="VPFeatures home-cards">
    <div class="container">
      <div class="items">
        <div
          v-for="f in features"
          :key="f.title"
          class="item"
          :class="[grid]"
        >
          <component
            :is="f.link ? 'a' : 'div'"
            class="VPFeature"
            :class="{ link: !!f.link }"
            :href="f.link ? withBase(f.link) : undefined"
          >
            <article class="box">
              <div v-if="f.icon" class="icon" v-html="f.icon" />
              <h2 class="title" v-html="f.title" />
              <p v-if="f.details" class="details" v-html="f.details" />
              <div v-if="f.linkText" class="link-text">
                <p class="link-text-value">
                  {{ f.linkText }}
                  <svg
                    class="link-text-icon"
                    xmlns="http://www.w3.org/2000/svg"
                    width="14"
                    height="14"
                    viewBox="0 0 24 24"
                    fill="none"
                    stroke="currentColor"
                    stroke-width="2"
                    stroke-linecap="round"
                    stroke-linejoin="round"
                  ><path d="M5 12h14M12 5l7 7-7 7" /></svg>
                </p>
              </div>
            </article>
          </component>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* ── 布局骨架复制自 VitePress VPFeatures.vue / VPFeature.vue 的 scoped 段；
     视觉层（边框/圆角/rail/tier/hover/动画）由 custom.css 的全局 .VPFeature
     规则接管，二者叠加即为改造前的完整观感 ── */

.VPFeatures {
  position: relative;
  padding: 16px 24px 48px;
}

.container {
  margin: 0 auto;
  max-width: 1152px;
}

.items {
  display: flex;
  flex-wrap: wrap;
  margin: -8px;
}

.item {
  padding: 8px;
  width: 100%;
}

@media (min-width: 640px) {
  .item.grid-2,
  .item.grid-4,
  .item.grid-6 {
    width: calc(100% / 2);
  }
}

@media (min-width: 768px) {
  .item.grid-2,
  .item.grid-4 {
    width: calc(100% / 2);
  }

  .item.grid-3,
  .item.grid-6 {
    width: calc(100% / 3);
  }
}

@media (min-width: 960px) {
  .item.grid-4 {
    width: calc(100% / 4);
  }
}

.VPFeature {
  display: block;
  height: 100%;
}

.box {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.icon {
  display: flex;
  justify-content: center;
  align-items: center;
  margin-bottom: 20px;
}

.title {
  line-height: 24px;
  font-size: 16px;
  font-weight: 600;
}

.details {
  flex-grow: 1;
  padding-top: 8px;
  line-height: 24px;
  font-weight: 500;
}

.link-text {
  padding-top: 8px;
}

.link-text-value {
  display: flex;
  align-items: center;
  font-size: 14px;
  font-weight: 500;
  color: var(--vp-c-brand-1);
}

.link-text-icon {
  margin-left: 6px;
  flex-shrink: 0;
}
</style>
