<script setup lang="ts">
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))
const link = computed(() => withBase('/roadmap/'))
</script>

<template>
  <div class="home-tip-banner">
    <span class="home-tip-icon">
      <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <circle cx="12" cy="12" r="10" />
        <polygon points="16.24 7.76 14.12 14.12 7.76 16.24 9.88 9.88 16.24 7.76" />
      </svg>
    </span>
    <span class="home-tip-text">
      <template v-if="isEn">
        Not sure where to start?
        <a :href="link">Read the learning roadmap →</a>
      </template>
      <template v-else>
        不知道从哪里开始？
        <a :href="link">点击查看学习路线图 →</a>
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
  background-color: var(--vp-tip-bg, rgba(63, 81, 181, 0.08));
  border: 1px solid var(--vp-tip-border, rgba(63, 81, 181, 0.2));
  color: var(--vp-tip-text, var(--vp-c-text-2));
  font-size: 14px;
  line-height: 1.6;
}

.home-tip-icon {
  flex-shrink: 0;
  color: var(--vp-tip-accent, #3f51b5);
  display: flex;
  align-items: center;
}

.home-tip-text a {
  color: var(--vp-tip-accent, #3f51b5);
  text-decoration: underline;
  text-underline-offset: 2px;
  font-weight: 600;
  margin-left: 2px;
}

.home-tip-text a:hover {
  color: var(--vp-tip-accent-hover, #303f9f);
}
</style>
