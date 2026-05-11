import type { PluginSimple } from 'markdown-it'
import type MarkdownIt from 'markdown-it'

export const kbdPlugin: PluginSimple = (md: MarkdownIt) => {
  md.inline.ruler.after('emphasis', 'kbd', (state, silent) => {
    const start = state.pos
    const max = state.posMax

    if (state.src.charCodeAt(start) !== 0x2B /* + */) return false
    if (state.src.charCodeAt(start + 1) !== 0x2B /* + */) return false

    let pos = start + 2
    while (pos < max) {
      if (state.src.charCodeAt(pos) === 0x2B /* + */ && state.src.charCodeAt(pos + 1) === 0x2B /* + */) {
        const content = state.src.slice(start + 2, pos)
        if (content.length === 0) return false
        if (!silent) {
          const parts = content.split('+')
          const token = state.push('html_inline', '', 0)
          token.content = parts.map(p => `<kbd>${p.trim()}</kbd>`).join('+')
        }
        state.pos = pos + 2
        return true
      }
      pos++
    }
    return false
  })
}
