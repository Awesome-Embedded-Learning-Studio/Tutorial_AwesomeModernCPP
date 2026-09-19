import type { Highlighter } from 'shiki'

// shiki 单例：只在客户端、且首次有源码需要高亮时才动态加载 shiki/bundle/web
// （JS regex 引擎，无需 WASM）。一页多个 OnlineCompilerDemo 共享同一个 highlighter，
// 初始只注册 cpp 语言 + github-light/dark 两个主题，体积最小;asm 按需后补(见下)。
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
// cpp/c 共用 cpp 语法;asm(题解里偶尔出现的 GCC 汇编块)初始不注册,
// 首次用到再从 shiki/langs/asm.mjs 动态加载,平时不增加首屏负担。
export type ShikiLang = 'cpp' | 'asm'

let asmLoaded = false

export async function highlightCode(code: string, lang: ShikiLang = 'cpp'): Promise<string> {
  const highlighter = await getHighlighter()
  if (lang === 'asm' && !asmLoaded) {
    await highlighter.loadLanguage((await import('shiki/langs/asm.mjs')).default)
    asmLoaded = true
  }
  return highlighter.codeToHtml(code, {
    lang,
    themes: { light: 'github-light', dark: 'github-dark' },
    defaultColor: false,
  })
}

export async function highlightCpp(code: string): Promise<string> {
  return highlightCode(code, 'cpp')
}
