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
  /** 跳过标志:旁路于状态链——这题先放放/已会,「继续练习」不再派发;随时可取消,底层状态原样保留 */
  skipped?: boolean
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
  return {
    status: r.status,
    hintsUsed: typeof r.hintsUsed === 'number' ? r.hintsUsed : 0,
    updatedAt: r.updatedAt ?? 0,
    ...(typeof r.skipped === 'boolean' ? { skipped: r.skipped } : {}),
  }
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
    // 跳过是开关不是等级:patch 显式给 false 即取消,不走只升不降的 rank
    skipped: patch.skipped ?? prev.skipped,
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

// ── 备份:进度跟浏览器走,换设备/清缓存前导出一份,到新设备再导回来 ──

export interface QuizBackup {
  app: 'weekly-problems'
  version: 1
  exportedAt: string
  progress: Record<string, unknown>
  drafts: Record<string, unknown>
}

export function exportQuizData(): string {
  const backup: QuizBackup = {
    app: 'weekly-problems',
    version: 1,
    exportedAt: new Date().toISOString(),
    progress: readStore(PROGRESS_KEY),
    drafts: readStore(DRAFTS_KEY),
  }
  return JSON.stringify(backup)
}

export interface QuizImportResult {
  /** 合并进本浏览器的进度条数 */
  progress: number
  /** 覆盖写入的草稿条数 */
  drafts: number
}

/** 导入备份:进度按题合并(updatedAt 新者胜,两边都做题也不丢);草稿以文件为准覆盖同名题。格式不对返回 null。 */
export function importQuizData(raw: string): QuizImportResult | null {
  let parsed: unknown
  try {
    parsed = JSON.parse(raw)
  } catch {
    return null
  }
  const data = parsed && typeof parsed === 'object' ? (parsed as Partial<QuizBackup>) : null
  if (!data || !data.progress || !data.drafts || typeof data.progress !== 'object' || typeof data.drafts !== 'object') return null

  const progress = readStore(PROGRESS_KEY)
  let progressCount = 0
  for (const [id, rawRecord] of Object.entries(data.progress)) {
    const incoming = coerceRecord(rawRecord)
    if (!incoming) continue
    const local = coerceRecord(progress[id])
    if (!local || incoming.updatedAt >= local.updatedAt) {
      progress[id] = incoming
      progressCount++
    }
  }
  writeStore(PROGRESS_KEY, progress)

  const drafts = readStore(DRAFTS_KEY)
  let draftsCount = 0
  for (const [id, rawDraft] of Object.entries(data.drafts)) {
    const draft = rawDraft && typeof rawDraft === 'object' ? (rawDraft as Partial<QuizDraft>) : null
    if (!draft || (draft.source === undefined && draft.fill === undefined && !draft.marks?.length)) continue
    drafts[id] = draft
    draftsCount++
  }
  writeStore(DRAFTS_KEY, drafts)
  return { progress: progressCount, drafts: draftsCount }
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
