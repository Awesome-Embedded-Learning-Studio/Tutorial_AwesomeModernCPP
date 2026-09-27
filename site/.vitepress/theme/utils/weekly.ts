import type { QuizRecord, QuizStatus } from '../composables/useQuizProgress'

export interface WeekProblem { src: string; title: string; type: string; stars: number }
export interface WeeklyContributor { github: string; role: string }
export interface Week { slug: string; title: string; dateRange?: string; description?: string; weeklyThanks?: WeeklyContributor[]; problems: WeekProblem[] }

/** 展示态:在存储的四种状态之上多一个「已跳过」——跳过是旁路标志,盖在底层状态上显示 */
export type QuizDisplayStatus = QuizStatus | 'skipped'

export const quizStatusLabel: Record<QuizDisplayStatus, string> = {
  untouched: '未开始', attempted: '尝试过', revealed: '已看题解', passed: '已通过', skipped: '已跳过',
}
export const quizStatusMark: Record<QuizDisplayStatus, string> = {
  untouched: '○', attempted: '◐', revealed: '◇', passed: '✓', skipped: '∅',
}

/** 跳过时显示「已跳过」;取消跳过后底层状态原样回来,不需要任何迁移 */
export function displayStatusOf(record: QuizRecord | null | undefined): QuizDisplayStatus {
  return record?.skipped ? 'skipped' : (record?.status ?? 'untouched')
}

export function issueNumber(week: Week): string {
  return (week.slug.match(/^week-(\d+)/)?.[1] ?? '1').padStart(2, '0')
}

export function issueTitle(week: Week): string {
  return week.title.replace(/^Week\s*\d+\s*[·:：—-]?\s*/i, '')
}

/** 稳定锚点,不依赖懒加载的题目配置或本期题目数量。 */
export function problemAnchor(src: string): string {
  return `problem-${src.split('/').filter(Boolean).pop()}`
}

export function goToProblem(src: string): void {
  const id = problemAnchor(src)
  const target = document.getElementById(id)
  if (!target) return
  history.replaceState(history.state, '', `#${id}`)
  target.focus({ preventScroll: true })
  target.scrollIntoView({ behavior: window.matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth', block: 'start' })
}

export function resumeProblem(week: Week, records: Record<string, QuizRecord | null>, drafts: Set<string>): WeekProblem | undefined {
  // 跳过的题不再派发(草稿优先级也一并让位);全做完或全跳过时回顾第一题
  const unfinished = week.problems.filter(p => records[p.src]?.status !== 'passed' && !records[p.src]?.skipped)
  return unfinished.find(p => drafts.has(p.src))
    ?? unfinished.find(p => records[p.src]?.status === 'attempted')
    ?? unfinished[0]
    ?? week.problems[0]
}
