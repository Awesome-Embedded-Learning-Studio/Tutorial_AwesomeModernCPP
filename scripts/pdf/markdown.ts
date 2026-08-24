import { createMarkdownRenderer, disposeMdItInstance } from 'vitepress'
import type MarkdownIt from 'markdown-it'
import { cppTemplateEscapePlugin } from '../../site/.vitepress/plugins/escape-cpp-templates'
import { kbdPlugin } from '../../site/.vitepress/plugins/kbd-plugin'
import { mermaidPlugin } from '../../site/.vitepress/plugins/mermaid-plugin'

/**
 * The book renderer intentionally omits the website's code-fold plugin. A book
 * must contain every code line in the normal flow so Paged.js can fragment it.
 */
export async function createBookMarkdownRenderer(repositoryRoot: string): Promise<MarkdownIt> {
  disposeMdItInstance()
  return createMarkdownRenderer(repositoryRoot, {
    lineNumbers: false,
    math: true,
    languageAlias: {
      ld: 'c',
      nasm: 'asm',
    },
    theme: 'github-light',
    config(md) {
      cppTemplateEscapePlugin(md)
      md.use(kbdPlugin)
      md.use(mermaidPlugin)
    },
  }, '/')
}

export function renderCodeFence(md: MarkdownIt, code: string, language: string, relativePath: string): string {
  const longestRun = Math.max(0, ...Array.from(code.matchAll(/`+/g), (match) => match[0].length))
  const fence = '`'.repeat(Math.max(3, longestRun + 1))
  return md.render(`${fence}${language}\n${code.replace(/\n?$/, '\n')}${fence}\n`, { relativePath })
}
