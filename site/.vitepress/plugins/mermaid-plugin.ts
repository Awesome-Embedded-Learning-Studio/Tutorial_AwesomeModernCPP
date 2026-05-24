import type { PluginSimple } from 'markdown-it'
import type MarkdownIt from 'markdown-it'

export const mermaidPlugin: PluginSimple = (md: MarkdownIt) => {
  // Change mermaid fence tokens to a custom type so Shiki never sees them.
  // Core rules run after tokenization but before rendering, so this works
  // regardless of when VitePress applies Shiki's fence renderer override.
  md.core.ruler.push('mermaid_block', (state) => {
    for (let i = 0; i < state.tokens.length; i++) {
      const token = state.tokens[i]
      if (token.type === 'fence' && token.info.trim() === 'mermaid') {
        token.type = 'mermaid_diagram'
        token.tag = ''
        token.nesting = 0
      }
    }
    return true
  })

  md.renderer.rules.mermaid_diagram = (tokens, idx) => {
    const encoded = encodeURIComponent(tokens[idx].content.trim())
    return `<div class="mermaid-diagram" data-mermaid="${encoded}" data-rendered="false"></div>`
  }
}
