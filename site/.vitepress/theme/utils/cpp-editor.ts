import { Compartment, EditorState } from '@codemirror/state'
import { EditorView, keymap, lineNumbers, highlightActiveLine, drawSelection, tooltips } from '@codemirror/view'
import { defaultKeymap, history, historyKeymap, indentLess, indentMore } from '@codemirror/commands'
import { bracketMatching, HighlightStyle, indentUnit, syntaxHighlighting, syntaxTree } from '@codemirror/language'
import { tags } from '@lezer/highlight'
import { cpp } from '@codemirror/lang-cpp'
import { acceptCompletion, autocompletion, closeBrackets, closeBracketsKeymap, snippetCompletion, type CompletionContext } from '@codemirror/autocomplete'
import { setDiagnostics, linter, lintGutter, type Diagnostic } from '@codemirror/lint'
import type { CodeDiagnostic } from './cpp-check'

const keywords = 'alignas alignof auto bool break case catch char class co_await co_return co_yield concept const consteval constexpr constinit continue decltype default delete do double else enum explicit export extern false float for friend if inline int long mutable namespace new noexcept nullptr operator private protected public requires return short signed sizeof static static_assert struct switch template this thread_local throw true try typedef typename union unsigned using virtual void volatile while'.split(' ')
const snippets = [
  snippetCompletion('for (int ${i} = 0; ${i} < ${n}; ++${i}) {\n\t${}\n}', { label: 'for', detail: '循环', type: 'keyword' }),
  snippetCompletion('if (${condition}) {\n\t${}\n}', { label: 'if', detail: '条件分支', type: 'keyword' }),
  snippetCompletion('for (const auto& ${item} : ${items}) {\n\t${}\n}', { label: 'for-range', detail: '范围循环', type: 'keyword' }),
]

function completions(context: CompletionContext) {
  const node = syntaxTree(context.state).resolveInner(context.pos, -1)
  if (/Comment|String|CharLiteral/.test(node.name)) return null
  const word = context.matchBefore(/[\w:]+/)
  if (!word || (!context.explicit && word.text.length < 2)) return null
  const standard = ['std::vector', 'std::string', 'std::array', 'std::cout', 'std::cin', 'std::endl', 'std::move', 'std::size_t', 'std::optional', 'std::span', 'std::generator']
  // 文档标识符:本题代码里写过的函数名/变量名(从语法树收 Identifier,跳过
  // 注释/字符串;≥3 字符且含字母——挡掉 "0" 这类噪音;不与关键字重复)
  const docIdentifiers = new Set<string>()
  syntaxTree(context.state).iterate({
    enter(sub) {
      if (/Comment|String|CharLiteral|Preproc|Literal/.test(sub.name)) return false
      if (sub.name !== 'Identifier') return
      const text = context.state.sliceDoc(sub.from, sub.to)
      if (text.length >= 3 && /[A-Za-z_]/.test(text)) docIdentifiers.add(text)
    },
  })
  return { from: word.from, options: [
    ...snippets,
    ...keywords.filter(k => !['for', 'if'].includes(k)).map(label => ({ label, type: 'keyword' })),
    // 排序(boost):自己写的函数/变量最优先,标准库垫底;正打进来的半截词不当候选
    ...standard.map(label => ({ label, type: 'text', boost: -1 })),
    ...[...docIdentifiers]
      .filter(id => !keywords.includes(id) && id !== word.text)
      .map(label => ({ label, type: 'variable', boost: 1 })),
  ], validFor: /^[\w:]*$/ }
}

// 补全默认边打字边弹(activateOnTyping);编辑器栏的开关经 Compartment 热切换到
// 「安静模式」——打字不弹,但 Ctrl+空格 手动唤起始终可用,补全本身不下线
const completionCompartment = new Compartment()

function completionExtension(autoPopup: boolean) {
  return autocompletion({ override: [completions], activateOnTyping: autoPopup })
}

function rootZoom(): number {
  if (typeof document === 'undefined') return 1
  const zoom = parseFloat(getComputedStyle(document.documentElement).zoom)
  return Number.isFinite(zoom) && zoom > 0 ? zoom : 1
}

/** tooltip 定位的 zoom 校正:CodeMirror 写的 top/left 是视觉坐标,但 html 级
 *  CSS zoom(字号档)会把 fixed 定位再乘一次 zoom → 弹窗漂移;此处在其每次
 *  定位后除以根 zoom 补偿。data-zoom-fixed 记录自己写过的值,防校正回环。 */
function zoomSafeTooltips(container: HTMLElement): { destroy(): void } {
  const correct = (tooltip: HTMLElement, axis: 'top' | 'left') => {
    const raw = tooltip.style[axis]
    if (!raw || tooltip.dataset[`zoomFixed${axis}`] === raw) return
    const value = parseFloat(raw)
    if (Number.isFinite(value)) {
      tooltip.style[axis] = `${value / rootZoom()}px`
      tooltip.dataset[`zoomFixed${axis}`] = tooltip.style[axis]
    }
  }
  const observer = new MutationObserver(() => {
    if (rootZoom() === 1) return
    container.querySelectorAll<HTMLElement>('.cm-tooltip').forEach(tooltip => {
      if (tooltip.style.position !== 'fixed') return
      correct(tooltip, 'top')
      correct(tooltip, 'left')
    })
  })
  observer.observe(container, { subtree: true, attributeFilter: ['style'], childList: true })
  return { destroy() { observer.disconnect() } }
}

/** 在光标处插入 4 空格缩进单元;非空选区交给内置 indentMore(逐行加缩进、
 *  不删内容、多光标安全——"整段替换选区"的写法会把选中的代码删成 4 空格)。
 *  插入文本的正道是 view.dispatch(view.state.replaceSelection(text)):
 *  replaceSelection 在 EditorState 上、返回 TransactionSpec;EditorView
 *  上没有(前几版在此连炸过两次,勿再踩)。 */
function insertIndentUnit(view: EditorView): boolean {
  if (view.state.selection.ranges.some(range => !range.empty)) return indentMore(view)
  view.dispatch(view.state.replaceSelection('    '))
  return true
}

export function createCppEditor(parent: HTMLElement, source: string, label: string, onChange: (source: string) => void) {
  let compilerDiagnostics: CodeDiagnostic[] = []
  let replacing = false
  function collectDiagnostics(editor: EditorView): Diagnostic[] {
    const diagnostics: Diagnostic[] = []
    const positions = new Set<number>()
    // 解析器只提供语法线索,不将其当作 C++ 编译结论。
    syntaxTree(editor.state).iterate({ enter(node) {
      if (!node.type.isError || positions.has(node.from) || diagnostics.length >= 20) return
      positions.add(node.from)
      diagnostics.push({ from: node.from, to: Math.min(editor.state.doc.length, Math.max(node.to, node.from + 1)), severity: 'warning', source: '本地语法提示', message: '这里可能缺少符号或语法尚未写完整；宏和部分新语法请以编译检查为准。' })
    } })
    for (const item of compilerDiagnostics) {
      if (item.line < 1 || item.line > editor.state.doc.lines) continue
      const line = editor.state.doc.line(item.line)
      const from = Math.min(line.to, line.from + Math.max(0, item.column - 1))
      diagnostics.push({ from, to: Math.min(line.to, from + 1), severity: item.severity, message: item.message, source: '编译检查' })
    }
    return diagnostics
  }
  // tooltip 定位校正器(观察容器内 .cm-tooltip 的内联样式,除以根 zoom 补偿)
  const tooltipGuard = zoomSafeTooltips(parent)
  const view = new EditorView({
    parent,
    state: EditorState.create({
      doc: source.replace(/\r\n?/g, '\n'),
      extensions: [
        cpp(), lineNumbers(), highlightActiveLine(), drawSelection(), history(), bracketMatching(), closeBrackets(),
        indentUnit.of('    '), EditorState.tabSize.of(4),
        syntaxHighlighting(HighlightStyle.define([
          { tag: tags.keyword, color: '#ff9da9' },
          { tag: [tags.string, tags.character], color: '#b9daff' },
          { tag: [tags.number, tags.bool], color: '#c6b7ff' },
          { tag: tags.comment, color: '#899bb1' },
          { tag: [tags.typeName, tags.className], color: '#82d8ca' },
          { tag: tags.function(tags.variableName), color: '#e9c588' },
        ])),
        completionCompartment.of(completionExtension(true)),
        tooltips({ position: 'fixed' }),
        // 弹窗开着:Tab = 接受候选(与 Enter 同义);没弹窗:Tab = 插入缩进单元
        keymap.of([{ key: 'Tab', run: view => acceptCompletion(view) || insertIndentUnit(view), shift: indentLess }, ...closeBracketsKeymap, ...defaultKeymap, ...historyKeymap]),
        EditorView.contentAttributes.of({ 'aria-label': label, 'aria-description': '补全弹窗打开时，Tab 或 Enter 接受候选；否则 Tab 缩进，Shift+Tab 反缩进，Esc 后按 Tab 离开编辑器。Ctrl+空格打开补全。', spellcheck: 'false' }),
        EditorView.updateListener.of(update => {
          if (!update.docChanged) return
          compilerDiagnostics = []
          if (!replacing) onChange(update.state.doc.toString())
        }),
        lintGutter(),
        linter(collectDiagnostics, { delay: 600, needsRefresh: update => syntaxTree(update.startState) !== syntaxTree(update.state) }),
        EditorView.theme({
          '&': { height: '100%', color: '#d7e3f2', backgroundColor: '#101d30', fontSize: '14px' },
          '.cm-scroller': { overflow: 'auto', fontFamily: 'var(--vp-font-family-mono)', lineHeight: '23px' },
          '.cm-content': { padding: '16px 0', caretColor: '#d7e3f2' },
          '.cm-line': { padding: '0 14px' },
          '.cm-gutters': { backgroundColor: '#101d30', color: '#8296b0', borderRight: '1px solid #31415a' },
          '.cm-activeLine, .cm-activeLineGutter': { backgroundColor: '#ffffff08' },
          '.cm-cursor': { borderLeftColor: '#d7e3f2' },
          '&.cm-focused .cm-selectionBackground, .cm-selectionBackground, .cm-content ::selection': { backgroundColor: '#315781' },
          '.cm-tooltip': { backgroundColor: '#1c2e46', color: '#e1eaf5', border: '1px solid #47617f', fontFamily: 'var(--vp-font-family-mono)' },
          '.cm-tooltip-autocomplete ul': { fontFamily: 'inherit', maxWidth: 'min(400px, 80vw)' },
        }, { dark: true }),
      ],
    }),
  })
  return {
    setSource(value: string) {
      const normalized = value.replace(/\r\n?/g, '\n')
      if (view.state.doc.toString() === normalized) return
      replacing = true
      try { view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: normalized } }) }
      finally { replacing = false }
    },
    setDiagnostics(value: CodeDiagnostic[]) {
      compilerDiagnostics = value
      view.dispatch(setDiagnostics(view.state, collectDiagnostics(view)))
    },
    setCompletion(autoPopup: boolean) {
      view.dispatch({ effects: completionCompartment.reconfigure(completionExtension(autoPopup)) })
    },
    focus() { view.focus() },
    destroy() { tooltipGuard.destroy(); view.destroy() },
  }
}
