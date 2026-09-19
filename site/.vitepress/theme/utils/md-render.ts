// 客户端 markdown 渲染:题目目录里的 problem.md / answer.md / 提示都在浏览器端渲染
// (它们是运行时 fetch 的文件,不走 VitePress 构建管线)。
// 代码块在渲染后用站点 shiki 单例补高亮(cpp/c/asm),与全站观感一致。

import MarkdownIt from 'markdown-it'
import { highlightCode, type ShikiLang } from '../shiki'

const md = new MarkdownIt({ html: true })

// fenced 语言标记 → shiki 语法:c/cpp 一族共用 cpp 语法,asm(GCC 汇编输出)单独注册;
// 其余语言保持纯文本,别把零星语言都塞进单例
const SHIKI_LANGS: Record<string, ShikiLang> = { cpp: 'cpp', c: 'cpp', asm: 'asm' }

export function renderMarkdown(src: string): string {
  return md.render(src)
}

/** 给容器里已识别语言的代码块补 shiki 高亮(v-html 渲染后调用) */
export async function highlightCodeBlocks(container: HTMLElement): Promise<void> {
  const blocks = container.querySelectorAll<HTMLElement>('pre code[class*="language-"]')
  for (const block of blocks) {
    const cls = [...block.classList].find(c => c.startsWith('language-'))
    const target = cls ? SHIKI_LANGS[cls.slice('language-'.length)] : undefined
    if (!target) continue
    const pre = block.parentElement
    if (!pre) continue
    try {
      const html = await highlightCode(block.textContent ?? '', target)
      const wrapper = document.createElement('div')
      wrapper.innerHTML = html
      const shikiPre = wrapper.firstElementChild
      if (shikiPre) pre.replaceWith(shikiPre)
    } catch {
      // 高亮失败就保留纯文本块,不影响内容
    }
  }
}
