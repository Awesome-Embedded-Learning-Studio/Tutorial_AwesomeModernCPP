<script setup lang="ts">
import { computed, ref } from 'vue'

const props = withDefaults(defineProps<{
  id: number | string
  author?: string
  title: string
  publisher?: string
  year?: number | string
  chapter?: string
  url?: string
  quotes?: string
}>(), {
  author: '',
  publisher: '',
  year: '',
  chapter: '',
  url: '',
  quotes: '',
})

const expanded = ref(false)

const publicationInfo = computed(() => {
  const parts: string[] = []
  if (props.publisher) parts.push(props.publisher)
  if (props.year) parts.push(String(props.year))
  if (props.chapter) parts.push(props.chapter)
  return parts.join(', ')
})

const quoteList = computed(() => {
  if (!props.quotes) return []
  return props.quotes.split('||').map(q => q.trim()).filter(Boolean)
})
</script>

<template>
  <div :id="`ref-${id}`" class="ref-item">
    <div class="ref-item-header">
      <span class="ref-item-badge">{{ id }}</span>
      <div class="ref-item-meta">
        <span v-if="author" class="ref-item-author">{{ author }}</span>
        <span class="ref-item-title-wrapper">
          <a v-if="url" :href="url" target="_blank" rel="noopener noreferrer" class="ref-item-link">
            <em>{{ title }}</em>
          </a>
          <em v-else>{{ title }}</em>
        </span>
        <span v-if="publicationInfo" class="ref-item-pub">{{ publicationInfo }}</span>
      </div>
    </div>

    <div v-if="quoteList.length" class="ref-quotes">
      <button class="ref-quotes-toggle" @click="expanded = !expanded">
        <svg class="ref-quotes-arrow" :class="{ 'ref-quotes-arrow-open': expanded }" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <polyline points="6 9 12 15 18 9" />
        </svg>
        <span>{{ expanded ? '收起引文' : `展开引文 (${quoteList.length} 条)` }}</span>
      </button>
      <div v-if="expanded" class="ref-quotes-body">
        <p v-for="(q, i) in quoteList" :key="i" class="ref-quote-line">{{ q }}</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.ref-item {
  padding: 12px 14px;
  border-radius: 8px;
  transition: background-color 0.25s ease;
}

.ref-item:hover {
  background-color: var(--vp-c-bg);
}

.ref-item.ref-highlight {
  background-color: var(--vp-c-brand-soft);
  transition: background-color 0.3s ease;
}

.ref-item-header {
  display: flex;
  gap: 12px;
  align-items: flex-start;
}

/* ── Badge ────────────────────────────────── */

.ref-item-badge {
  flex-shrink: 0;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 26px;
  height: 26px;
  padding: 0 4px;
  border-radius: 6px;
  background: linear-gradient(135deg, var(--vp-c-brand-soft), var(--vp-c-indigo-soft));
  color: var(--vp-c-brand-1);
  font-size: 12px;
  font-weight: 700;
  font-variant-numeric: tabular-nums;
}

/* ── Meta ─────────────────────────────────── */

.ref-item-meta {
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}

.ref-item-author {
  font-size: 14px;
  font-weight: 600;
  color: var(--vp-c-text-1);
}

.ref-item-title-wrapper {
  font-size: 14px;
  line-height: 1.5;
}

.ref-item-link {
  color: var(--vp-c-brand-1);
  text-decoration: none !important;
  transition: color 0.25s ease;
}

.ref-item-link:hover {
  color: var(--vp-c-brand-2);
  text-decoration: underline !important;
}

.ref-item-pub {
  font-size: 12px;
  color: var(--vp-c-text-3);
}

/* ── Quotes (collapsible) ─────────────────── */

.ref-quotes {
  margin-top: 8px;
  margin-left: 38px;
}

.ref-quotes-toggle {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  cursor: pointer;
  font-size: 12px;
  color: var(--vp-c-text-3);
  font-weight: 500;
  user-select: none;
  background: none;
  border: none;
  padding: 2px 0;
  transition: color 0.25s ease;
}

.ref-quotes-toggle:hover {
  color: var(--vp-c-brand-1);
}

.ref-quotes-arrow {
  width: 14px;
  height: 14px;
  transition: transform 0.25s ease;
}

.ref-quotes-arrow-open {
  transform: rotate(180deg);
}

.ref-quotes-body {
  margin-top: 8px;
  padding: 10px 14px;
  border-left: 2px solid var(--vp-c-brand-soft);
  border-radius: 0 6px 6px 0;
  background-color: var(--vp-c-bg);
  font-size: 13px;
  line-height: 1.7;
  color: var(--vp-c-text-2);
}

.ref-quote-line {
  margin: 4px 0;
}

/* ── Dark Mode ───────────────────────────── */

.dark .ref-item:hover {
  background-color: var(--vp-c-bg-elv);
}

.dark .ref-quotes-body {
  background-color: var(--vp-c-bg-elv);
}

/* ── Responsive ──────────────────────────── */

@media (max-width: 639px) {
  .ref-item {
    padding: 10px 12px;
  }

  .ref-quotes {
    margin-left: 0;
    margin-top: 8px;
  }

  .ref-item-header {
    gap: 8px;
  }
}
</style>
