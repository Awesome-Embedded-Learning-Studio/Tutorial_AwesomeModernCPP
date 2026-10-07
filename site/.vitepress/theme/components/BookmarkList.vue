<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { withBase } from 'vitepress'
import {
  BOOKMARKS_EVENT, loadBookmarks, removeBookmark,
  exportBookmarks, importBookmarks, type Bookmark,
} from '../composables/useBookmarks'
import { markRestore } from '../composables/readingKeys'
import { leadOf } from '../composables/noteDom'
import { NOTES_EVENT, loadNotes, removeNote, type Note } from '../composables/useNotes'

// 书签页主体:按文章分组展示「收藏的文章 + 每篇的便签」。
// 组头跳转续读(百分比恢复),便签点击跳回原文原处(文本指纹定位+高亮,失败退百分比)。
// 书签与便签只存在当前浏览器,导出 JSON 是唯一的搬运手段。

interface Group {
  path: string
  title: string
  bookmark: Bookmark | null
  notes: Note[]
  sortAt: number
}

const bookmarks = ref<Bookmark[]>([])
const notes = ref<Note[]>([])
const fileInput = ref<HTMLInputElement | null>(null)
const note = ref('')
let noteTimer: ReturnType<typeof setTimeout> | null = null

function sync(): void {
  bookmarks.value = loadBookmarks()
  notes.value = loadNotes()
}

/** 文章分组:有书签或有便签的文章各一组,组间按最近活动时间降序 */
const groups = computed<Group[]>(() => {
  const byPath = new Map<string, Group>()
  for (const bm of bookmarks.value) {
    byPath.set(bm.path, { path: bm.path, title: bm.title, bookmark: bm, notes: [], sortAt: bm.updatedAt })
  }
  for (const n of notes.value) {
    const g = byPath.get(n.path) ?? { path: n.path, title: n.title || n.path, bookmark: null, notes: [], sortAt: 0 }
    g.notes.push(n)
    if (n.createdAt > g.sortAt) g.sortAt = n.createdAt
    if (!g.bookmark) g.title = n.title || g.title
    byPath.set(n.path, g)
  }
  return [...byPath.values()].sort((a, b) => b.sortAt - a.sortAt)
})

const totalCount = computed(() => groups.value.length)
const noteCount = computed(() => notes.value.length)

function flash(message: string): void {
  note.value = message
  if (noteTimer) clearTimeout(noteTimer)
  noteTimer = setTimeout(() => (note.value = ''), 8000)
}

function formatTime(ts: number): string {
  if (!ts) return ''
  const d = new Date(ts)
  const pad = (n: number) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`
}

/** 组头跳转:续读恢复(百分比) */
function onJumpGroup(g: Group): void {
  if (g.bookmark) markRestore(g.path, g.bookmark.scrollPercent)
}

/** 便签跳转:优先文本指纹定位,失败退便签存的百分比 */
function onJumpNote(n: Note): void {
  markRestore(n.path, n.scrollPercent, leadOf(n.quote))
}

function onRemoveBookmark(g: Group): void {
  if (!g.bookmark) return
  removeBookmark(g.path)
  sync()
  flash(`已取消「${g.title}」的收藏。`)
}

function onRemoveNote(n: Note): void {
  removeNote(n.id)
  sync()
}

function exportBackup(): void {
  try {
    const blob = new Blob([exportBookmarks()], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `bookmarks-backup-${new Date().toISOString().slice(0, 10)}.json`
    a.click()
    URL.revokeObjectURL(url)
    flash('备份文件已开始下载,收好它;到新设备回到这里导入就能接上。')
  } catch {
    flash('导出失败:当前浏览器不允许下载文件。QAQ')
  }
}

async function onImportFile(event: Event): Promise<void> {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  input.value = ''
  if (!file) return
  try {
    const result = importBookmarks(await file.text())
    if (!result) {
      flash('导入失败:这不是书签备份文件。')
      return
    }
    sync()
    flash(`已导入:合并 ${result.bookmarks} 条书签、${result.notes} 条便签(同一条两边都动过时,保留更新时间晚的一方)。`)
  } catch {
    flash('导入失败:文件读不出来。')
  }
}

onMounted(() => {
  sync()
  window.addEventListener(BOOKMARKS_EVENT, sync)
  window.addEventListener(NOTES_EVENT, sync)
})

onBeforeUnmount(() => {
  window.removeEventListener(BOOKMARKS_EVENT, sync)
  window.removeEventListener(NOTES_EVENT, sync)
  if (noteTimer) clearTimeout(noteTimer)
})
</script>

<template>
  <div class="bm-list">
    <p v-if="!totalCount" class="bm-empty">
      还没有书签,也没有便签。读文章时点顶部导航栏的「☆ 收藏」收下整篇;在正文里划选一段文字,点弹出的「存为便签」,回头从这里点它就能跳回原处。换设备前导出一份备份。
    </p>
    <ul v-else class="bm-items">
      <li v-for="g in groups" :key="g.path" class="bm-group">
        <div class="bm-group__head">
          <a :href="withBase(g.path)" class="bm-group__title" @click="onJumpGroup(g)">
            <span class="bm-group__star" aria-hidden="true">{{ g.bookmark ? '★' : '✎' }}</span>
            <span class="bm-group__name">{{ g.title }}</span>
            <span v-if="g.bookmark && formatTime(g.bookmark.updatedAt)" class="bm-group__time">读到 {{ formatTime(g.bookmark.updatedAt) }}</span>
          </a>
          <button
            v-if="g.bookmark"
            type="button"
            class="bm-group__unstar"
            :aria-label="`取消收藏「${g.title}」`"
            title="取消收藏(这篇的便签会保留)"
            @click="onRemoveBookmark(g)"
          >取消收藏</button>
        </div>
        <ul v-if="g.notes.length" class="bm-notes">
          <li v-for="n in g.notes" :key="n.id" class="bm-note">
            <a :href="withBase(g.path)" class="bm-note__quote" :title="'点击跳回原文原处'" @click="onJumpNote(n)">
              「{{ n.quote.length > 90 ? n.quote.slice(0, 90) + '…' : n.quote }}」
            </a>
            <button type="button" class="bm-note__remove" :aria-label="`删除这条便签`" @click="onRemoveNote(n)">×</button>
          </li>
        </ul>
      </li>
    </ul>
    <div class="bm-backup">
      <span class="bm-backup__hint">书签与便签({{ bookmarks.length }} + {{ noteCount }})只存在当前浏览器,换设备或清缓存会丢,走之前导出一份,到了新地方再导入接上。</span>
      <span class="bm-backup__actions">
        <button type="button" class="bm-text-link" @click="exportBackup">导出备份 ↓</button>
        <button type="button" class="bm-text-link" @click="fileInput?.click()">导入备份 ↑</button>
        <input ref="fileInput" type="file" accept="application/json,.json" class="bm-backup__file" @change="onImportFile" />
      </span>
      <span v-if="note" class="bm-backup__note" role="status">{{ note }}</span>
    </div>
  </div>
</template>

<style scoped>
.bm-empty {
  color: var(--vp-c-text-2);
  border: 1px dashed var(--vp-c-border);
  border-radius: 8px;
  padding: 18px 20px;
  line-height: 1.9;
}

.bm-items {
  list-style: none;
  padding: 0;
  margin: 0 0 20px;
  display: flex;
  flex-direction: column;
  gap: 18px;
}

.bm-group {
  border: 1px solid var(--vp-c-border);
  border-radius: 10px;
  background: var(--vp-c-bg-soft);
  overflow: hidden;
}

.bm-group__head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 0 0 0 4px;
}

.bm-group__title {
  flex: 1;
  min-width: 0;
  display: inline-flex;
  align-items: baseline;
  gap: 8px;
  padding: 10px 12px;
  text-decoration: none;
}

.bm-group__star {
  color: var(--vp-c-brand-1);
  font-size: 14px;
}

.bm-group__name {
  font-weight: 600;
  color: var(--vp-c-text-1);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.bm-group:hover .bm-group__name {
  color: var(--vp-c-brand-1);
}

.bm-group__time {
  flex-shrink: 0;
  font-size: 12px;
  color: var(--vp-c-text-3);
  font-weight: 400;
}

.bm-group__unstar {
  flex-shrink: 0;
  margin-right: 10px;
  border: none;
  background: none;
  padding: 4px 6px;
  color: var(--vp-c-text-3);
  font-size: 12px;
  cursor: pointer;
  border-radius: 6px;
}

.bm-group__unstar:hover {
  color: var(--vp-c-red-1);
  background: var(--vp-c-red-soft);
}

.bm-notes {
  list-style: none;
  margin: 0;
  padding: 6px 12px 10px 30px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  border-top: 1px dashed var(--vp-c-border);
}

.bm-note {
  display: flex;
  align-items: flex-start;
  gap: 4px;
}

.bm-note__quote {
  flex: 1;
  min-width: 0;
  display: block;
  padding: 2px 8px;
  border-left: 3px solid var(--vp-c-brand-1);
  color: var(--vp-c-text-2);
  font-size: 13.5px;
  line-height: 1.8;
  text-decoration: none;
  transition: color 0.15s, border-color 0.15s;
}

.bm-note__quote:hover {
  color: var(--vp-c-brand-1);
  border-left-color: var(--vp-c-brand-2);
}

.bm-note__remove {
  flex-shrink: 0;
  border: none;
  background: none;
  color: var(--vp-c-text-3);
  font-size: 16px;
  line-height: 1.8;
  cursor: pointer;
  padding: 0 4px;
  border-radius: 4px;
}

.bm-note__remove:hover {
  color: var(--vp-c-red-1);
}

.bm-backup {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px 16px;
  border-top: 1px solid var(--vp-c-border);
  padding-top: 14px;
}

.bm-backup__hint {
  color: var(--vp-c-text-3);
  font-size: 13px;
}

.bm-backup__actions {
  display: inline-flex;
  gap: 14px;
}

.bm-text-link {
  border: none;
  background: none;
  padding: 0;
  color: var(--vp-c-brand-1);
  font-size: 13.5px;
  cursor: pointer;
}

.bm-text-link:hover {
  color: var(--vp-c-brand-2);
}

.bm-backup__file {
  display: none;
}

.bm-backup__note {
  flex-basis: 100%;
  color: var(--vp-c-text-2);
  font-size: 13px;
}
</style>
