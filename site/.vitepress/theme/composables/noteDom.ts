import { appendNodeText, findLeadIndex, normalizeQuote, type CharSite } from '../utils/noteText'

// 便签的 DOM 定位与高亮(无存储依赖,书签恢复管道与便签存储层共用)。
// 思路:摘录开头 40 字做指纹,跳回文章后在正文里重建「归一化文本↔text node 偏移」
// 的逐字符映射,indexOf 命中即还原成 Range,滚动过去并用 CSS Custom Highlight
// 闪一下(不支持该 API 的浏览器退化为只滚动)。找不到就交回百分比兜底。

/** lead 指纹长度:摘录前 40 字,长到几乎唯一,短到不受改版影响 */
const LEAD_LEN = 40
/** 高亮停留时长 */
const FLASH_MS = 2600
const HIGHLIGHT_NAME = 'note-flash'

/** 从摘录文本算定位指纹(空白归一) */
export function leadOf(quote: string): string {
  return normalizeQuote(quote).slice(0, LEAD_LEN)
}

interface TextIndex {
  big: string
  map: CharSite[]
}

function buildIndex(root: Element): TextIndex {
  const big: string[] = []
  const map: CharSite[] = []
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT)
  let prev = ''
  while (walker.nextNode()) {
    const node = walker.currentNode as Text
    prev = appendNodeText(node.data, prev, big, map, node)
  }
  return { big: big.join(''), map }
}

/** 在正文容器里定位 leadText,命中返回 Range(截到 lead 长度,足够定位与高亮) */
export function locateLead(root: Element, leadText: string): Range | null {
  if (!leadText || typeof document === 'undefined' || !document.createRange) return null
  const { big, map } = buildIndex(root)
  const idx = findLeadIndex(big, leadText)
  if (idx < 0) return null
  const end = Math.min(idx + leadText.length - 1, map.length - 1)
  const start = map[idx]
  const stop = map[end]
  if (!start || !stop) return null
  const range = document.createRange()
  range.setStart(start.node, start.offset)
  range.setEnd(stop.node, stop.offset + 1)
  return range
}

let flashTimer: ReturnType<typeof setTimeout> | null = null

/** 把 Range 滚到视口中央并短暂高亮;返回是否成功 */
export function flashRange(range: Range): boolean {
  const anchor = range.startContainer.parentElement
  if (!anchor) return false
  // 滚三次(立即/300ms/1200ms):与路由切换自身的滚动重置赛跑,单次会被拉回顶部
  // (百分比分支的 scrollToPercent 同款三次校准节奏)
  const scroll = () => anchor.scrollIntoView({ block: 'center' })
  try {
    scroll()
    setTimeout(scroll, 300)
    setTimeout(scroll, 1200)
  } catch {
    return false
  }
  // CSS Custom Highlight API(Chrome 105+/Safari 17.2+),不支持则只滚动
  const CSSLike = (globalThis as { CSS?: { highlights?: Map<string, unknown> } }).CSS
  if (CSSLike?.highlights && typeof Highlight !== 'undefined') {
    try {
      CSSLike.highlights.set(HIGHLIGHT_NAME, new Highlight(range))
      if (flashTimer) clearTimeout(flashTimer)
      flashTimer = setTimeout(() => CSSLike.highlights?.delete(HIGHLIGHT_NAME), FLASH_MS)
    } catch {
      // 高亮失败不影响定位本身
    }
  }
  return true
}

/** 恢复入口:定位 + 滚动 + 高亮。全文找不到指纹返回 false(调用方退百分比兜底)。 */
export function restoreByLeadText(quote: string): boolean {
  if (typeof document === 'undefined') return false
  const root = document.querySelector('.vp-doc') ?? document.querySelector('main')
  if (!root) return false
  const range = locateLead(root, leadOf(quote))
  if (!range) return false
  return flashRange(range)
}
