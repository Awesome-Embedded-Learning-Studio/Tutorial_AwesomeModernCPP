<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useData, withBase } from 'vitepress'
import { subscribeAfterRouteChange } from '../router-hooks'
import { pathnameToKey } from '../composables/readingKeys'
import { leadOf, locateLead, flashRange } from '../composables/noteDom'
import { NOTES_EVENT, loadNotes, removeNote, type Note } from '../composables/useNotes'

// 右侧边栏(aside)里当前文章的便签:挂 aside-outline-after,在大纲下方。
// 空态显示引导(划选正文存便签),标题行的「全部 ↗」跳书签页看全局书架。

const { frontmatter } = useData()
const notes = ref<Note[]>([])

const isDocPage = computed(() => typeof frontmatter.value.chapter === 'number')

function sync(): void {
  const key = pathnameToKey(location.pathname)
  notes.value = loadNotes().filter((n) => n.path === key)
}

/** aside 窄,时间用短格式:当天只报时分,今年报到日,跨年带年 */
function shortTime(ts: number): string {
  if (!ts) return ''
  const d = new Date(ts)
  const now = new Date()
  const pad = (n: number) => String(n).padStart(2, '0')
  const sameDay = d.toDateString() === now.toDateString()
  if (sameDay) return `${pad(d.getHours())}:${pad(d.getMinutes())}`
  if (d.getFullYear() === now.getFullYear()) return `${d.getMonth() + 1}月${d.getDate()}日`
  return `${String(d.getFullYear()).slice(2)}-${d.getMonth() + 1}-${d.getDate()}`
}

/** 本页内定位:文本指纹找到就滚过去高亮,找不到退百分比 */
function jump(n: Note): void {
  const root = document.querySelector('.vp-doc')
  if (root) {
    const range = locateLead(root, leadOf(n.quote))
    if (range && flashRange(range)) return
  }
  // 兜底:按百分比滚(三次校准节奏同书签恢复)
  const percent = n.scrollPercent
  const attempt = () => {
    const el = document.documentElement
    const max = el.scrollHeight - el.clientHeight
    if (max > 0) el.scrollTop = (max * percent) / 100
  }
  requestAnimationFrame(attempt)
  setTimeout(attempt, 300)
  setTimeout(attempt, 1200)
}

function onRemove(n: Note): void {
  removeNote(n.id)
  sync()
}

// aside 组件跨路由保留实例,切页后要按新路径重新过滤
subscribeAfterRouteChange(sync)

onMounted(() => {
  sync()
  window.addEventListener(NOTES_EVENT, sync)
})

onBeforeUnmount(() => {
  window.removeEventListener(NOTES_EVENT, sync)
})
</script>

<template>
  <div v-if="isDocPage" class="article-notes" :class="{ 'is-empty': !notes.length }">
    <p class="article-notes__heading">
      <span aria-hidden="true">✎</span> 本页便签<span v-if="notes.length" class="article-notes__count">{{ notes.length }}</span>
      <a :href="withBase('/bookmarks')" class="article-notes__all" title="去书签页看全部收藏与便签">全部 ↗</a>
    </p>

    <div v-if="!notes.length" class="article-notes__hint">
      <p>本页还没有便签</p>
      <p>划选正文里的一段文字，点弹出的「✎ 存为便签」，回头在这里一键跳回原处。</p>
    </div>

    <ul v-else class="article-notes__list">
      <li v-for="n in notes" :key="n.id" class="article-notes__item">
        <button type="button" class="article-notes__main" title="点击定位到原文原处" @click="jump(n)">
          <span class="article-notes__quote">{{ n.quote.length > 46 ? n.quote.slice(0, 46) + '…' : n.quote }}</span>
          <span v-if="shortTime(n.createdAt)" class="article-notes__time">{{ shortTime(n.createdAt) }}</span>
        </button>
        <button type="button" class="article-notes__remove" aria-label="删除这条便签" @click="onRemove(n)">×</button>
      </li>
    </ul>
  </div>
</template>

<style scoped>
.article-notes {
  margin-top: 20px;
  padding-top: 14px;
  border-top: 1px solid var(--vp-c-divider);
}

.article-notes__heading {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 0 0 10px;
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0.02em;
  color: var(--vp-c-text-2);
}

.article-notes__count {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 16px;
  height: 16px;
  padding: 0 5px;
  border-radius: 999px;
  background: var(--vp-c-brand-soft);
  color: var(--vp-c-brand-1);
  font-size: 11px;
  font-weight: 600;
}

.article-notes__heading {
  position: relative;
  padding-right: 46px;
}

.article-notes__all {
  position: absolute;
  right: 0;
  top: 50%;
  transform: translateY(-50%);
  color: var(--vp-c-text-3);
  font-size: 11.5px;
  font-weight: 500;
  text-decoration: none;
  transition: color 0.15s;
}

.article-notes__all:hover {
  color: var(--vp-c-brand-1);
}

/* 空态引导 */
.article-notes__hint {
  border: 1px dashed var(--vp-c-border);
  border-radius: 8px;
  padding: 10px 12px;
  background: var(--vp-c-bg-soft);
}

.article-notes__hint p {
  margin: 0;
  font-size: 12px;
  line-height: 1.8;
}

.article-notes__hint p:first-child {
  color: var(--vp-c-text-2);
  font-weight: 600;
}

.article-notes__hint p:last-child {
  margin-top: 2px;
  color: var(--vp-c-text-3);
}

/* 便签条目 */
.article-notes__list {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.article-notes__item {
  display: flex;
  align-items: stretch;
  gap: 0;
  border-radius: 7px;
  transition: background 0.15s;
}

.article-notes__item:hover {
  background: var(--vp-c-bg-soft);
}

.article-notes__main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 2px;
  border: none;
  background: none;
  padding: 6px 4px 6px 10px;
  border-left: 2px solid var(--vp-c-brand-1);
  border-radius: 0 7px 7px 0;
  text-align: left;
  cursor: pointer;
}

.article-notes__quote {
  color: var(--vp-c-text-2);
  font-size: 12.5px;
  line-height: 1.7;
  transition: color 0.15s;
  word-break: break-all;
}

.article-notes__main:hover .article-notes__quote {
  color: var(--vp-c-brand-1);
}

.article-notes__time {
  color: var(--vp-c-text-3);
  font-size: 11px;
  line-height: 1;
}

.article-notes__remove {
  flex-shrink: 0;
  border: none;
  background: none;
  padding: 0 6px;
  color: var(--vp-c-text-3);
  font-size: 14px;
  cursor: pointer;
  border-radius: 7px;
  opacity: 0;
  transition: opacity 0.15s, color 0.15s;
}

.article-notes__item:hover .article-notes__remove,
.article-notes__remove:focus-visible {
  opacity: 1;
}

.article-notes__remove:hover {
  color: var(--vp-c-red-1);
}
</style>
