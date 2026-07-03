<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { useData } from 'vitepress'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))

// 一段"剧本":逐字打出 写代码 → 保存 → 编译 → 运行 的完整流程
type LineType = 'code' | 'cmd' | 'ok' | 'save' | 'out'
interface Line {
  text: string
  type: LineType
}

const script = computed<Line[]>(() =>
  isEn.value
    ? [
        { text: '#include <print>', type: 'code' },
        { text: 'constexpr int fib(int n) {', type: 'code' },
        { text: '  return n < 2 ? n : fib(n - 1) + fib(n - 2);', type: 'code' },
        { text: '}', type: 'code' },
        { text: 'int main() { std::print("{}", fib(10)); }', type: 'code' },
        { text: '', type: 'code' },
        { text: '\u{1F4BE}  saved main.cpp', type: 'save' },
        { text: '$ g++ -std=c++23 -O2 main.cpp', type: 'cmd' },
        { text: '✓  compiled in 0.3s', type: 'ok' },
        { text: '$ ./a.out', type: 'cmd' },
        { text: '55', type: 'out' },
      ]
    : [
        { text: '#include <print>', type: 'code' },
        { text: 'constexpr int fib(int n) {', type: 'code' },
        { text: '  return n < 2 ? n : fib(n - 1) + fib(n - 2);', type: 'code' },
        { text: '}', type: 'code' },
        { text: 'int main() { std::print("{}", fib(10)); }', type: 'code' },
        { text: '', type: 'code' },
        { text: '\u{1F4BE}  已保存 main.cpp', type: 'save' },
        { text: '$ g++ -std=c++23 -O2 main.cpp', type: 'cmd' },
        { text: '✓  编译通过 (0.3s)', type: 'ok' },
        { text: '$ ./a.out', type: 'cmd' },
        { text: '55', type: 'out' },
      ],
)

const CHAR_MS = 28        // 每字毫秒
const LINE_MS = 80        // 行间停顿
const LOOP_MS = 2200      // 跑完后驻留再循环

const done = ref<Line[]>([])          // 已完成行
const partial = ref('')               // 当前正在打的局部文本
const partialType = ref<LineType>('code')
const finished = ref(false)           // 全部打完,驻留中
const reduced = ref(false)

let lineIdx = 0
let charIdx = 0
let timer: ReturnType<typeof setTimeout> | null = null

function reset() {
  done.value = []
  partial.value = ''
  partialType.value = 'code'
  finished.value = false
  lineIdx = 0
  charIdx = 0
  tick()
}

function tick() {
  const lines = script.value
  if (lineIdx >= lines.length) {
    finished.value = true
    timer = setTimeout(reset, LOOP_MS)
    return
  }
  const line = lines[lineIdx]
  const chars = Array.from(line.text)   // 按码点切,emoji 不会断
  if (charIdx <= chars.length) {
    partial.value = chars.slice(0, charIdx).join('')
    partialType.value = line.type
    charIdx++
    timer = setTimeout(tick, CHAR_MS)
  } else {
    done.value.push({ text: line.text, type: line.type })
    partial.value = ''
    lineIdx++
    charIdx = 0
    timer = setTimeout(tick, LINE_MS)
  }
}

onMounted(() => {
  reduced.value = !!window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
  if (reduced.value) {
    // 减弱动效:一次性显示全部内容
    done.value = script.value.slice()
    finished.value = true
  } else {
    tick()
  }
})
onBeforeUnmount(() => {
  if (timer) clearTimeout(timer)
})
</script>

<template>
  <div class="hero-visual">
    <div class="terminal">
      <div class="terminal__bar">
        <span class="dot dot--red" />
        <span class="dot dot--yellow" />
        <span class="dot dot--green" />
        <span class="terminal__title">main.cpp — zsh</span>
      </div>

      <div class="terminal__body">
        <div
          v-for="(ln, i) in done"
          :key="'d' + i"
          class="ln"
          :class="'ln--' + ln.type"
        >{{ ln.text }}</div>

        <div
          v-if="!finished"
          class="ln"
          :class="'ln--' + partialType"
        >{{ partial }}<span class="cursor">▋</span></div>

        <div v-else class="ln ln--prompt"><span class="cursor">▋</span></div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.hero-visual {
  /* 固定宽度:容下最长代码行(≈44 字符 × 9.3px ≈ 410px + 44px 内边距 + 2px 边框 ≈ 456px),
     取 540px 留余量;max-width:100% 保证窄屏/窄图列不被遮挡。 */
  width: 540px;
  max-width: 100%;
  margin: 0 auto;
  animation: hero-fade-up 0.7s cubic-bezier(0.25, 0.46, 0.45, 0.94) both;
}

/* ── Terminal frame ────────────────────────────────────────── */
.terminal {
  position: relative;
  box-sizing: border-box;
  border: 1px solid var(--term-border);
  border-radius: 14px;
  overflow: hidden;
  background: linear-gradient(135deg, var(--term-bg-from) 0%, var(--term-bg-to) 100%);
  box-shadow:
    0 22px 56px var(--term-shadow),
    0 6px 14px rgba(0, 0, 0, 0.22);
  animation: terminal-glow 4s ease-in-out infinite;
}

.terminal__bar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 13px 16px;
  background: rgba(0, 0, 0, 0.22);
  border-bottom: 1px solid rgba(255, 255, 255, 0.06);
}

.dot {
  width: 14px;
  height: 14px;
  border-radius: 50%;
  flex-shrink: 0;
}
.dot--red { background: #ff5f56; }
.dot--yellow { background: #ffbd2e; }
.dot--green { background: #27c93f; }

.terminal__title {
  margin-left: 10px;
  color: var(--term-title);
  font-family: var(--vp-font-family-mono);
  font-size: 13px;
  letter-spacing: 0.3px;
}

/* ── Body (typed lines) ────────────────────────────────────── */
.terminal__body {
  padding: 22px 22px 20px;
  font-family: var(--vp-font-family-mono);
  font-size: 15.5px;
  line-height: 1.75;
  /* 预留峰值高度 = 11 条脚本 + 1 收尾 prompt = 12 行,从开打就占满,避免边打字边撑高抖一下 */
  min-height: calc(21em + 42px);
}

.ln {
  white-space: pre;        /* 保留代码缩进 */
  min-height: 1.75em;
}

.ln--code { color: var(--term-code); }
.ln--cmd { color: var(--term-cmd); }     /* shell prompt */
.ln--ok { color: var(--term-ok); }      /* 编译成功(语义绿) */
.ln--save { color: var(--term-save); }    /* 保存提示 */
.ln--out { color: var(--term-out); }     /* 程序输出 */
.ln--prompt { color: var(--term-ok); }

.cursor {
  display: inline-block;
  margin-left: 2px;
  color: var(--term-ok);
  animation: blink 1.05s step-end infinite;
}

/* ── Animations ────────────────────────────────────────────── */
@keyframes blink {
  0%, 50% { opacity: 1; }
  50.01%, 100% { opacity: 0; }
}

@keyframes terminal-glow {
  0%, 100% {
    box-shadow:
      0 22px 56px var(--term-shadow),
      0 6px 14px rgba(0, 0, 0, 0.22),
      0 0 0 0 rgba(96, 165, 250, 0);
  }
  50% {
    box-shadow:
      0 22px 56px var(--term-shadow),
      0 6px 14px rgba(0, 0, 0, 0.25),
      0 0 26px 3px var(--term-glow);
  }
}

@keyframes hero-fade-up {
  from { opacity: 0; transform: translateY(22px); }
  to   { opacity: 1; transform: translateY(0); }
}

@media (prefers-reduced-motion: reduce) {
  .hero-visual,
  .terminal,
  .cursor {
    animation: none !important;
  }
}

@media (max-width: 639px) {
  .hero-visual {
    /* 手机端父容器(.image-container)是 width:auto 按内容撑开,max-width:100% 会相对它而非视口,
       故直接按视口约束:100vw - 48px(= 手机端 hero 左右各 24px 内边距后的内容宽度)。 */
    max-width: calc(100vw - 48px);
  }
  .terminal__body {
    font-size: 13px;
    padding: 16px 16px 14px;
    min-height: calc(21em + 30px);
  }
}
</style>
