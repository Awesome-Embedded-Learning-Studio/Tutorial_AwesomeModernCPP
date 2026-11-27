<script setup lang="ts">
/**
 * 首页「正在做的工作」：挂在全部卷卡片之后，直接读仓库的开放 Issue。
 * 看板本体来自 @aels/loupe-ui，这里只负责标题和容器宽度。
 */
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'
import { IssueBoard } from '@aels/loupe-ui'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))

const t = computed(() =>
  isEn.value
    ? {
        title: 'Work in Progress',
        note: 'Open issues from the repository, most recently updated first',
        more: 'About the development process',
        moreLink: '/en/community/dev/',
      }
    : {
        title: '正在做的工作',
        note: '仓库的开放 Issue，最近更新的排在前面',
        more: '了解项目怎么维护',
        moreLink: '/community/dev/',
      },
)
</script>

<template>
  <section id="issues" class="home-issues">
    <header class="hi-head">
      <h2 class="hi-title">{{ t.title }}</h2>
      <p class="hi-note">{{ t.note }}</p>
      <a class="hi-more" :href="withBase(t.moreLink)">{{ t.more }}</a>
    </header>

    <IssueBoard repo="Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP" :en="isEn" />
  </section>
</template>

<style scoped>
.home-issues {
  max-width: 1152px;
  margin: 8px auto 56px;
  padding: 0 24px;
  scroll-margin-top: 80px;
}

.hi-head {
  display: flex;
  align-items: baseline;
  flex-wrap: wrap;
  gap: 10px 16px;
}

.hi-title {
  margin: 0;
  font-size: 20px;
  font-weight: 700;
  line-height: 1.4;
  color: var(--vp-c-text-1);
}

.hi-note {
  margin: 0;
  font-size: 13px;
  color: var(--vp-c-text-3);
}

.hi-more {
  margin-left: auto;
  font-size: 13px;
  font-weight: 500;
  color: var(--vp-c-brand-1);
  text-decoration: none;
  transition: color 0.25s;
}

.hi-more:hover {
  color: var(--vp-c-brand-2);
}

.home-issues :deep(.issue-board) {
  margin-top: 12px;
}

@media (max-width: 639px) {
  .home-issues {
    padding: 0 16px;
    margin: 0 auto 36px;
  }

  .hi-note {
    display: none;
  }

  .home-issues :deep(.issue-board) {
    margin-left: -16px;
    margin-right: -16px;
  }
}
</style>
