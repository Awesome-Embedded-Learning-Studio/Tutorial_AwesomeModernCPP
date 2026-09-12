import { ref, watch, type Ref } from 'vue'

import { highlightCpp } from '../shiki'

// ── shiki 高亮(响应式)──
// 源码 ref 一变(懒加载完成 / 切 ARM tab / 编辑中),重新产出带双主题 CSS 变量的 HTML。
// 高亮是异步的——未就绪时调用方先用纯文本 fallback,就绪后替换为着色 HTML。
// SSR 不触发(watch 非 immediate,源码只在客户端 onMounted 后才填充)。
export function useShikiHighlight(source: Ref<string>): Ref<string> {
  const html = ref('')
  watch(source, async (code) => {
    html.value = ''
    if (!code) return
    try {
      html.value = await highlightCpp(code)
    } catch {
      html.value = ''
    }
  })
  return html
}

// ── 编辑器 overlay 技巧 ──
// textarea 与高亮 backdrop 两层重叠:编辑框文字透明、背后垫 shiki 高亮,
// 光标/选区由 textarea 接管;两层滚动必须同步,否则错位。
export function useEditorOverlay() {
  const backdropRef = ref<HTMLElement | null>(null)
  const textareaRef = ref<HTMLTextAreaElement | null>(null)

  function syncScroll(): void {
    const ta = textareaRef.value
    const bd = backdropRef.value
    if (!ta || !bd) return
    bd.scrollTop = ta.scrollTop
    bd.scrollLeft = ta.scrollLeft
  }

  return { backdropRef, textareaRef, syncScroll }
}
