<script setup lang="ts">
import { computed } from 'vue'
import { useData } from 'vitepress'
import { useBookmarkState } from '../composables/useBookmarks'

// 导航栏的收藏按钮:读到哪都能点,点击瞬间的滚动位置就是收藏位置。
// 挂 nav-bar-content-after(桌面)与 nav-screen-content-after(移动端汉堡),与字号切换器同排。
// 判据用 frontmatter.chapter:全站文章必填(validate_frontmatter 强制),首页/roadmap/tags/书签页都没有。
const { frontmatter, title } = useData()
const { bookmark, toggle } = useBookmarkState()

const isDocPage = computed(() => typeof frontmatter.value.chapter === 'number')

// useData().title 会拼上站名后缀「| 现代 C++ 教程」,书签里只要文章自己的标题
const pageTitle = () => (typeof frontmatter.value.title === 'string' && frontmatter.value.title
  ? frontmatter.value.title
  : title.value)

/** 点击瞬间的滚动百分比(照 ReadingProgress 的算法现算一次) */
function currentScrollPercent(): number {
  const el = document.documentElement
  const max = el.scrollHeight - el.clientHeight
  return max > 0 ? Math.min(100, Math.round((el.scrollTop / max) * 100)) : 0
}

function onToggle(): void {
  toggle(pageTitle(), currentScrollPercent())
}
</script>

<template>
  <button
    v-if="isDocPage"
    type="button"
    class="bm-toggle"
    :class="{ 'is-active': !!bookmark }"
    :aria-pressed="!!bookmark"
    :title="bookmark
      ? `已收藏在 ${Math.round(bookmark.scrollPercent)}%(点此取消,想换位置就删了重收)`
      : '收藏本页当前位置,回头从导航「更多 → 书签」进来续读'"
    @click="onToggle"
  >
    <span class="bm-toggle__mark" aria-hidden="true">{{ bookmark ? '★' : '☆' }}</span>
    <span class="bm-toggle__label">{{ bookmark ? '已收藏' : '收藏' }}</span>
  </button>
</template>

<style scoped>
/* 尺度对齐 FontSizeSwitcher:24px 高的胶囊,margin 0 8px */
.bm-toggle {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  margin: 0 8px;
  padding: 0 10px;
  height: 28px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 14px;
  background: var(--vp-c-bg-soft);
  color: var(--vp-c-text-2);
  font-size: 12px;
  font-weight: 600;
  line-height: 1;
  cursor: pointer;
  transition: color 0.2s ease, border-color 0.2s ease, background-color 0.2s ease;
}

.bm-toggle:hover {
  color: var(--vp-c-brand-1);
  border-color: var(--vp-c-brand-1);
}

.bm-toggle.is-active {
  color: var(--vp-c-brand-1);
  border-color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft);
}

.bm-toggle__mark {
  font-size: 13px;
  line-height: 1;
}
</style>
