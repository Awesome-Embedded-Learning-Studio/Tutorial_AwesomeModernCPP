import { onMounted, ref, type Ref } from 'vue'

// 「每周一些题」localStorage 进度 + 草稿。
// 无账号无云端:进度跟着浏览器走,换设备即丢
// 所有读写都做 SSR 守卫(构建期无 localStorage),组件里在 onMounted 后加载。

const PROGRESS_KEY = 'weekly-problems:progress'
const DRAFTS_KEY = 'weekly-problems:drafts'
export const QUIZ_PROGRESS_EVENT = 'weekly-problems:change'

export type QuizStatus = 'untouched' | 'attempted' | 'passed' | 'revealed'

export interface QuizRecord {
  status: QuizStatus
  /** 已解锁的提示条数 */
  hintsUsed: number
  updatedAt: number
}

export interface QuizDraft {
  /** 判题类编辑器草稿 */
  source?: string
  /** 填写式草稿 */
  fill?: string
  /** 找 bug 自由标注行(1-based,不判定) */
  marks?: number[]
}

// status 只升不降:通过(passed)是终点;已看答案(revealed)不会回落成「尝试过」——
// 自评「没答上」时保持 revealed,诚实优先
const STATUS_RANK: Record<QuizStatus, number> = { untouched: 0, attempted: 1, revealed: 2, passed: 3 }

function readStore(key: string): Record<string, unknown> {
  if (typeof localStorage === 'undefined') return {}
  try {
    const parsed = JSON.parse(localStorage.getItem(key) ?? '{}')
    return parsed && typeof parsed === 'object' ? parsed : {}
  } catch {
    return {}
  }
}

function writeStore(key: string, value: Record<string, unknown>): boolean {
  if (typeof localStorage === 'undefined') return false
  try {
    localStorage.setItem(key, JSON.stringify(value))
    window.dispatchEvent(new Event(QUIZ_PROGRESS_EVENT))
    return true
  } catch {
    // 隐私模式/配额满:进度存不进去就算了,不阻断做题
    return false
  }
}

function coerceRecord(raw: unknown): QuizRecord | null {
  if (!raw || typeof raw !== 'object') return null
  const r = raw as Partial<QuizRecord>
  if (r.status !== 'untouched' && r.status !== 'attempted' && r.status !== 'passed' && r.status !== 'revealed') return null
  return { status: r.status, hintsUsed: typeof r.hintsUsed === 'number' ? r.hintsUsed : 0, updatedAt: r.updatedAt ?? 0 }
}

export function loadQuizRecord(id: string): QuizRecord | null {
  return coerceRecord(readStore(PROGRESS_KEY)[id])
}

export function saveQuizRecord(id: string, patch: Partial<QuizRecord>): QuizRecord {
  const store = readStore(PROGRESS_KEY)
  const prev = coerceRecord(store[id]) ?? { status: 'untouched' as const, hintsUsed: 0, updatedAt: 0 }
  const status: QuizStatus = patch.status && STATUS_RANK[patch.status] > STATUS_RANK[prev.status]
    ? patch.status
    : prev.status
  const next: QuizRecord = {
    status,
    hintsUsed: Math.max(prev.hintsUsed, patch.hintsUsed ?? 0),
    updatedAt: Date.now(),
  }
  store[id] = next
  writeStore(PROGRESS_KEY, store)
  return next
}

export function loadQuizDraft(id: string): QuizDraft {
  const raw = readStore(DRAFTS_KEY)[id]
  return raw && typeof raw === 'object' ? raw as QuizDraft : {}
}

export function saveQuizDraft(id: string, patch: Partial<QuizDraft>): boolean {
  const store = readStore(DRAFTS_KEY)
  const prev = (store[id] as QuizDraft | undefined) ?? {}
  const next: QuizDraft = { ...prev, ...patch }
  if (!next.source && !next.fill && !next.marks?.length) {
    delete store[id]
  } else {
    store[id] = next
  }
  return writeStore(DRAFTS_KEY, store)
}

/** 重置本题:进度记录与草稿一并抹掉(「做砸了重开」用) */
export function clearQuizData(id: string): void {
  const progress = readStore(PROGRESS_KEY)
  delete progress[id]
  writeStore(PROGRESS_KEY, progress)
  const drafts = readStore(DRAFTS_KEY)
  delete drafts[id]
  writeStore(DRAFTS_KEY, drafts)
}

// 组件侧的响应式封装:record 在 onMounted 后才从 localStorage 加载(SSR/水合安全)
export function useQuizState(id: string): {
  record: Ref<QuizRecord | null>
  update: (patch: Partial<QuizRecord>) => void
  reset: () => void
} {
  const record = ref<QuizRecord | null>(null)
  const update = (patch: Partial<QuizRecord>) => {
    record.value = saveQuizRecord(id, patch)
  }
  const reset = () => {
    clearQuizData(id)
    record.value = null
  }
  onMounted(() => {
    record.value = loadQuizRecord(id)
  })
  return { record, update, reset }
}
