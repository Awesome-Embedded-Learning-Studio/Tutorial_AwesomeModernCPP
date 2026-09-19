<template>
  <section v-show="!practice?.week.value || (practice.practicing.value && practice.active.value === src)" ref="rootRef" :id="problemAnchor(src)" class="quiz-problem" :data-status="displayStatus" tabindex="-1" :role="practice?.week.value ? 'tabpanel' : undefined" :aria-labelledby="practice?.week.value ? `tab-${problemAnchor(src)}` : undefined" :aria-label="config?.title || '练习题'">
    <!-- 加载/坏卡态 -->
    <div v-if="loadError" class="quiz-problem__card quiz-problem__card--broken">
      题目加载失败:{{ loadError }} <button class="quiz-problem__btn" @click="loadProblem">重新加载</button>
    </div>
    <div v-else-if="!config" class="quiz-problem__card quiz-problem__card--loading">题目加载中…</div>

    <template v-else>
      <header class="quiz-problem__meta">
        <span class="quiz-problem__number" aria-hidden="true">{{ problemNumber }}</span>
        <h2 class="quiz-problem__title">{{ config.title }}</h2>
        <span class="quiz-problem__badge">{{ typeLabel }}</span>
        <span class="quiz-problem__stars" :aria-label="`难度 ${config.stars}/3`">{{
          '★'.repeat(config.stars) + '☆'.repeat(3 - config.stars)
        }}</span>
        <span class="quiz-problem__statusbar-spacer"></span>
        <span class="quiz-problem__status-dot" :data-status="displayStatus"></span>
        <span class="quiz-problem__status-text">{{ statusLabel }}</span>
        <button
          type="button"
          class="quiz-problem__skip"
          :aria-pressed="skipped"
          :title="skipped ? '继续练习不再派这道题;点一下恢复原来的状态' : '这题我会了或先放放:继续练习不再派来,草稿和状态都留着,随时可取消'"
          @click="toggleSkip"
        >{{ skipped ? '取消跳过' : '跳过此题' }}</button>
        <button v-if="isJudge" type="button" class="quiz-problem__layout-toggle" :aria-pressed="splitView" @click="splitView = !splitView">{{ splitView ? '☰ 纵向阅读' : '◫ 双栏做题' }}</button>
        <button
          type="button"
          class="quiz-problem__reset"
          :disabled="judging"
          :class="{ 'is-confirming': confirmingReset }"
          :title="confirmingReset ? '再点一次确认:清空本题进度与草稿' : '重置本题(清进度与草稿)'"
          @click="onResetClick"
        >{{ confirmingReset ? '确认重置?' : '↺' }}</button>
      </header>

      <div class="quiz-problem__workspace" :class="{ 'is-split': splitView && isJudge }">
      <!-- 题面(problem.md,客户端渲染 + shiki 补高亮)-->
      <div ref="statementRef" class="quiz-problem__statement" v-html="statementHtml"></div>

      <div class="quiz-problem__card">
        <div v-if="isJudge" class="quiz-problem__editor-bar">
          <span><i aria-hidden="true"></i> solution.cpp</span>
          <span class="quiz-problem__editor-bar-right">
            <button type="button" class="quiz-problem__completion-toggle" :aria-pressed="completionOn" title="打字时自动弹出补全;关闭后仍可随时按 Ctrl+空格 手动唤起" @click="completionOn = !completionOn">自动补全:{{ completionOn ? '开' : '关' }}</button>
            <span :title="`${DEFAULT_JUDGE_COMPILER} ${DEFAULT_JUDGE_OPTIONS}`">gcc · C++23</span>
          </span>
        </div>
        <!-- ── 判题类:编辑器 + 提交 ─────────────────────── -->
        <CppCodeEditor v-if="isJudge" v-model="editorSource" :label="`${config.title}的 C++ 代码编辑器`" :diagnostics="codeCheck?.diagnostics ?? []" :completion="completionOn" @update:model-value="onCodeInput" />

        <div v-if="isJudge" :id="`${problemAnchor(src)}-editor-help`" class="quiz-problem__editor-help"><span>Tab 缩进(弹窗时=接受补全) · Ctrl+空格补全 · Esc 后 Tab 离开</span><span role="status">{{ saveLabel }}</span></div>

        <div v-if="isJudge" class="quiz-problem__actions">
          <button type="button" class="quiz-problem__btn" :disabled="judging" @click="resetEditor">还原初始代码</button>
          <button type="button" class="quiz-problem__btn quiz-problem__check-code" :disabled="checking || judging || cooldownLeft > 0" @click="checkCode">{{ checking ? '检查中…' : '检查代码' }}</button>
          <button
            type="button"
            class="quiz-problem__btn quiz-problem__btn--primary"
            :disabled="judging || checking || cooldownLeft > 0"
            @click="submitJudge"
          >
            <span v-if="judging">判题中…</span>
            <span v-else-if="cooldownLeft > 0">冷却 {{ Math.ceil(cooldownLeft / 1000) }}s…</span>
            <span v-else>提交判题</span>
          </button>
        </div>

        <div v-if="isJudge" class="quiz-problem__code-check">
          <p>本地语法提示仅供参考；「检查代码」将当前代码发送至 Compiler Explorer 编译，不执行程序。</p>
          <p v-if="checkError" role="alert">{{ checkError }}</p>
          <template v-else-if="codeCheck">
            <p role="status">{{ codeCheck.ok ? '✓ 编译检查通过；尚未运行测试用例。' : '发现编译错误，请查看标记与诊断。' }}</p>
            <pre v-if="codeCheck.text"><code>{{ codeCheck.text }}</code></pre>
          </template>
        </div>

        <p v-if="isJudge && judging" class="quiz-problem__progress" role="status">{{ progressText || '正在编译并运行，请稍候…' }}</p>
        <p v-if="judgeError" class="quiz-problem__error" role="alert">{{ judgeError }}</p>

        <div v-if="verdict" class="quiz-problem__verdict" :data-verdict="verdict.status" role="status" aria-live="polite">
          <p class="quiz-problem__verdict-banner">
            <strong>{{ verdictLabel }}</strong>
            <span v-if="verdict.status === 'WA' && verdict.firstFailed">首个未过用例:#{{ verdict.firstFailed }}</span>
            <span v-else-if="verdict.status === 'TLE'">Compiler Explorer 执行时限内没跑完</span>
          </p>
          <ul v-if="verdict.cases.length" class="quiz-problem__cases">
            <li v-for="c in verdict.cases" :key="c.index" :data-case="c.status">
              <span class="quiz-problem__case-mark">{{ caseMark(c.status) }}</span>
              <span class="quiz-problem__case-name">用例 {{ c.index }}{{ c.label ? ` · ${c.label}` : '' }}</span>
              <div v-if="c.status === 'fail'" class="quiz-problem__comparison">
                <div><span>期望输出</span><pre><code>{{ c.expected === '' ? '(空输出)' : c.expected }}</code></pre></div>
                <div><span>实际输出</span><pre><code><template v-for="(part, i) in outputDifference(c.expected ?? '', c.got ?? '')" :key="i"><mark v-if="part.changed">{{ part.text }}</mark><template v-else>{{ part.text }}</template></template></code></pre></div>
              </div>
            </li>
          </ul>
          <pre v-if="verdict.diagnostics" class="quiz-problem__diagnostics"><code>{{ verdict.diagnostics }}</code></pre>
        </div>

        <!-- ── 填写式 ──────────────────────────────────── -->
        <div v-if="config.type === 'fill'" class="quiz-problem__fill">
          <textarea
            v-model="fillText"
            class="quiz-problem__fill-input"
            rows="3"
            placeholder="把程序的输出写在这里…"
            :aria-label="`${config.title}的输出答案`"
            spellcheck="false"
          ></textarea>
          <button type="button" class="quiz-problem__btn quiz-problem__btn--primary" @click="submitFill">对答案</button>
          <p v-if="fillResult === 'correct'" class="quiz-problem__feedback" data-feedback="correct" role="status">✓ 答对了!</p>
          <p v-else-if="fillResult === 'wrong'" class="quiz-problem__feedback" data-feedback="wrong" role="status">
            ✗ 不对,再想想(比对时忽略行尾空白与末尾空行)
          </p>
        </div>

        <!-- ── 选择式(单选/多选)──────────────────────── -->
        <div v-if="config.type === 'choice'" class="quiz-problem__choice">
          <button
            v-for="(opt, i) in config.options"
            :key="i"
            type="button"
            class="quiz-problem__option"
            :aria-pressed="selected.includes(i + 1)"
            :class="{
              'is-chosen': selected.includes(i + 1),
              'is-correct': submitted && config.answer!.includes(i + 1),
              'is-wrong': submitted && selected.includes(i + 1) && !config.answer!.includes(i + 1),
            }"
            @click="pickOption(i + 1)"
          >
            <span class="quiz-problem__option-key">{{ i + 1 }}</span>
            <span class="quiz-problem__option-text">{{ opt }}</span>
          </button>
          <div class="quiz-problem__actions">
            <button
              v-if="isMultiChoice"
              type="button"
              class="quiz-problem__btn quiz-problem__btn--primary"
              :disabled="submitted || selected.length === 0"
              @click="submitChoice"
            >
              提交答案(多选)
            </button>
            <button v-if="submitted" type="button" class="quiz-problem__btn" @click="redoChoice">重做</button>
          </div>
          <p v-if="submitted" class="quiz-problem__feedback" :data-feedback="choiceCorrect ? 'correct' : 'wrong'" role="status">
            {{ choiceCorrect ? '✓ 答对了!' : '✗ 不对——绿色是正确答案' }}
          </p>
        </div>

        <!-- ── 找 bug:题面代码,点行号做自用标注(不判定)── -->
        <div v-if="config.type === 'find-bug'" class="quiz-problem__codelines">
          <p class="quiz-problem__codelines-tip">点行号做自己的标注(想标哪行标哪行,不做判定)——想好了再往下看答案。</p>
          <button
            v-for="(line, i) in bugCodeLines"
            :key="i"
            type="button"
            class="quiz-problem__codeline"
            :aria-pressed="marks.includes(i + 1)"
            :aria-label="`第 ${i + 1} 行：${line}`"
            :class="{ 'is-marked': marks.includes(i + 1) }"
            @click="toggleMark(i + 1)"
          >
            <span class="quiz-problem__line-no">{{ i + 1 }}</span>
            <span class="quiz-problem__line-text">{{ line || ' ' }}</span>
          </button>
        </div>

        <!-- ── 提示(多级,逐条解锁)──────────────────── -->
        <div v-if="config.hints.length" class="quiz-problem__hints">
          <div v-for="(hintHtml, i) in visibleHints" :key="i" class="quiz-problem__hint" v-html="hintHtml"></div>
          <button
            v-if="hintsUnlocked < config.hints.length"
            type="button"
            class="quiz-problem__btn quiz-problem__btn--ghost"
            @click="unlockHint"
          >
            看第 {{ hintsUnlocked + 1 }} 条提示 <span class="weekly-mono">{{ hintsUnlocked }}/{{ config.hints.length }}</span>
          </button>
        </div>

        <!-- ── 题解:独立展区(一人一文件夹),卡片上只留引路链接 ── -->
        <div class="quiz-problem__solution">
          <a class="quiz-problem__solution-link" :href="solutionsLink" @click="onRevealLink">
            看看其他人是如何解决的呢 ↗<span v-if="solutionCount !== null" class="quiz-problem__answer-note">{{ solutionCount }} 篇</span>
          </a>
          <p class="quiz-problem__solution-note">题解在独立展区:每位投稿人一份代码与思路,附 issue 讨论入口;点开会记「已看答案」。</p>
          <div class="quiz-problem__selfeval">
            <template v-if="config.type === 'find-bug' || config.type === 'reveal'">
              <span>自评:</span>
              <button type="button" class="quiz-problem__btn quiz-problem__btn--primary" @click="selfEval(true)">答上了 ✓</button>
              <button type="button" class="quiz-problem__btn" @click="selfEval(false)">没答上 ✗</button>
            </template>
            <template v-else>
              <span>这道题已经在别处做过了?</span>
              <button type="button" class="quiz-problem__btn" @click="selfEval(true)">标记为已通过</button>
            </template>
          </div>
        </div>
        <div v-if="status === 'passed'" class="quiz-problem__next"><span>✓ 这一题，拿下了。</span><a v-if="nextProblem" :href="`#${problemAnchor(nextProblem.src)}`" @click.prevent="practice?.select(nextProblem.src, true)">下一题：{{ nextProblem.title }} →</a><a v-else :href="withBase('/weekly-problems/')">返回历期目录 →</a></div>
        <div v-else-if="skipped" class="quiz-problem__next quiz-problem__next--skipped"><span>这题先跳过——状态和草稿都留着,「取消跳过」随时回来。</span><a v-if="nextProblem" :href="`#${problemAnchor(nextProblem.src)}`" @click.prevent="practice?.select(nextProblem.src, true)">下一题：{{ nextProblem.title }} →</a><a v-else :href="withBase('/weekly-problems/')">返回历期目录 →</a></div>
      </div>
      </div>
    </template>
  </section>
</template>

<script setup lang="ts">
import { watchDebounced } from '@vueuse/core'
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useData, withBase } from 'vitepress'

import CppCodeEditor from './CppCodeEditor.vue'
import { checkCppSource, type CodeCheck } from '../utils/cpp-check'
import { problemAnchor, quizStatusLabel, type QuizDisplayStatus, type Week } from '../utils/weekly'
import { useWeeklyPractice } from '../composables/useWeeklyPractice'
import { outputDifference } from '../utils/output-difference'
import { loadQuizDraft, saveQuizDraft, useQuizState } from '../composables/useQuizProgress'
import {
  JudgeCooldownError,
  DEFAULT_JUDGE_COMPILER,
  DEFAULT_JUDGE_OPTIONS,
  judge,
  judgeCooldownRemaining,
  normalizeIoOutput,
  type JudgeProgress,
  type JudgeVerdict,
} from '../utils/judge'
import { QUIZ_TYPE_LABELS, parseQuizConfig, type QuizConfig } from '../utils/quiz-data'
import { fetchRepoText, WEEKLY_MANIFEST_PATH } from '../utils/repo-file'
import { highlightCodeBlocks, renderMarkdown } from '../utils/md-render'

const props = defineProps<{ src: string }>()
const practice = useWeeklyPractice()
const { frontmatter } = useData()
const splitView = ref(false)
// 补全默认边打字边弹;开关切到「安静模式」(打字不弹,Ctrl+空格 手动唤起仍可用)
const completionOn = ref(true)
const problemNumber = computed(() => props.src.split('/').pop()?.match(/^\d+/)?.[0] ?? '—')
const nextProblem = computed(() => {
  const problems = (frontmatter.value.weeklyIssue as Week | undefined)?.problems ?? []
  const index = problems.findIndex(p => p.src === props.src)
  return index >= 0 ? problems[index + 1] : undefined
})

// ── 加载:题目目录四件套(quiz.json + problem.md 必读;其余按题型/按需)──
const rootRef = ref<HTMLElement | null>(null)
const config = ref<QuizConfig | null>(null)
const statementHtml = ref('')
const statementRef = ref<HTMLElement | null>(null)
const loadError = ref('')
let lazyObserver: IntersectionObserver | null = null

const isJudge = computed(() => config.value?.type === 'judge-assert' || config.value?.type === 'judge-io')
const typeLabel = computed(() => (config.value ? QUIZ_TYPE_LABELS[config.value.type] : ''))

async function loadProblem(): Promise<void> {
  loadError.value = ''
  try {
    const [quizJson, problemMd] = await Promise.all([
      fetchRepoText(`${props.src}/quiz.json`),
      fetchRepoText(`${props.src}/problem.md`),
    ])
    config.value = parseQuizConfig(JSON.parse(quizJson))
    statementHtml.value = renderMarkdown(problemMd)
    await nextTick()
    if (statementRef.value) await highlightCodeBlocks(statementRef.value)

    const draft = loadQuizDraft(props.src)
    if (isJudge.value) {
      const starter = await fetchRepoText(`${props.src}/starter.cpp`)
      starterCode = starter.replace(/\n+$/, '\n').replace(/\n$/, '')
      editorSource.value = draft.source ?? starterCode
    }
    if (config.value.type === 'find-bug') {
      bugCode.value = (await fetchRepoText(`${props.src}/code.cpp`)).replace(/\n+$/, '')
    }
    if (config.value.type === 'fill') fillText.value = draft.fill ?? ''
    if (config.value.type === 'find-bug') marks.value = draft.marks ?? []
    hintsUnlocked.value = record.value?.hintsUsed ?? 0
    // 题解篇数(manifest 有则显示;拿不到就静默隐藏篇数)
    fetchRepoText(WEEKLY_MANIFEST_PATH)
      .then(raw => {
        const weeks = (JSON.parse(raw) as { weeks?: Array<{ problems?: Array<{ src: string; solutions?: string[] }> }> }).weeks ?? []
        for (const week of weeks) {
          const hit = (week.problems ?? []).find(p => p.src === props.src)
          if (hit) {
            solutionCount.value = hit.solutions?.length ?? 0
            break
          }
        }
      })
      .catch(() => {})
  } catch (err) {
    loadError.value = err instanceof Error ? err.message : String(err)
  }
}

onMounted(() => {
  const target = rootRef.value
  if (!target || typeof IntersectionObserver === 'undefined') {
    void loadProblem()
    return
  }
  lazyObserver = new IntersectionObserver(
    entries => {
      if (entries.some(e => e.isIntersecting)) {
        lazyObserver?.disconnect()
        lazyObserver = null
        void loadProblem()
      }
    },
    { rootMargin: '512px' },
  )
  lazyObserver.observe(target)
})

onBeforeUnmount(() => {
  lazyObserver?.disconnect()
  lazyObserver = null
  if (cooldownTicker) { clearInterval(cooldownTicker); cooldownTicker = null }
  if (resetTimer) clearTimeout(resetTimer)
})

// ── 进度(src 路径即题目 id,天然稳定)─────────────────
const { record, update, reset } = useQuizState(props.src)

// ── 重置本题:内联二次确认(第一次点变成「确认重置?」,3s 不点自动回退)──
const confirmingReset = ref(false)
let resetTimer: ReturnType<typeof setTimeout> | null = null

function onResetClick(): void {
  if (!confirmingReset.value) {
    confirmingReset.value = true
    resetTimer = setTimeout(() => (confirmingReset.value = false), 3000)
    return
  }
  if (resetTimer) clearTimeout(resetTimer)
  resetTimer = null
  confirmingReset.value = false
  reset()
  // 界面整体回到初始态
  hintsUnlocked.value = 0
  verdict.value = null
  judgeError.value = ''
  liveProgress.value = null
  if (starterCode) editorSource.value = starterCode
  fillText.value = ''
  fillResult.value = 'none'
  selected.value = []
  submitted.value = false
  marks.value = []
}
const status = computed(() => record.value?.status ?? 'untouched')
const skipped = computed(() => record.value?.skipped ?? false)
// 展示态:跳过盖在底层状态上;「✓ 已通过」这类文案跟着展示态走
const displayStatus = computed<QuizDisplayStatus>(() => (skipped.value ? 'skipped' : status.value))
const statusLabel = computed(() => quizStatusLabel[displayStatus.value])
function toggleSkip(): void {
  update({ skipped: !skipped.value })
}

// ── 提示(多级)──────────────────────────────────────
const hintsUnlocked = ref(0)
const visibleHints = computed(() =>
  config.value ? config.value.hints.slice(0, hintsUnlocked.value).map(renderMarkdown) : [],
)
function unlockHint(): void {
  if (!config.value) return
  hintsUnlocked.value = Math.min(hintsUnlocked.value + 1, config.value.hints.length)
  update({ hintsUsed: hintsUnlocked.value, status: 'attempted' })
}

// ── 题解:链接到独立展区页(/weekly-problems/solutions#<slug>)──
const solutionCount = ref<number | null>(null)
const solutionSlug = computed(() => props.src.replace(/^code\/volumn_codes\/weekly-problems\//, ''))
const solutionsLink = computed(() => withBase(`/weekly-problems/solutions#${solutionSlug.value}`))

function onRevealLink(): void {
  // 导航前落「已看答案」(不禁看,只诚实)
  update({ status: 'revealed' })
}

function selfEval(ok: boolean): void {
  update({ status: ok ? 'passed' : 'attempted' })
}

// ── 判题类:编辑器 + judge() ──────────────────────────
const editorSource = ref('')
const codeCheck = ref<CodeCheck | null>(null)
const checking = ref(false)
const checkError = ref('')
let checkController: AbortController | undefined
let checkDisposed = false
watch(editorSource, () => {
  checkController?.abort()
  codeCheck.value = null
  checkError.value = ''
})
onBeforeUnmount(() => { checkDisposed = true; checkController?.abort() })
const saveState = ref<'idle' | 'saving' | 'saved' | 'unavailable'>('idle')
const saveLabel = computed(() => ({ idle: '草稿自动保存在当前浏览器', saving: '正在保存…', saved: '✓ 草稿已保存', unavailable: '当前浏览器无法保存草稿' })[saveState.value])
function onCodeInput() {
  saveState.value = 'saving'
  if (status.value === 'untouched') update({ status: 'attempted' })
}
let starterCode = ''
const judging = ref(false)
const verdict = ref<JudgeVerdict | null>(null)
const liveProgress = ref<JudgeProgress | null>(null)
const judgeError = ref('')
const cooldownLeft = ref(0)
let cooldownTicker: ReturnType<typeof setInterval> | null = null

function refreshCooldown() {
  cooldownLeft.value = judgeCooldownRemaining()
  if (!cooldownTicker) cooldownTicker = setInterval(() => { cooldownLeft.value = judgeCooldownRemaining() }, 300)
}

async function checkCode() {
  if (checking.value || judging.value || cooldownLeft.value > 0) return
  const source = editorSource.value
  const controller = new AbortController()
  checkController = controller
  checking.value = true
  codeCheck.value = null
  checkError.value = ''
  const timeout = setTimeout(() => controller.abort(), 30000)
  try {
    const result = await checkCppSource(source, controller.signal)
    if (!controller.signal.aborted && editorSource.value === source) codeCheck.value = result
  } catch (error) {
    if (editorSource.value === source) checkError.value = controller.signal.aborted ? '检查已取消或超时，请重试。' : error instanceof Error ? error.message : String(error)
  } finally {
    clearTimeout(timeout)
    checking.value = false
    if (!checkDisposed) refreshCooldown()
  }
}

const verdictLabel = computed(
  () =>
    ({
      AC: '✓ 全部用例通过',
      WA: '× 输出与预期不一致',
      CE: '编译失败 · 检查下面的诊断',
      RE: '程序运行出错',
      TLE: '执行超时',
      ERROR: '判题服务暂时不可用',
    })[verdict.value?.status ?? 'ERROR'],
)
const progressText = computed(() =>
  liveProgress.value ? `用例 ${liveProgress.value.running}/${liveProgress.value.total} …` : '',
)

function caseMark(state: string): string {
  return state === 'pass' ? '✓' : state === 'fail' ? '✗' : state === 'running' ? '…' : '·'
}

async function submitJudge(): Promise<void> {
  if (!config.value?.mode || judging.value || cooldownLeft.value > 0) return
  judging.value = true
  verdict.value = null
  liveProgress.value = null
  judgeError.value = ''
  update({ status: 'attempted' })
  try {
    const result = await judge(
      {
        source: editorSource.value,
        mode: config.value.mode,
        tests: config.value.tests as never,
      },
      { onProgress: p => (liveProgress.value = p) },
    )
    verdict.value = result
    if (result.status === 'AC') update({ status: 'passed' })
  } catch (err) {
    if (err instanceof JudgeCooldownError) {
      cooldownLeft.value = err.cooldownRemaining
    } else {
      judgeError.value = err instanceof Error ? err.message : String(err)
    }
  } finally {
    judging.value = false
    // 判题进行中离开页面:别在已卸载组件上重建冷却定时器(泄漏)
    if (!checkDisposed) refreshCooldown()
  }
}

function resetEditor(): void {
  editorSource.value = starterCode
  verdict.value = null
  judgeError.value = ''
}

watchDebounced(editorSource, source => {
  if (!isJudge.value) return
  // 与 starter 一致的草稿不存(还原初始代码即清空草稿)
  const saved = saveQuizDraft(props.src, source === starterCode ? { source: undefined } : { source })
  saveState.value = saved ? source === starterCode ? 'idle' : 'saved' : 'unavailable'
}, { debounce: 500 })

// ── 填写式 ────────────────────────────────────────────
const fillText = ref('')
const fillResult = ref<'none' | 'correct' | 'wrong'>('none')
function submitFill(): void {
  if (!config.value?.answerText) return
  const ok = normalizeIoOutput(fillText.value) === normalizeIoOutput(config.value.answerText)
  fillResult.value = ok ? 'correct' : 'wrong'
  update({ status: ok ? 'passed' : 'attempted' })
}
watchDebounced(fillText, fill => {
  if (config.value?.type === 'fill') saveQuizDraft(props.src, { fill })
}, { debounce: 500 })

// ── 选择式 ────────────────────────────────────────────
const selected = ref<number[]>([])
const submitted = ref(false)
const isMultiChoice = computed(() => (config.value?.answer?.length ?? 1) > 1)
const choiceCorrect = computed(() => {
  if (!config.value?.answer) return false
  const a = [...config.value.answer].sort((x, y) => x - y)
  const b = [...selected.value].sort((x, y) => x - y)
  return a.length === b.length && a.every((v, i) => v === b[i])
})
function pickOption(n: number): void {
  if (submitted.value) return
  if (isMultiChoice.value) {
    selected.value = selected.value.includes(n) ? selected.value.filter(x => x !== n) : [...selected.value, n]
  } else {
    selected.value = [n]
    submitted.value = true
    update({ status: choiceCorrect.value ? 'passed' : 'attempted' })
  }
}
function submitChoice(): void {
  submitted.value = true
  update({ status: choiceCorrect.value ? 'passed' : 'attempted' })
}
function redoChoice(): void {
  selected.value = []
  submitted.value = false
}

// ── 找 bug:自由标注(不判定)────────────────────────
const marks = ref<number[]>([])
const bugCode = ref('')
const bugCodeLines = computed(() => bugCode.value.split('\n'))
function toggleMark(n: number): void {
  marks.value = marks.value.includes(n) ? marks.value.filter(x => x !== n) : [...marks.value, n]
}
watchDebounced(marks, value => {
  if (config.value?.type === 'find-bug') saveQuizDraft(props.src, { marks: value.length ? value : undefined })
}, { debounce: 500 })
</script>
