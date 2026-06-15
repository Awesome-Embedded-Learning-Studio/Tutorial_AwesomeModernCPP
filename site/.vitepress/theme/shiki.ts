import type { Highlighter } from 'shiki'

// shiki 单例：只在客户端、且首次有源码需要高亮时才动态加载 shiki/bundle/web
// （JS regex 引擎，无需 WASM）。一页多个 OnlineCompilerDemo 共享同一个 highlighter，
// 只注册 cpp 语言 + github-light/dark 两个主题，体积最小。
let highlighterPromise: Promise<Highlighter> | null = null

async function getHighlighter(): Promise<Highlighter> {
  if (!highlighterPromise) {
    highlighterPromise = (async () => {
      const { createHighlighter } = await import('shiki/bundle/web')
      return createHighlighter({
        langs: ['cpp'],
        themes: ['github-light', 'github-dark'],
      })
    })()
  }
  return highlighterPromise
}

// 返回带 --shiki-light / --shiki-dark 双主题 CSS 变量的 HTML（defaultColor:false，
// 颜色交给 custom.css 里的 html.dark 选择器切换）。SSR 不执行（仅组件 onMounted 后调用）。
export async function highlightCpp(code: string): Promise<string> {
  const highlighter = await getHighlighter()
  return highlighter.codeToHtml(code, {
    lang: 'cpp',
    themes: { light: 'github-light', dark: 'github-dark' },
    defaultColor: false,
  })
}
