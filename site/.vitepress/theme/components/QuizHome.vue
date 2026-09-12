<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useData, withBase } from 'vitepress'
import WeeklyCoverArt from './WeeklyCoverArt.vue'
import WeeklyAcknowledgements from './WeeklyAcknowledgements.vue'
import { loadWeeklyCatalog, useWeeklyOverview } from '../composables/useWeeklyOverview'
import { issueNumber, issueTitle, problemAnchor, quizStatusLabel, quizStatusMark, resumeProblem, type Week } from '../utils/weekly'
import { QUIZ_TYPE_LABELS, type QuizType } from '../utils/quiz-data'

const { frontmatter } = useData()
const fallback = ref<Week[]>()
const weeks = computed<Week[]>(() => fallback.value ?? frontmatter.value.weeklyArchive ?? [])
const latest = computed(() => weeks.value[0])
const past = computed(() => weeks.value.slice(1))
const error = ref('')
const loading = ref(!weeks.value.length)
const { records, drafts, doneCount, statusOf, started } = useWeeklyOverview(weeks)
const weekLink = (week: Week) => withBase(`/weekly-problems/${week.slug}`)
function continueLink(week: Week) {
  const target = resumeProblem(week, records.value, drafts.value)
  return `${weekLink(week)}${started(week) && target ? `#${problemAnchor(target.src)}` : ''}`
}
function action(week: Week) {
  return week.problems.length && doneCount(week) === week.problems.length ? '回顾本期' : started(week) ? '继续练习' : '打开本期'
}
async function reload() {
  loading.value = !weeks.value.length
  error.value = ''
  try { fallback.value = await loadWeeklyCatalog() }
  catch { error.value = '题目目录暂时没能加载，稍后再试一次。' }
  finally { loading.value = false }
}
onMounted(() => { void reload() })
</script>

<template>
  <div class="weekly-index">
    <div class="weekly-masthead"><span class="weekly-masthead__brand">每周一些题<span> / WEEKLY C++</span></span><a class="weekly-text-link" href="https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/CONTRIBUTING.md" target="_blank" rel="noreferrer">孩子们我也想来出题！ ↗</a></div>
    <header class="weekly-index__intro">
      <p class="weekly-eyebrow">一周一包题，一点点积累。</p>
      <h1>给代码一点<span>练习的时间。</span></h1>
      <p>写实现，猜输出，找 bug。挑一期感兴趣的，打开浏览器就能开始。</p>
    </header>
    <div v-if="loading" class="weekly-notice" role="status">正在整理题目目录…</div>
    <div v-else-if="error" class="weekly-notice" role="alert">{{ error }} <button class="weekly-text-link" @click="reload">重新加载 ↻</button></div>
    <template v-if="latest">
      <div class="weekly-section-heading"><span>最新一期 <span class="weekly-mono">/ LATEST ISSUE</span><span class="weekly-new-dot" aria-hidden="true"></span></span><span>{{ latest.dateRange }}</span></div>
      <article class="weekly-index__feature">
        <div class="weekly-index__feature-copy">
          <p class="weekly-eyebrow">ISSUE {{ issueNumber(latest) }}<span> / {{ latest.problems.length }} 道题</span></p>
          <h2><a :href="weekLink(latest)">{{ issueTitle(latest) }}</a></h2>
          <WeeklyAcknowledgements :people="latest.weeklyThanks ?? []" />
          <p>{{ latest.description }}</p>
          <a class="weekly-primary" :href="continueLink(latest)">{{ action(latest) }} <span aria-hidden="true">→</span></a>
        </div>
        <a :href="weekLink(latest)" class="weekly-index__art-link" :aria-label="`打开${latest.title}`"><WeeklyCoverArt :issue="issueNumber(latest)" /></a>
        <div class="weekly-index__problems">
          <a v-for="(problem, index) in latest.problems" :key="problem.src" :href="`${weekLink(latest)}#${problemAnchor(problem.src)}`" :data-status="statusOf(problem.src)">
            <span class="weekly-mono">{{ String(index + 1).padStart(2, '0') }}</span>
            <strong>{{ problem.title }}</strong>
            <span class="weekly-index__type">{{ QUIZ_TYPE_LABELS[problem.type as QuizType] || problem.type }}</span>
            <span class="weekly-index__problem-status">{{ quizStatusMark[statusOf(problem.src)] }} {{ quizStatusLabel[statusOf(problem.src)] }}</span>
            <span aria-hidden="true">↗</span>
          </a>
        </div>
      </article>
      <div class="weekly-section-heading weekly-section-heading--archive"><span>往期练习 <span class="weekly-mono">/ ARCHIVE</span></span><span>{{ past.length }} 期</span></div>
      <div v-if="!past.length" class="weekly-index__opening"><span class="weekly-index__opening-mark" aria-hidden="true">01 /</span><div><strong>从这一期开始。</strong><p>栏目刚刚开张，下一次见面时，这里就会多一份练习。</p></div></div>
      <div v-else class="weekly-archive-covers">
        <article v-for="week in past" :key="week.slug" class="weekly-index__feature">
          <div class="weekly-index__feature-copy">
            <p class="weekly-eyebrow">ISSUE {{ issueNumber(week) }}<span>{{ week.dateRange }}</span></p>
            <h2><a :href="weekLink(week)">{{ issueTitle(week) }}</a></h2>
            <WeeklyAcknowledgements :people="week.weeklyThanks ?? []" />
            <p>{{ week.description }}</p>
            <a class="weekly-primary" :href="continueLink(week)">{{ action(week) }} <span aria-hidden="true">→</span></a>
            <small>{{ doneCount(week) }} / {{ week.problems.length }} 已通过</small>
          </div>
          <a :href="weekLink(week)" class="weekly-index__art-link" :aria-label="`打开${week.title}`"><WeeklyCoverArt :issue="issueNumber(week)" /></a>
        </article>
      </div>
    </template>
    <p v-else-if="!loading && !error" class="weekly-notice">第一期正在准备中，过些时候再来看看。</p>
    <footer class="weekly-index__footer"><span>每周一些题，保持一点手感。</span><span>进度保存在本地哦~</span></footer>
  </div>
</template>
