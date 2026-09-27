<template>
  <div class="quiz-solutions">
    <div v-if="loading" class="weekly-notice" role="status">正在搬题解进来…</div>
    <div v-else-if="error" class="weekly-notice" role="alert">
      题解目录加载失败:{{ error }} <button class="weekly-text-link" @click="load">重试 ↻</button>
    </div>
    <div v-else-if="!problem" class="weekly-notice">
      没找到这道题——链接里的题目记号可能缺失或有误。去
      <a class="weekly-text-link" :href="withBase('/weekly-problems/')">栏目首页 ↗</a> 挑一期吧。
    </div>

    <template v-else>
      <header class="quiz-solutions__head">
        <div class="quiz-solutions__head-copy">
          <p class="quiz-solutions__crumb"><span class="weekly-mono">SOLUTIONS / 题解展区</span></p>
          <h2>{{ problem.title }}</h2>
          <p class="quiz-solutions__meta">
            <span class="quiz-problem__badge">{{ typeLabel }}</span>
            <span class="quiz-solutions__stars">{{ starsText }}</span>
            <span class="weekly-mono">{{ slug }}</span>
          </p>
        </div>
        <a class="weekly-text-link" :href="backLink">← 回去做题</a>
      </header>

      <!-- find-bug:病根行先折着,想完再展开对照 -->
      <details v-if="problem.type === 'find-bug' && bugCodeLines.length" class="quiz-solutions__bug">
        <summary>病根行(先自己想,再展开对照)</summary>
        <div class="quiz-problem__codelines quiz-problem__codelines--answer">
          <div
            v-for="(line, i) in bugCodeLines"
            :key="i"
            class="quiz-problem__codeline"
            :class="{ 'is-buggy': bugLines.includes(i + 1) }"
          >
            <span class="quiz-problem__line-no">{{ i + 1 }}</span>
            <span class="quiz-problem__line-text">{{ line || ' ' }}</span>
          </div>
        </div>
      </details>

      <div class="quiz-solutions__section">
        <span>题解 <b>{{ problem.solutions.length }}</b> 篇 <span class="weekly-mono">/ ONE PER AUTHOR</span></span>
        <span>点卡片,展开这位投稿人的代码与思路</span>
      </div>

      <div class="quiz-solutions__list">
        <article v-for="person in problem.solutions" :key="person" class="quiz-sol">
          <button type="button" class="quiz-sol__head" :aria-expanded="expanded === person" @click="toggle(person)">
            <span class="quiz-sol__avatar" :data-c="colorOf(person)" aria-hidden="true">{{ person[0]?.toUpperCase() }}</span>
            <span class="quiz-sol__author">
              <strong>{{ person }}</strong>
              <a
                v-if="isHandle(person)"
                :href="`https://github.com/${person}`"
                target="_blank"
                rel="noreferrer"
                class="quiz-sol__gh"
                @click.stop
              >GitHub ↗</a>
            </span>
            <span class="quiz-sol__toggle">{{ expanded === person ? '收起 ▲' : '展开 ▼' }}</span>
          </button>

          <div v-if="expanded === person" class="quiz-sol__body">
            <p v-if="entries[person]?.state === 'error'" class="quiz-problem__error" role="alert">
              这份题解加载失败,稍后再点开试试。
            </p>
            <template v-else-if="entries[person]?.state === 'ok'">
              <div class="quiz-sol__grid">
                <div v-if="entries[person]?.codeHtml" class="quiz-sol__code">
                  <div class="quiz-sol__code-bar"><span>▮ solution.cpp</span><span>{{ person }}</span></div>
                  <div class="quiz-sol__code-pre" v-html="entries[person]?.codeHtml"></div>
                </div>
                <div :id="`quiz-sol-note-${safeId(person)}`" class="quiz-sol__note" v-html="entries[person]?.noteHtml"></div>
              </div>
              <div class="quiz-sol__foot">
                <span>对这份解法有质疑,或者想补充别的思路?</span>
                <a class="quiz-sol__issue" :href="issueLink(person)" target="_blank" rel="noreferrer">去 issue 讨论 ↗</a>
              </div>
            </template>
            <p v-else class="quiz-sol__loading" role="status">展开中…</p>
          </div>
        </article>
      </div>

      <p class="quiz-solutions__cta">
        自己也解出来了?
        <a
          class="weekly-text-link"
          href="https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/CONTRIBUTING.md"
          target="_blank"
          rel="noreferrer"
        >一人一个文件夹投稿题解 ↗</a>
      </p>
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { useRoute, withBase } from 'vitepress'

import { fetchRepoText, WEEKLY_MANIFEST_PATH } from '../utils/repo-file'
import { renderMarkdown, highlightCodeBlocks } from '../utils/md-render'
import { highlightCpp } from '../shiki'
import { QUIZ_TYPE_LABELS } from '../utils/quiz-data'
import { problemAnchor } from '../utils/weekly'

interface ManifestProblem {
  src: string
  title: string
  type: string
  stars: number
  solutions: string[]
}
interface ManifestWeek {
  slug: string
  problems?: ManifestProblem[]
}

const REPO = 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP'

const route = useRoute()
const loading = ref(true)
const error = ref('')
const problem = ref<ManifestProblem | null>(null)
const bugLines = ref<number[]>([])
const bugCodeLines = ref<string[]>([])
const expanded = ref('')
// person → { state, codeHtml?, noteHtml? }(reactive:展开后回填即渲染)。
// 缓存只对「当前题目」有效,换题必须清空——否则同作者跨题会端出上一题的题解
const entries = reactive<Record<string, { state: 'loading' | 'ok' | 'error'; codeHtml?: string; noteHtml?: string }>>({})

const rawHash = ref('')
const slug = computed(() => decodeURIComponent(rawHash.value.replace(/^#/, '')))
const typeLabel = computed(() => (problem.value ? QUIZ_TYPE_LABELS[problem.value.type as keyof typeof QUIZ_TYPE_LABELS] ?? problem.value.type : ''))
const starsText = computed(() => {
  const stars = problem.value?.stars ?? 1
  return '★'.repeat(stars) + '☆'.repeat(3 - stars)
})
const backLink = computed(() => {
  const weekSlug = slug.value.split('/')[0]
  return problem.value ? withBase(`/weekly-problems/${weekSlug}#${problemAnchor(problem.value.src)}`) : withBase('/weekly-problems/')
})

async function load(): Promise<void> {
  loading.value = true
  error.value = ''
  problem.value = null
  expanded.value = ''
  // 题目切换:清空上一题的题解缓存(同作者不同题,内容完全不同)
  for (const key of Object.keys(entries)) delete entries[key]
  try {
    const raw = await fetchRepoText(WEEKLY_MANIFEST_PATH)
    const weeks = (JSON.parse(raw) as { weeks?: ManifestWeek[] }).weeks ?? []
    for (const week of weeks) {
      const hit = (week.problems ?? []).find(p => p.src.endsWith(slug.value))
      if (hit) {
        problem.value = hit
        break
      }
    }
    if (problem.value?.type === 'find-bug') {
      // 病根行数据在 quiz.json;code.cpp 是题面代码
      const [quizJson, codeCpp] = await Promise.all([
        fetchRepoText(`${problem.value.src}/quiz.json`),
        fetchRepoText(`${problem.value.src}/code.cpp`),
      ])
      bugLines.value = (JSON.parse(quizJson).bugLines ?? []) as number[]
      bugCodeLines.value = codeCpp.replace(/\n+$/, '').split('\n')
    }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

// hash 是本页的「路由参数」:hashchange 事件 + route.hash 双保险监听,
// 保证从不同题目的入口进来各自加载各自的题解
function onHashChange(): void {
  rawHash.value = window.location.hash
  void load()
}

onMounted(() => {
  rawHash.value = window.location.hash
  void load()
  window.addEventListener('hashchange', onHashChange)
})

onBeforeUnmount(() => {
  window.removeEventListener('hashchange', onHashChange)
})

watch(() => route.hash, () => {
  if (route.hash && route.hash !== rawHash.value) onHashChange()
})

async function toggle(person: string): Promise<void> {
  if (expanded.value === person) {
    expanded.value = ''
    return
  }
  expanded.value = person
  // 首次展开才取数据;已取过的也要等 DOM 挂载后重补高亮
  // (v-if 卸载会重建原始 v-html 节点,之前高亮过的 DOM 随之丢弃)
  if (!entries[person]) {
    entries[person] = { state: 'loading' }
    const base = `${problem.value?.src}/solution/${person}`
    try {
      const noteHtml = renderMarkdown(await fetchRepoText(`${base}/answer.md`))
      const cpp = await fetchRepoText(`${base}/solution.cpp`).catch(() => null)
      const codeHtml = cpp === null ? undefined : await highlightCpp(cpp)
      entries[person] = { state: 'ok', codeHtml, noteHtml }
    } catch {
      entries[person] = { state: 'error' }
      return
    }
  }
  await nextTick()
  const noteEl = document.getElementById(`quiz-sol-note-${safeId(person)}`)
  if (noteEl) await highlightCodeBlocks(noteEl)
}

/** issue 讨论:标题带题目与作者,正文带路径模板 */
function issueLink(person: string): string {
  const title = encodeURIComponent(`[题解讨论] ${problem.value?.title ?? ''} · @${person}`)
  const body = encodeURIComponent(`题目:\`${problem.value?.src ?? ''}\`\n题解作者:@${person}\n\n我想说:\n`)
  return `${REPO}/issues/new?title=${title}&body=${body}`
}

function isHandle(name: string): boolean {
  return /^[a-z\d](?:[a-z\d-]{0,37}[a-z\d])?$/i.test(name)
}

function colorOf(name: string): string {
  let sum = 0
  for (const ch of name) sum += ch.charCodeAt(0)
  return String((sum % 3) + 1)
}

function safeId(name: string): string {
  return name.replace(/[^a-z0-9-]/gi, '')
}
</script>
