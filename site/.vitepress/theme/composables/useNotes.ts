import { pathnameToKey } from './readingKeys'
import { normalizeQuote } from '../utils/noteText'

// 「便签」localStorage 存储:选中正文里的一段文字存下来,回头点它跳回原文原处。
// 一篇文章可以存多条;定位靠摘录开头 40 字指纹(noteDom.restoreByLeadText)。
// 无账号无云端,导出/导入随书签备份文件走(version 2 段)。

const NOTES_KEY = 'notes:v1'
export const NOTES_EVENT = 'notes:change'

/** 摘录原文的存储截断长度 */
const QUOTE_MAX = 240

export interface Note {
  /** 唯一 id:path + 创建时刻 + 随机尾,列表 key 与删除都靠它 */
  id: string
  path: string
  title: string
  /** 摘录的原文(空白归一,截 240 字) */
  quote: string
  /** 存的那一刻的滚动百分比:指纹定位失败时的兜底 */
  scrollPercent: number
  createdAt: number
  updatedAt: number
}

function readStore(): Record<string, unknown> {
  if (typeof localStorage === 'undefined') return {}
  try {
    const parsed = JSON.parse(localStorage.getItem(NOTES_KEY) ?? '{}')
    return parsed && typeof parsed === 'object' ? parsed : {}
  } catch {
    return {}
  }
}

function writeStore(value: Record<string, unknown>): boolean {
  if (typeof localStorage === 'undefined') return false
  try {
    localStorage.setItem(NOTES_KEY, JSON.stringify(value))
    window.dispatchEvent(new Event(NOTES_EVENT))
    return true
  } catch {
    return false
  }
}

function coerceNote(raw: unknown): Note | null {
  if (!raw || typeof raw !== 'object') return null
  const n = raw as Partial<Note>
  if (typeof n.id !== 'string' || !n.id || typeof n.path !== 'string' || !n.path) return null
  if (typeof n.quote !== 'string' || !n.quote) return null
  return {
    id: n.id,
    path: n.path.startsWith('/') ? n.path : `/${n.path}`,
    title: typeof n.title === 'string' ? n.title : '',
    quote: n.quote,
    scrollPercent: typeof n.scrollPercent === 'number' ? Math.min(100, Math.max(0, n.scrollPercent)) : 0,
    createdAt: typeof n.createdAt === 'number' ? n.createdAt : 0,
    updatedAt: typeof n.updatedAt === 'number' ? n.updatedAt : 0,
  }
}

/** 全部便签按创建时间降序(最新的在前) */
export function loadNotes(): Note[] {
  return Object.values(readStore())
    .map(coerceNote)
    .filter((n): n is Note => n !== null)
    .sort((a, b) => b.createdAt - a.createdAt)
}

export function removeNote(id: string): void {
  const store = readStore()
  delete store[id]
  writeStore(store)
}

// ── 捕获:选区 → Note ──

/** 选区是否落在正文容器(.vp-doc)内;代码块、表格都在容器里,自然可摘录 */
function selectionInDoc(sel: Selection): boolean {
  if (sel.rangeCount === 0) return false
  const container = sel.getRangeAt(0).commonAncestorContainer
  const el = container.nodeType === Node.TEXT_NODE ? container.parentElement : (container as Element)
  return !!el?.closest?.('.vp-doc')
}

/** 从当前选区捕获一条便签;选区无效(太短/不在正文里)返回 null */
export function captureNoteFromSelection(title: string): Note | null {
  if (typeof window === 'undefined') return null
  const sel = window.getSelection()
  if (!sel || sel.isCollapsed || !selectionInDoc(sel)) return null
  const quote = normalizeQuote(sel.toString()).slice(0, QUOTE_MAX)
  if (quote.length < 4) return null
  const el = document.documentElement
  const max = el.scrollHeight - el.clientHeight
  const percent = max > 0 ? Math.min(100, Math.round((el.scrollTop / max) * 100)) : 0
  const path = pathnameToKey(location.pathname)
  const now = Date.now()
  const note: Note = {
    id: `${path}#${now}${Math.random().toString(36).slice(2, 6)}`,
    path,
    title,
    quote,
    scrollPercent: percent,
    createdAt: now,
    updatedAt: now,
  }
  const store = readStore()
  store[note.id] = note
  writeStore(store)
  return note
}

// ── 备份段:挂在书签备份文件(version 2)的 notes 字段上 ──

/** 导入便签段:按 id 去重(同 id updatedAt 新者胜),返回合并条数 */
export function importNotes(rawNotes: unknown[] | undefined): number {
  if (!Array.isArray(rawNotes)) return 0
  const store = readStore()
  let count = 0
  for (const raw of rawNotes) {
    const incoming = coerceNote(raw)
    if (!incoming) continue
    const local = coerceNote(store[incoming.id])
    if (!local || incoming.updatedAt >= local.updatedAt) {
      store[incoming.id] = incoming
      count++
    }
  }
  writeStore(store)
  return count
}
