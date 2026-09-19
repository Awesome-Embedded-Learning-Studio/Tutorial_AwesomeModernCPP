import type { PluginSimple } from 'markdown-it'

/** Keep VitePress's copy/lang/pre siblings intact; only change the visible label. */
export const codeLabelPlugin: PluginSimple = (md) => {
  const fence = md.renderer.rules.fence
  if (!fence) return
  md.renderer.rules.fence = (...args) => {
    const html = fence(...args)
    const relativePath = args[3]?.relativePath
    const isEn = typeof relativePath === 'string' && relativePath.startsWith('en/')
    return html.replace(/(<span class="lang">)(cpp|output)(<\/span>)/g, (_, open, lang, close) => (
      open + (lang === 'cpp' ? 'C++' : isEn ? 'Output' : '运行结果') + close
    ))
  }
}
