<script setup lang="ts">
import { computed } from 'vue'
import { useData } from 'vitepress'
import ArticleContributorCard from './ArticleContributorCard.vue'
import type { ArticleContributions } from '../utils/article-contributors'

const props = withDefaults(defineProps<{ variant?: 'meta' | 'full'; automatic?: boolean }>(), { variant: 'full', automatic: false })
const { frontmatter } = useData()
const data = computed(() => frontmatter.value.articleContributions as ArticleContributions | undefined)
const visible = computed(() => !(props.automatic && frontmatter.value.contributors_footer === 'inline'))
</script>

<template>
  <ArticleContributorCard v-if="data && visible" :data="data" :variant="variant" />
</template>
