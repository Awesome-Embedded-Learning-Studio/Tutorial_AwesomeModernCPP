/** 首尾相同部分保留,突出中间差异。按码点切分,不会拆开 Unicode 字符。 */
export function outputDifference(expected: string, actual: string): Array<{ text: string; changed: boolean }> {
  if (!actual) return [{ text: '(空输出)', changed: expected !== actual }]
  const left = Array.from(expected)
  const right = Array.from(actual)
  let prefix = 0
  while (prefix < left.length && prefix < right.length && left[prefix] === right[prefix]) prefix++
  let suffix = 0
  while (suffix < left.length - prefix && suffix < right.length - prefix && left[left.length - 1 - suffix] === right[right.length - 1 - suffix]) suffix++
  return [
    { text: right.slice(0, prefix).join(''), changed: false },
    { text: right.slice(prefix, right.length - suffix).join('') || (left.length > right.length ? '〔缺少内容〕' : ''), changed: true },
    { text: suffix ? right.slice(-suffix).join('') : '', changed: false },
  ].filter(part => part.text)
}
