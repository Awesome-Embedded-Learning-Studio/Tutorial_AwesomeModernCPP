import type { PluginSimple } from 'markdown-it'
import type MarkdownIt from 'markdown-it'

/**
 * 长代码折叠:把超过阈值行数的代码块包成
 * <div class="vp-code-fold"><details><summary/></details><代码块/></div>
 * (代码在 <details> 外,见下方说明),用原生 <details> 做开关、纯 CSS :has() 控展开。
 *
 * 为什么是构建期 markdown-it 插件、而不是客户端 JS 增强?
 *   - FOUC:站点是 SSG,首屏静态 HTML 里代码块就是全展开的;客户端 bundle 异步加载,
 *     冷缓存首访「先展开再闪收」必然。构建期产出即折叠态,零闪烁。
 *   - 无障碍 / 无 JS:<details> 原生 disclosure,无 JS 也能展开,键盘/AT 语义白送。
 *
 * 实现要点(已核实 VitePress 1.6.4 编译产物 node/chunk-D3CUZ4fa.js):
 *   - sharedMarkdown.config(md) 在 VitePress 把 Shiki/复制按钮/行号/preWrapper 全部接好
 *     【之后】才执行(options.config(md) 在所有 md.use() 链之后),故此处捕获的
 *     md.renderer.rules.fence 已是完整链 —— 调用它即得含 .copy/.lang/pre.shiki/
 *     .line-numbers-wrapper 的整段 HTML。直接整段包 <details>,内部兄弟链不动。
 *   - 必须整段包(而非只包 <pre>):VitePress 复制按钮靠 button.copy.nextElementSibling
 *     定位 <pre>,兄弟链一断复制即失效。
 *   - 代码放在 <details>【外面】(.vp-code-fold 里与 <details> 同级),而非放进 <details> 内:
 *     原生 <details> 关闭时不渲染非 summary 内容;放外面则代码始终在 DOM 中,纯 CSS
 *     :has(details[open]) 控制 display(收起完全隐藏、不做预览;展开全显示) —— 零 JS、
 *     无 JS 也能点 summary 展开、@media print 强制显示即完整打印。
 *   - code-group 内的 fence 不折叠(tab 本身已是折叠语义):core ruler 用独立 depth 计数
 *     打 token.meta.inCodeGroup 标记 —— 不依赖 render 期才追加的 " active" info。
 *
 * 阈值 30 依据全站实测(7732 个代码块):>30 行占 ~12%,即 Issue #71 所指「一大坨」;
 * 20-30 行可正常阅读的中等块不误伤。改这一处常量即可全局调档。
 */
const FOLD_THRESHOLD = 20

export const codeFoldPlugin: PluginSimple = (md: MarkdownIt) => {
  // ① core ruler:标记 code-group 内的 fence,折叠时跳过。
  //    容器 token 与子 fence 都在 state.tokens 平铺流里(core 阶段已全部就绪),
  //    用 depth 计数即可判定祖先,render 时仍读同一 token 对象的 meta。
  md.core.ruler.push('code_fold_mark_codegroup', (state) => {
    let depth = 0
    for (const token of state.tokens) {
      if (token.type === 'container_code-group_open') {
        depth++
      } else if (token.type === 'container_code-group_close') {
        if (depth > 0) depth--
      } else if (depth > 0 && token.type === 'fence') {
        if (!token.meta) token.meta = {}
        token.meta.inCodeGroup = true
      }
    }
    return true
  })

  // ② 覆写 fence:整段包 <details>。
  const originalFence = md.renderer.rules.fence
  if (!originalFence) return

  md.renderer.rules.fence = (tokens, idx, options, env, self) => {
    const html = originalFence(tokens, idx, options, env, self)
    const token = tokens[idx]

    // code-group 内不折;mermaid 已被 mermaidPlugin 改型为 mermaid_diagram,不进 fence rule。
    if (token.meta && token.meta.inCodeGroup) return html

    // 数行数:token.content 末尾通常带单个 \n,去掉再 split。
    const body = token.content.replace(/\n$/, '')
    const lineCount = body === '' ? 0 : body.split('\n').length
    if (lineCount <= FOLD_THRESHOLD) return html

    // 双语 summary:EN 站构建时源码拷到 <srcDir>/en/<vol>/,env.relativePath 以 "en/" 开头
    // (见 scripts/build.ts 的 EN 分支)。CN 卷不带该前缀。
    const relativePath = env?.relativePath
    const isEn = typeof relativePath === 'string' && relativePath.startsWith('en/')
    const closedLabel = isEn
      ? `Expand <em>(${lineCount} lines)</em>`
      : `展开代码 <em>(共 ${lineCount} 行)</em>`
    const openLabel = isEn ? 'Collapse' : '收起代码'

    return (
      `<div class="vp-code-fold" data-lines="${lineCount}">` +
      `<details><summary><span class="vp-cf-closed">${closedLabel}</span>` +
      `<span class="vp-cf-open">${openLabel}</span></summary></details>` +
      html +
      `</div>`
    )
  }
}
