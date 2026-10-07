// 便签锚点的文本归一化与索引映射(纯逻辑,可单测)。
//
// 问题:选区拿到的 text 是排版后的(displayed),正文 DOM 里被行内元素
// (code/strong/a)切成多个 text node,空白也随源码换行散落。要在跳转回来后
// 把「摘录开头 40 字」重新定位成 DOM Range,需要一份「归一化大字符串 ↔ 原始
// text node 偏移」的逐字符映射。
//
// 归一化规则(采集与检索两边必须一致):
//   连续空白(空格/换行/制表)折叠为单个空格;text node 边界处同样折叠。

export interface CharSite {
  node: Text
  /** 该归一化字符在 node.data 里的原始下标(归一坐标→原始坐标的换算在这里完成) */
  offset: number
}

/** 逐字符折叠一个 text node 的内容,追加进 big/map。prevChar 传上一个字符,处理跨节点折叠。 */
export function appendNodeText(
  data: string,
  prevChar: string,
  big: string[],
  map: CharSite[],
  node: Text,
): string {
  let prev = prevChar
  for (let i = 0; i < data.length; i++) {
    const ch = data[i]
    if (/\s/.test(ch)) {
      if (prev === ' ') continue // 连续空白:折叠
      big.push(' ')
      map.push({ node, offset: i })
      prev = ' '
    } else {
      big.push(ch)
      map.push({ node, offset: i })
      prev = ch
    }
  }
  return prev
}

/** 选区/摘录文本的归一化(与 appendNodeText 同规则) */
export function normalizeQuote(text: string): string {
  return text.replace(/\s+/g, ' ').trim()
}

/** 在 big 里找 leadText 的首选位置;找不到时做「跳过 leadText 首字符」的宽容匹配
 *  (正文行首的列表标记、标点前的空白差异兜底)。返回下标或 -1。 */
export function findLeadIndex(big: string, leadText: string): number {
  const idx = big.indexOf(leadText)
  if (idx >= 0) return idx
  // 宽容:leadText 首字符后可能紧跟标点差异,试去掉 lead 首字符再搜其尾段
  if (leadText.length > 8) {
    const tail = leadText.slice(2)
    const t = big.indexOf(tail)
    if (t >= 0) return t - 2 >= 0 ? t - 2 : t
  }
  return -1
}
