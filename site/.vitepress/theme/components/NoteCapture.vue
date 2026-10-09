<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useData } from 'vitepress'
import { captureNoteFromSelection } from '../composables/useNotes'

// 选中正文文字时,在选区上方浮出「存为便签」小按钮。
// 点击把选区文字(加定位指纹与百分比兜底)存进 localStorage,浮层变「已存 ✓」后收起。
// 只在文章页工作(frontmatter.chapter 判据,同 BookmarkButton)。

const { frontmatter, title } = useData()
const isDocPage = computed(() => typeof frontmatter.value.chapter === 'number')

const visible = ref(false)
const x = ref(0)
const y = ref(0)
const saved = ref(false)
const rejected = ref(false)
let hideTimer: ReturnType<typeof setTimeout> | null = null

function flashState(ok: boolean): void {
  saved.value = ok
  rejected.value = !ok
  if (hideTimer) clearTimeout(hideTimer)
  hideTimer = setTimeout(() => {
    visible.value = false
    saved.value = false
    rejected.value = false
  }, 900)
}

function onSelectionChange(): void {
  if (saved.value || rejected.value) return // 正在显示结果态,不打断
  const sel = window.getSelection()
  if (!sel || sel.isCollapsed || sel.rangeCount === 0) {
    visible.value = false
    return
  }
  // 选区必须在正文容器里(导航/侧栏里划选不算)
  const container = sel.getRangeAt(0).commonAncestorContainer
  const el = container.nodeType === Node.TEXT_NODE ? container.parentElement : (container as Element)
  if (!el?.closest?.('.vp-doc')) {
    visible.value = false
    return
  }
  const text = sel.toString().replace(/\s+/g, ' ').trim()
  if (text.length < 4) {
    visible.value = false
    return
  }
  const rect = sel.getRangeAt(0).getBoundingClientRect()
  x.value = rect.left + rect.width / 2
  y.value = rect.top
  visible.value = true
}

// 选区滚出视口后 rect 失真,滚动时先收起(松手再划选会再弹)
function onScroll(): void {
  if (!saved.value && !rejected.value) visible.value = false
}

// mousedown preventDefault 保住选区(按钮一按选区就没了,捕获就拿不到文字)
function onSave(event: MouseEvent): void {
  event.preventDefault()
  const pageTitle = typeof frontmatter.value.title === 'string' && frontmatter.value.title
    ? frontmatter.value.title
    : title.value
  const note = captureNoteFromSelection(pageTitle)
  flashState(note !== null)
}

onMounted(() => {
  document.addEventListener('selectionchange', onSelectionChange)
  window.addEventListener('scroll', onScroll, { passive: true })
})

onBeforeUnmount(() => {
  document.removeEventListener('selectionchange', onSelectionChange)
  window.removeEventListener('scroll', onScroll)
  if (hideTimer) clearTimeout(hideTimer)
})
</script>

<template>
  <div
    v-if="visible && isDocPage"
    class="note-capture"
    :style="{ left: `${x}px`, top: `${y}px` }"
    @mousedown.prevent
  >
    <button
      v-if="!saved && !rejected"
      type="button"
      class="note-capture__btn"
      title="把选中的文字存成便签,回头从书签页点它跳回原处"
      @mousedown.prevent
      @click="onSave"
    > ✎ 存为便签</button>
    <span v-else-if="saved" class="note-capture__done" role="status">✓ 已存,书签页见</span>
    <span v-else class="note-capture__done" role="status">这段存不进(太短啦)</span>
  </div>
</template>

<style scoped>
.note-capture {
  position: fixed;
  transform: translate(-50%, calc(-100% - 10px));
  z-index: 90;
  pointer-events: auto;
  filter: drop-shadow(0 4px 14px rgba(0, 0, 0, 0.18));
}

.note-capture__btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 5px 14px;
  border: 1px solid var(--vp-c-brand-1);
  border-radius: 999px;
  background: var(--vp-c-bg);
  color: var(--vp-c-brand-1);
  font-size: 13px;
  font-weight: 600;
  line-height: 1.4;
  cursor: pointer;
  user-select: none;
  transition: background 0.15s, color 0.15s;
}

.note-capture__btn:hover {
  background: var(--vp-c-brand-1);
  color: var(--vp-c-bg);
}

.note-capture__done {
  display: inline-flex;
  padding: 5px 14px;
  border-radius: 999px;
  background: var(--vp-c-bg);
  border: 1px solid var(--vp-c-border);
  color: var(--vp-c-text-2);
  font-size: 13px;
  user-select: none;
  white-space: nowrap;
}

@media print {
  .note-capture { display: none; }
}
</style>
