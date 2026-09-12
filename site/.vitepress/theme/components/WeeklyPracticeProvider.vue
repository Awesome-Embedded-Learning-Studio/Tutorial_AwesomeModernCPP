<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, provide, ref, watch } from 'vue'
import { useData } from 'vitepress'
import { weeklyPracticeKey } from '../composables/useWeeklyPractice'
import { goToProblem, problemAnchor, type Week } from '../utils/weekly'

const { frontmatter } = useData()
const week = computed(() => frontmatter.value.weeklyIssue as Week | undefined)
const selected = ref('')
const active = computed(() => week.value?.problems.find(p => p.src === selected.value)?.src ?? week.value?.problems[0]?.src ?? '')
const practicing = computed(() => !!week.value?.problems.some(p => p.src === selected.value))
let stopWatch: (() => void) | undefined

function readHash() {
  const problem = week.value?.problems.find(p => `#${problemAnchor(p.src)}` === window.location.hash)
  selected.value = problem?.src ?? ''
}
function overview() {
  selected.value = ''
  history.pushState(history.state, '', window.location.pathname + window.location.search)
  void nextTick(() => {
    window.scrollTo({ top: 0, behavior: 'instant' })
    document.querySelector<HTMLElement>('.weekly-cover h1')?.focus({ preventScroll: true })
  })
}
function select(src: string, focusPanel = false) {
  if (!week.value?.problems.some(p => p.src === src)) return
  const entering = !practicing.value
  selected.value = src
  const hash = `#${problemAnchor(src)}`
  if (window.location.hash !== hash) history.pushState(history.state, '', hash)
  void nextTick(() => {
    if (entering) {
      window.scrollTo({ top: 0, behavior: 'instant' })
      document.getElementById(`tab-${problemAnchor(src)}`)?.focus({ preventScroll: true })
    } else if (focusPanel) goToProblem(src)
    else {
      // 题目切换只调整工作区的位置,封面在入口视图中展示。
      const panel = document.getElementById(problemAnchor(src))
      const nav = document.querySelector('.weekly-nav')
      if (panel && nav && panel.getBoundingClientRect().top < nav.getBoundingClientRect().bottom) {
        panel.scrollIntoView({ block: 'start', behavior: 'auto' })
      }
    }
  })
}
provide(weeklyPracticeKey, { week, active, practicing, select, overview })
onMounted(() => {
  stopWatch = watch(() => week.value?.slug, async () => {
    readHash()
    await nextTick()
    if (window.location.hash.startsWith('#problem-') && active.value) goToProblem(active.value)
  }, { immediate: true })
  window.addEventListener('hashchange', readHash)
  window.addEventListener('popstate', readHash)
})
onBeforeUnmount(() => {
  stopWatch?.()
  window.removeEventListener('hashchange', readHash)
  window.removeEventListener('popstate', readHash)
})
</script>

<template><slot /></template>
