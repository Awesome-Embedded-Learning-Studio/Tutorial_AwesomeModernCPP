// 客户端 markdown 渲染:题目目录里的 problem.md / answer.md / 提示都在浏览器端渲染
// (它们是运行时 fetch 的文件,不走 VitePress 构建管线)。
// 代码块在渲染后用站点 shiki 单例补高亮(cpp/c),与全站观感一致。

import MarkdownIt from 'markdown-it'
import { highlightCpp } from '../shiki'

const md = new MarkdownIt({ html: true })

export function renderMarkdown(src: string): string {
  return md.render(src)
}

/** 给容器里 language-cpp / language-c 的代码块补 shiki 高亮(v-html 渲染后调用) */
export async function highlightCodeBlocks(container: HTMLElement): Promise<void> {
  const blocks = container.querySelectorAll<HTMLElement>('pre code.language-cpp, pre code.language-c')
  for (const block of blocks) {
    const pre = block.parentElement
    if (!pre) continue
    try {
      const html = await highlightCpp(block.textContent ?? '')
      const wrapper = document.createElement('div')
      wrapper.innerHTML = html
      const shikiPre = wrapper.firstElementChild
      if (shikiPre) pre.replaceWith(shikiPre)
    } catch {
      // 高亮失败就保留纯文本块,不影响内容
    }
  }
}
