<script setup lang="ts">
import { computed, ref } from 'vue'
import { useData, withBase } from 'vitepress'
import WeeklyCoverArt from './WeeklyCoverArt.vue'
import WeeklyAcknowledgements from './WeeklyAcknowledgements.vue'
import { useWeeklyOverview } from '../composables/useWeeklyOverview'
import { useWeeklyPractice } from '../composables/useWeeklyPractice'
import { issueNumber, issueTitle, problemAnchor, quizStatusLabel, quizStatusMark, resumeProblem, type Week } from '../utils/weekly'

const { frontmatter } = useData()
const week = computed(() => frontmatter.value.weeklyIssue as Week | undefined)
const weeks = computed(() => week.value ? [week.value] : [])
const { records, drafts, displayOf, doneCount, skippedCount, started } = useWeeklyOverview(weeks)
const target = computed(() => week.value && resumeProblem(week.value, records.value, drafts.value))
const complete = computed(() => !!week.value?.problems.length && doneCount(week.value) === week.value.problems.length)
const action = computed(() => complete.value ? '回顾本期' : week.value && started(week.value) ? `继续：${target.value?.title ?? '本期练习'}` : '开始本期练习')
const practice = useWeeklyPractice()
const active = computed(() => practice?.active.value ?? '')
const practicing = computed(() => practice?.practicing.value ?? false)
const navRef = ref<HTMLElement | null>(null)
function onTabKey(event: KeyboardEvent) {
  const tabs = Array.from(navRef.value?.querySelectorAll<HTMLButtonElement>('[role="tab"]') ?? [])
  const index = tabs.indexOf(event.target as HTMLButtonElement)
  if (index < 0) return
  let next = index
  if (event.key === 'ArrowRight') next = (index + 1) % tabs.length
  else if (event.key === 'ArrowLeft') next = (index - 1 + tabs.length) % tabs.length
  else if (event.key === 'Home') next = 0
  else if (event.key === 'End') next = tabs.length - 1
  else return
  event.preventDefault()
  tabs[next].focus()
  const problem = week.value?.problems[next]
  if (problem) practice?.select(problem.src)
}
</script>

<template>
  <template v-if="week">
    <div class="weekly-masthead">
      <a :href="withBase('/weekly-problems/')" class="weekly-masthead__brand">每周一些题<span> / WEEKLY C++</span></a>
      <a :href="withBase('/weekly-problems/')" class="weekly-text-link">全部期数 <span aria-hidden="true">↗</span></a>
    </div>
    <div v-if="practicing" class="weekly-session">
      <div><p class="weekly-eyebrow">第 {{ issueNumber(week) }} 期 · 练习中</p><h1>{{ issueTitle(week) }}</h1></div>
      <a :href="withBase(`/weekly-problems/${week.slug}`)" class="weekly-text-link" @click.prevent="practice?.overview()">← 本周介绍</a>
    </div>
    <nav v-show="practicing" ref="navRef" class="weekly-nav" role="tablist" aria-label="本期题目" @keydown="onTabKey">
      <button v-for="(problem, index) in week.problems" :key="problem.src" type="button"
        :id="`tab-${problemAnchor(problem.src)}`" role="tab" :aria-controls="problemAnchor(problem.src)" class="weekly-nav__item"
        :class="{ 'is-active': active === problem.src }" :data-status="displayOf(problem.src)"
        :aria-selected="active === problem.src" :tabindex="active === problem.src ? 0 : -1"
        :aria-label="`${index + 1}. ${problem.title}，${quizStatusLabel[displayOf(problem.src)]}`"
        @click="practice?.select(problem.src)">
        <span class="weekly-status-mark" aria-hidden="true">{{ quizStatusMark[displayOf(problem.src)] }}</span>
        <span class="weekly-nav__number">{{ String(index + 1).padStart(2, '0') }}</span>
        <span class="weekly-nav__title">{{ problem.title }}</span>
        <span class="weekly-nav__status">{{ quizStatusLabel[displayOf(problem.src)] }}</span>
      </button>
      <span v-if="!week.problems.length" class="weekly-nav__empty">本期题目准备中</span>
    </nav>
    <header v-show="!practicing" class="weekly-cover">
      <div class="weekly-cover__copy">
        <p class="weekly-eyebrow"><span class="weekly-issue-tag">第 {{ issueNumber(week) }} 期</span><span>{{ week.dateRange }}</span></p>
        <h1 tabindex="-1">{{ issueTitle(week) }}<span class="weekly-cover__period">。</span></h1>
        <WeeklyAcknowledgements :people="week.weeklyThanks ?? []" />
        <p class="weekly-cover__description">{{ week.description || '留一点时间给思考，写几行代码，把想法跑通。' }}</p>
        <div class="weekly-cover__actions">
          <a v-if="target" :href="`#${problemAnchor(target.src)}`" class="weekly-primary" @click.prevent="practice?.select(target.src, true)">{{ action }} <span aria-hidden="true">→</span></a>
          <span class="weekly-cover__count">{{ week.problems.length }} 道题 · 慢慢来，想清楚</span>
        </div>
      </div>
      <WeeklyCoverArt :issue="issueNumber(week)" />
      <div class="weekly-cover__bottom">
        <span><span class="weekly-small-dot" aria-hidden="true"></span>在线练习 · 无需登录</span>
        <span class="weekly-cover__progress" aria-live="polite">
          <span v-if="complete" class="weekly-complete-stamp">✓ 本期完成</span>
          <span>已通过 <b>{{ doneCount(week) }}</b> / {{ week.problems.length }}<template v-if="skippedCount(week)"> · 跳过 {{ skippedCount(week) }}</template></span>
          <span class="weekly-progress-track" aria-hidden="true"><i :style="{ width: `${week.problems.length ? doneCount(week) / week.problems.length * 100 : 0}%` }"></i></span>
        </span>
      </div>
    </header>
    <template v-if="!practicing">
      <div class="weekly-section-heading"><span>本期题目 <span class="weekly-mono">/ PROBLEMS</span></span><span>选一道，开始练习</span></div>
      <div class="weekly-entry-problems">
        <a v-for="(problem, index) in week.problems" :key="problem.src" :href="`#${problemAnchor(problem.src)}`" @click.prevent="practice?.select(problem.src, true)">
          <span class="weekly-nav__number">{{ String(index + 1).padStart(2, '0') }}</span>
          <strong>{{ problem.title }}</strong>
          <span class="weekly-entry-problems__status">{{ quizStatusMark[displayOf(problem.src)] }} {{ quizStatusLabel[displayOf(problem.src)] }}</span>
          <span aria-hidden="true">→</span>
        </a>
      </div>
    </template>
  </template>
</template>
