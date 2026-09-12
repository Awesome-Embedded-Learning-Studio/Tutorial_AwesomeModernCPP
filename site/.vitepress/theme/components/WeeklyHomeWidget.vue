<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { withBase } from 'vitepress'
import { loadWeeklyCatalog, useWeeklyOverview } from '../composables/useWeeklyOverview'
import { issueNumber, issueTitle, problemAnchor, resumeProblem, type Week } from '../utils/weekly'

const weeks = ref<Week[]>([])
const latest = computed(() => weeks.value[0])
const { records, drafts, doneCount, started } = useWeeklyOverview(weeks)
const complete = computed(() => latest.value?.problems.length && doneCount(latest.value) === latest.value.problems.length)
const link = computed(() => {
  if (!latest.value) return withBase('/weekly-problems/')
  const target = resumeProblem(latest.value, records.value, drafts.value)
  return withBase(`/weekly-problems/${latest.value.slug}${started(latest.value) && target ? `#${problemAnchor(target.src)}` : ''}`)
})
onMounted(async () => {
  try { weeks.value = await loadWeeklyCatalog() } catch { /* 首页仍可通过栏目导航进入 */ }
})
</script>

<template>
  <div v-if="latest" class="weekly-widget">
    <a :href="link" class="weekly-widget__link">
      <span class="weekly-widget__issue"><small>WEEK</small>{{ issueNumber(latest) }}</span>
      <span class="weekly-widget__copy"><span class="weekly-widget__label">每周一些题 <i aria-hidden="true"></i></span><strong>{{ issueTitle(latest) }}</strong></span>
      <span class="weekly-widget__progress"><span>{{ doneCount(latest) }} / {{ latest.problems.length }} 已通过</span><span class="weekly-widget__segments" aria-hidden="true"><i v-for="p in latest.problems" :key="p.src" :class="{ 'is-done': records[p.src]?.status === 'passed' }"></i></span></span>
      <span class="weekly-widget__go">{{ complete ? '回顾本期' : started(latest) ? '继续练习' : '打开本期' }} <span aria-hidden="true">→</span></span>
    </a>
  </div>
</template>
