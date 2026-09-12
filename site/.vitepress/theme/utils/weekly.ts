import type { QuizRecord, QuizStatus } from '../composables/useQuizProgress'

export interface WeekProblem { src: string; title: string; type: string; stars: number }
export interface WeeklyContributor { github: string; role: string }
export interface Week { slug: string; title: string; dateRange?: string; description?: string; weeklyThanks?: WeeklyContributor[]; problems: WeekProblem[] }

export const quizStatusLabel: Record<QuizStatus, string> = {
  untouched: '未开始', attempted: '尝试过', revealed: '已看题解', passed: '已通过',
}
export const quizStatusMark: Record<QuizStatus, string> = {
  untouched: '○', attempted: '◐', revealed: '◇', passed: '✓',
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
  const unfinished = week.problems.filter(p => records[p.src]?.status !== 'passed')
  return unfinished.find(p => drafts.has(p.src))
    ?? unfinished.find(p => records[p.src]?.status === 'attempted')
    ?? unfinished[0]
    ?? week.problems[0]
}
