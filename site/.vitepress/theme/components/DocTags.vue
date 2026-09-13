<script setup lang="ts">
// 文章页底部标签徽章:读构建期注入的 frontmatter.topicTags(见 config/tags-manifest.ts
// 的 applyTagsPageData——平台/受众全员标签已在构建期滤掉,这里只剩主题标签)。
// 点徽章跳到预筛好的标签索引页(/tags?tag=X)。没有 topicTags 的页面整块不渲染。
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'

const { frontmatter } = useData()
const isEn = computed(() => (frontmatter.value.tagsPageBase ?? '').startsWith('/en/'))
const tags = computed<string[]>(() => {
  const t = frontmatter.value.topicTags
  return Array.isArray(t) ? t.filter((x): x is string => typeof x === 'string') : []
})
const base = computed(() => withBase(frontmatter.value.tagsPageBase ?? '/tags'))
const href = (tag: string) => `${base.value}?tag=${encodeURIComponent(tag)}`
</script>

<template>
  <div v-if="tags.length" class="doc-tags">
    <span class="doc-tags-label">{{ isEn ? 'Tags' : '标签' }}</span>
    <a v-for="tag in tags" :key="tag" class="doc-tag" :href="href(tag)">{{ tag }}</a>
  </div>
</template>

<style scoped>
.doc-tags {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
  margin: 28px 0 20px;
  padding-top: 20px;
  border-top: 1px solid var(--vp-c-divider);
}
.doc-tags-label {
  font-size: 12px;
  color: var(--vp-c-text-3);
}
.doc-tag {
  display: inline-block;
  padding: 2px 10px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 4px;
  background: var(--vp-c-bg-soft);
  color: var(--vp-c-text-2);
  font-size: 12px;
  line-height: 1.8;
  text-decoration: none;
  transition: border-color .15s, color .15s;
}
.doc-tag:hover {
  border-color: var(--vp-c-brand-1);
  color: var(--vp-c-brand-1);
}
.doc-tag:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}
</style>
