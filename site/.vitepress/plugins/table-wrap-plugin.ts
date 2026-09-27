import type { PluginSimple } from 'markdown-it'
import type MarkdownIt from 'markdown-it'

/**
 * 表格卡片 wrapper:把 markdown 渲染出的 <table> 整体包进
 * <div class="vp-table-wrap">…</div>,卡片视觉(边框/圆角/渐变/阴影/横向滚动)
 * 全部落在 wrapper 上,table 回到 display:table + width:100%。
 *
 * 为什么必须包一层(而不是纯 CSS):
 *   - VitePress 默认 .vp-doc table 是 display:block(为了 overflow-x 滚动),
 *     此时 width:100% 作用在块盒子上,内部匿名表格盒仍按内容收缩 —— 窄表的
 *     表头底色/斑马纹/悬停高亮会在最后一列右缘截断,卡片右侧露出底色(实测
 *     gap 124px)。wrapper 承担滚动后 table 用正常表格布局,天然满宽。
 *
 * 为什么是构建期 markdown-it 插件、而不是客户端 JS 增强:
 *   - 与 code-fold-plugin 同理:SSG 首屏静态 HTML 即含 wrapper,零 FOUC;
 *   - 精确命中「markdown 生成的表格」,不会误伤 Vue 组件模板里的 <table>
 *     (QuizProblem / OnlineCompilerDemo 等)。
 *
 * 时序:sharedMarkdown.config(md) 在 VitePress 全部 md.use() 之后执行,
 * 此处捕获的 rules.table_open/close 即最终链;为 undefined 时退回
 * renderToken 的默认输出(表格 token 不带属性,等价 '<table>'/'</table>')。
 */
export const tableWrapPlugin: PluginSimple = (md: MarkdownIt) => {
  const originalOpen = md.renderer.rules.table_open
  const originalClose = md.renderer.rules.table_close

  md.renderer.rules.table_open = (tokens, idx, options, env, self) => {
    const tag = originalOpen
      ? originalOpen(tokens, idx, options, env, self)
      : self.renderToken(tokens, idx, options)
    return `<div class="vp-table-wrap">${tag}`
  }

  md.renderer.rules.table_close = (tokens, idx, options, env, self) => {
    const tag = originalClose
      ? originalClose(tokens, idx, options, env, self)
      : self.renderToken(tokens, idx, options)
    return `${tag}</div>`
  }
}
