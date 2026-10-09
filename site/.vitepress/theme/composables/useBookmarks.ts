import { onBeforeUnmount, onMounted, ref, type Ref } from 'vue'
import { subscribeAfterRouteChange, subscribeBeforeRouteChange } from '../router-hooks'
import { pathnameToKey, stripHtmlExt, takeRestoreMark } from './readingKeys'
import { restoreByLeadText } from './noteDom'
import { loadNotes, importNotes, type Note } from './useNotes'

// 「书签」localStorage 存储:文章级收藏,想回头再看的文章一枚星标。
// 无账号无云端:书签跟着浏览器走,换设备即丢(导出/导入 JSON 搬运)。
// 所有读写都做 SSR 守卫(构建期无 localStorage),组件里在 onMounted 后加载。
//
// path 统一存剥掉部署 base 的 clean 路径(见 readingKeys.pathnameToKey)。

const BOOKMARKS_KEY = 'bookmarks:v1'
export const BOOKMARKS_EVENT = 'bookmarks:change'

export interface Bookmark {
  /** 文章路径(clean URL,如 /vol1-fundamentals/ch01/02-xxx) */
  path: string
  /** 文章标题(收藏时从页面 frontmatter 取,不带站名后缀) */
  title: string
  /** 上次读到的滚动百分比 0-100(自动跟随会更新;恢复的兜底定位) */
  scrollPercent: number
  createdAt: number
  updatedAt: number
}

function readStore(): Record<string, unknown> {
  if (typeof localStorage === 'undefined') return {}
  try {
    const parsed = JSON.parse(localStorage.getItem(BOOKMARKS_KEY) ?? '{}')
    return parsed && typeof parsed === 'object' ? parsed : {}
  } catch {
    return {}
  }
}

function writeStore(value: Record<string, unknown>): boolean {
  if (typeof localStorage === 'undefined') return false
  try {
    localStorage.setItem(BOOKMARKS_KEY, JSON.stringify(value))
    window.dispatchEvent(new Event(BOOKMARKS_EVENT))
    return true
  } catch {
    // 隐私模式/配额满:书签存不进去就算了,不阻断阅读
    return false
  }
}

function coerceBookmark(raw: unknown): Bookmark | null {
  if (!raw || typeof raw !== 'object') return null
  const b = raw as Partial<Bookmark>
  if (typeof b.path !== 'string' || !b.path || typeof b.title !== 'string') return null
  const percent = typeof b.scrollPercent === 'number' ? b.scrollPercent : 0
  return {
    path: stripHtmlExt(b.path.startsWith('/') ? b.path : `/${b.path}`),
    title: b.title,
    scrollPercent: Math.min(100, Math.max(0, percent)),
    createdAt: typeof b.createdAt === 'number' ? b.createdAt : 0,
    updatedAt: typeof b.updatedAt === 'number' ? b.updatedAt : 0,
  }
}

/** 全部书签,按更新时间降序(最近在读的排前面) */
export function loadBookmarks(): Bookmark[] {
  return Object.values(readStore())
    .map(coerceBookmark)
    .filter((b): b is Bookmark => b !== null)
    .sort((a, b) => b.updatedAt - a.updatedAt)
}

/** 收藏/更新一条:同路径已存在则刷新标题与位置,createdAt 保留 */
export function saveBookmark(input: { path: string; title: string; scrollPercent: number }): Bookmark | null {
  if (!input.path) return null
  const store = readStore()
  const prev = coerceBookmark(store[input.path])
  const now = Date.now()
  const next: Bookmark = {
    path: input.path,
    title: input.title,
    scrollPercent: Math.min(100, Math.max(0, input.scrollPercent)),
    createdAt: prev?.createdAt ?? now,
    updatedAt: now,
  }
  store[input.path] = next
  writeStore(store)
  return next
}

export function removeBookmark(path: string): void {
  const store = readStore()
  delete store[path]
  writeStore(store)
}

// ── 备份:书签跟浏览器走,换设备/清缓存前导出一份,到新设备再导回来 ──
// 备份文件 v2 含便签(notes)段;导入侧兼容只有 bookmarks 的 v1 旧文件。

export interface BookmarksBackup {
  app: 'site-bookmarks'
  version: 1 | 2
  exportedAt: string
  bookmarks: Bookmark[]
  /** version 2 起携带的便签;v1 旧文件没有此段 */
  notes?: Note[]
}

export function exportBookmarks(): string {
  const backup: BookmarksBackup = {
    app: 'site-bookmarks',
    version: 2,
    exportedAt: new Date().toISOString(),
    bookmarks: loadBookmarks(),
    notes: loadNotes(),
  }
  return JSON.stringify(backup)
}

export interface ImportResult {
  /** 合并进本浏览器的书签条数 */
  bookmarks: number
  /** 合并进本浏览器的便签条数(v1 旧文件为 0) */
  notes: number
}

/** 导入备份:书签同路径 updatedAt 新者胜,便签按 id 去重(见 useNotes)。格式不对返回 null。 */
export function importBookmarks(raw: string): ImportResult | null {
  let parsed: unknown
  try {
    parsed = JSON.parse(raw)
  } catch {
    return null
  }
  const data = parsed && typeof parsed === 'object' ? (parsed as Partial<BookmarksBackup>) : null
  if (!data || data.app !== 'site-bookmarks' || !Array.isArray(data.bookmarks)) return null

  const store = readStore()
  let bookmarkCount = 0
  for (const rawBookmark of data.bookmarks) {
    const incoming = coerceBookmark(rawBookmark)
    if (!incoming) continue
    const local = coerceBookmark(store[incoming.path])
    if (!local || incoming.updatedAt >= local.updatedAt) {
      store[incoming.path] = incoming
      bookmarkCount++
    }
  }
  writeStore(store)
  const noteCount = importNotes(data.notes)
  return { bookmarks: bookmarkCount, notes: noteCount }
}

// ── 跳转恢复:书签页跳过来后把滚动位置还原;便签跳转优先文本定位 ──

/** 滚动到百分比:rAF/300ms/1200ms 三次校准(图片与 mermaid 异步撑高,单次会落错位置) */
function scrollToPercent(percent: number): void {
  const attempt = () => {
    const el = document.documentElement
    const max = el.scrollHeight - el.clientHeight
    if (max > 0) el.scrollTop = (max * percent) / 100
  }
  requestAnimationFrame(attempt)
  setTimeout(attempt, 300)
  setTimeout(attempt, 1200)
}

/**
 * 全局安装书签恢复(在 theme setup() 里调用一次,与 setupMermaid 同模式)。
 * 必须在 setup 上下文执行:内部的路由订阅依赖 useRouter()。
 */
export function setupBookmarkRestore(): void {
  subscribeAfterRouteChange(() => {
    const mark = takeRestoreMark(pathnameToKey(location.pathname))
    if (!mark) return
    // 便签:先试文本指纹定位(能精确到段落并高亮),失败退回百分比
    if (mark.noteLead && restoreByLeadText(mark.noteLead)) return
    scrollToPercent(mark.scrollPercent)
  })
}

/**
 * 收藏位置自动跟随:离开页面时,若当前文章已被收藏,静默把读到的地方记下来。
 * 下次从书签进来就落在最新进度。未收藏的文章不记(不制造隐私残留)。
 */
export function setupBookmarkAutoTrack(): void {
  const snapshot = (): void => {
    const key = pathnameToKey(location.pathname)
    const prev = coerceBookmark(readStore()[key])
    if (!prev) return
    const el = document.documentElement
    const max = el.scrollHeight - el.clientHeight
    const percent = max > 0 ? Math.min(100, Math.round((el.scrollTop / max) * 100)) : 0
    // 页首(刚进来就跳走)不写,避免把有效进度覆盖成 0
    if (percent <= 0) return
    saveBookmark({ path: key, title: prev.title, scrollPercent: percent })
  }
  subscribeBeforeRouteChange(snapshot)
  if (typeof window !== 'undefined') {
    window.addEventListener('pagehide', snapshot)
  }
}

// 组件侧的响应式封装:当前页的书签状态在 onMounted 后加载(SSR/水合安全)
export function useBookmarkState(): {
  bookmark: Ref<Bookmark | null>
  currentPath: () => string
  toggle: (title: string, scrollPercent: number) => void
} {
  const bookmark = ref<Bookmark | null>(null)
  const currentPath = () => (typeof location !== 'undefined' ? pathnameToKey(location.pathname) : '')
  const toggle = (title: string, scrollPercent: number) => {
    const path = currentPath()
    if (bookmark.value && bookmark.value.path === path) {
      removeBookmark(path)
      bookmark.value = null
    } else {
      bookmark.value = saveBookmark({ path, title, scrollPercent })
    }
  }
  const sync = () => {
    bookmark.value = coerceBookmark(readStore()[currentPath()]) ?? null
  }
  // 按钮挂导航栏,SPA 切页时实例保留不重建,路由变了要重新加载
  subscribeAfterRouteChange(sync)
  onMounted(() => {
    sync()
    window.addEventListener(BOOKMARKS_EVENT, sync)
  })
  onBeforeUnmount(() => {
    window.removeEventListener(BOOKMARKS_EVENT, sync)
  })
  return { bookmark, currentPath, toggle }
}
