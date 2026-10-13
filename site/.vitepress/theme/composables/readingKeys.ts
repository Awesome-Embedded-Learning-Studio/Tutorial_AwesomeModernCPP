// 书签/便签共用的「路径换算 + 跳转恢复标记」。
// 独立成模块是为了让 useBookmarks 与 useNotes 单向依赖它,互不引用(避免循环 import)。

/** 剥掉路径尾部的 .html 后缀:外链常带 .html,站内 SPA 导航是 clean URL,
 *  同一篇文章必须归到同一个键,否则会存出两条书签。 */
export function stripHtmlExt(path: string): string {
  return path.endsWith('.html') ? path.slice(0, -5) : path
}

/** 把 location.pathname 换算成存储键:剥掉部署 base(如 /Tutorial_AwesomeModernCPP/)
 *  与查询/锚点,统一 clean URL。跳转时再 withBase 拼回,换 base 部署旧书签依然有效。 */
export function pathnameToKey(pathname: string): string {
  const base = import.meta.env.BASE_URL ?? '/'
  let p = pathname.split('#')[0].split('?')[0]
  if (base !== '/' && p === base.slice(0, -1)) p = '' // 恰好停在 base 根
  else if (base !== '/' && p.startsWith(base)) p = p.slice(base.length)
  p = stripHtmlExt(p)
  return p.startsWith('/') ? p : `/${p}`
}

// ── 跳转恢复标记:书签页跳转前写入一次性标记,目标页消费后删除 ──

interface RestoreMark {
  path: string
  scrollPercent: number
  /** 便签跳转带的定位指纹(摘录开头 40 字),书签跳转不填 */
  noteLead?: string
}

const RESTORE_KEY = 'bookmarks:restore'
/** 标记有效期:过了这个毫秒数按残留丢弃(书签页写入后用户半路去了别处) */
const RESTORE_TTL_MS = 60 * 60 * 1000

export function markRestore(path: string, scrollPercent: number, noteLead?: string): void {
  if (typeof sessionStorage === 'undefined') return
  try {
    sessionStorage.setItem(RESTORE_KEY, JSON.stringify({ path, scrollPercent, noteLead, ts: Date.now() } satisfies RestoreMark & { ts: number }))
  } catch {
    // 隐私模式:恢复不了就算了,跳转本身不受影响
  }
}

/** 消费恢复标记:当前路径与新鲜度都匹配才返回内容,否则丢弃返回 null */
export function takeRestoreMark(path: string): RestoreMark | null {
  if (typeof sessionStorage === 'undefined') return null
  let mark: (RestoreMark & { ts?: number }) | null = null
  try {
    mark = JSON.parse(sessionStorage.getItem(RESTORE_KEY) ?? 'null')
    sessionStorage.removeItem(RESTORE_KEY)
  } catch {
    try { sessionStorage.removeItem(RESTORE_KEY) } catch { /* 双重失败就不管了 */ }
    return null
  }
  if (!mark || mark.path !== path) return null
  if (typeof mark.ts !== 'number' || Date.now() - mark.ts > RESTORE_TTL_MS) return null
  return mark
}
