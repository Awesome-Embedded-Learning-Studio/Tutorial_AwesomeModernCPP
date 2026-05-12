<script setup lang="ts">
import { computed } from 'vue'
import { useData } from 'vitepress'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))
</script>

<template>
  <div class="home-tip-banner">
    <span class="home-tip-icon">
      <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M15 14c.2-1 .7-1.7 1.5-2.5 1-.9 1.5-2.2 1.5-3.5A6 6 0 0 0 6 8c0 1 .2 2.2 1.5 3.5.7.7 1.3 1.5 1.5 2.5"/>
        <path d="M9 18h6"/>
        <path d="M10 22h4"/>
      </svg>
    </span>
    <span class="home-tip-text">
      <template v-if="isEn">
        Tool versions, APIs, and configurations in this tutorial may change over time. We recommend verifying against
        <a href="https://en.cppreference.com/" target="_blank" rel="noopener noreferrer">the official documentation</a>
        and treating the tutorial content as a reference for ideas rather than an absolute standard.
      </template>
      <template v-else>
        本教程中的工具版本、API 和配置可能随时间变化，建议对照
        <a href="https://zh.cppreference.com/" target="_blank" rel="noopener noreferrer">官方文档</a>
        进行验证，将教程内容作为思路参考而非绝对标准。
      </template>
    </span>
  </div>
</template>

<style scoped>
.home-tip-banner {
  max-width: 1152px;
  margin: -16px auto 32px;
  padding: 12px 20px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  gap: 10px;
  background-color: var(--vp-tip-bg, rgba(83, 162, 83, 0.08));
  border: 1px solid var(--vp-tip-border, rgba(83, 162, 83, 0.2));
  color: var(--vp-tip-text, var(--vp-c-text-2));
  font-size: 14px;
  line-height: 1.6;
}

.home-tip-icon {
  flex-shrink: 0;
  color: var(--vp-tip-accent, #53a253);
  display: flex;
  align-items: center;
}

.home-tip-text a {
  color: var(--vp-tip-accent, #53a253);
  text-decoration: underline;
  text-underline-offset: 2px;
  font-weight: 500;
}

.home-tip-text a:hover {
  color: var(--vp-tip-accent-hover, #3d7a3d);
}
</style>
