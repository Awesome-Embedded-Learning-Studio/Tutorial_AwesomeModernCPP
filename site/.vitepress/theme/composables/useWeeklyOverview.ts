import { onBeforeUnmount, onMounted, ref, watch, type Ref } from 'vue'
import { loadQuizDraft, loadQuizRecord, QUIZ_PROGRESS_EVENT, type QuizRecord } from './useQuizProgress'
import { displayStatusOf, type Week } from '../utils/weekly'
import { fetchRepoText, WEEKLY_MANIFEST_PATH } from '../utils/repo-file'

// 仅客户端首次请求创建 Promise;双首页挂件共享请求,不在 SSR 进程保存个人状态。
let manifestRequest: Promise<Week[]> | undefined
export async function loadWeeklyCatalog(): Promise<Week[]> {
  if (!manifestRequest) {
    manifestRequest = fetchRepoText(WEEKLY_MANIFEST_PATH).then(raw => {
      const data = JSON.parse(raw)
      if (!Array.isArray(data.weeks)) throw new Error('题目目录格式有误')
      return data.weeks
    }).finally(() => { manifestRequest = undefined })
  }
  return manifestRequest
}

export function useWeeklyOverview(weeks: Ref<Week[]>) {
  const records = ref<Record<string, QuizRecord | null>>({})
  const drafts = ref(new Set<string>())
  let mounted = false
  function refresh() {
    if (!mounted) return
    const next: Record<string, QuizRecord | null> = {}
    const nextDrafts = new Set<string>()
    for (const week of weeks.value) for (const problem of week.problems) {
      next[problem.src] = loadQuizRecord(problem.src)
      const draft = loadQuizDraft(problem.src)
      if (draft.source || draft.fill || draft.marks?.length) nextDrafts.add(problem.src)
    }
    records.value = next
    drafts.value = nextDrafts
  }
  watch(weeks, refresh)
  onMounted(() => {
    mounted = true
    refresh()
    window.addEventListener(QUIZ_PROGRESS_EVENT, refresh)
    window.addEventListener('storage', refresh)
  })
  onBeforeUnmount(() => {
    window.removeEventListener(QUIZ_PROGRESS_EVENT, refresh)
    window.removeEventListener('storage', refresh)
  })
  const statusOf = (src: string) => records.value[src]?.status ?? 'untouched'
  const displayOf = (src: string) => displayStatusOf(records.value[src])
  const doneCount = (week: Week) => week.problems.filter(p => statusOf(p.src) === 'passed').length
  const skippedCount = (week: Week) => week.problems.filter(p => records.value[p.src]?.skipped).length
  const started = (week: Week) => week.problems.some(p => statusOf(p.src) !== 'untouched' || drafts.value.has(p.src))
  return { records, drafts, statusOf, displayOf, doneCount, skippedCount, started }
}
