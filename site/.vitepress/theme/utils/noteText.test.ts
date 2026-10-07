import test from 'node:test'
import assert from 'node:assert/strict'
import { appendNodeText, normalizeQuote, findLeadIndex } from './noteText'

// appendNodeText 依赖 DOM Text 只用 node.data 与引用,测试里用假对象即可
const fakeText = (data: string) => ({ data } as unknown as Text)

test('appendNodeText 折叠连续空白并记录原始下标', () => {
  const big: string[] = []
  const map: { node: Text; offset: number }[] = []
  const node = fakeText('foo\n  bar\tbaz')
  const prev = appendNodeText(node.data, '', big, map, node)
  assert.equal(big.join(''), 'foo bar baz')
  assert.equal(prev, 'z')
  // 'bar' 的 b 在原始串里下标是 6('\n'1 + 两个空格 = foo=0..2,\n=3,' '=4,' '=5,b=6)
  const bIndex = big.join('').indexOf('bar')
  assert.equal(map[bIndex].offset, 6)
})

test('appendNodeText 跨节点折叠空白', () => {
  const big: string[] = []
  const map: { node: Text; offset: number }[] = []
  const a = fakeText('end ')
  const b = fakeText(' start')
  let prev = appendNodeText(a.data, '', big, map, a)
  prev = appendNodeText(b.data, prev, big, map, b)
  // 两端的空白折叠成一个:期望 'end start'
  assert.equal(big.join(''), 'end start')
})

test('normalizeQuote 与 appendNodeText 规则一致', () => {
  assert.equal(normalizeQuote('  a\n\nb  c\t'), 'a b c')
  const big: string[] = []
  const map: { node: Text; offset: number }[] = []
  appendNodeText('  a\n\nb  c\t', '', big, map, fakeText('x'))
  // 采集侧 trim 了首尾,检索侧 big 尾部可能带空白——findLeadIndex 用 indexOf,首尾差异不影响
  assert.ok(big.join('').includes(normalizeQuote('a b c')))
})

test('findLeadIndex 精确命中', () => {
  assert.equal(findLeadIndex('abcdef', 'cd'), 2)
  assert.equal(findLeadIndex('abcdef', 'zz'), -1)
})

test('findLeadIndex 宽容匹配跳过 lead 开头差异', () => {
  // 正文渲染后行内元素边界可能吃掉 lead 的头一两个字符,尾段仍可定位
  const idx = findLeadIndex('世界你好,这是正文的很长一段', '你好,这是正文的很长一段')
  assert.ok(idx >= 0)
})
