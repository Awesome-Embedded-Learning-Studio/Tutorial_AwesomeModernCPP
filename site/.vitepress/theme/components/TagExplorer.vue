<script setup lang="ts">
// 标签索引页主组件:标签墙(按分类分组)+ 筛选结果列表。
// 数据由 transformPageData 注入(frontmatter.tagsIndex,见 config/tags-manifest.ts),
// SSR 即有完整内容;URL 查询参数(?tag=&d=&p=&q=)在客户端挂载后回填,可分享、可后退。
// 语义:同类标签取并集,跨类标签取交集;难度/平台是正交筛选。
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { useData, withBase } from 'vitepress'
import type { TagArticle, TagsIndex } from '../../config/tags-manifest'

const { frontmatter } = useData()
const index = computed<TagIndexBox>(() => frontmatter.value.tagsIndex as TagIndexBox)
type TagIndexBox = TagsIndex | undefined
const ready = computed(() => !!index.value)

const isEn = computed(() => index.value?.locale === 'en')
const t = computed(() => {
  const en = isEn.value
  return {
    eyebrow: en ? 'Tag Index' : '标签索引',
    mono: '/ TAG EXPLORER',
    h1: en ? 'Find articles by topic.' : '按主题找文章。',
    subIntro: en ? 'topic tags' : '个主题标签',
    subOver: en ? 'articles' : '篇',
    subRule: en
      ? 'Tags in the same group combine, groups intersect.'
      : '同类标签取并集,跨类取交集。',
    searchPh: en ? 'Search tags or article titles…' : '搜索标签或文章标题…',
    diffLabel: en ? 'Difficulty' : '难度',
    platLabel: en ? 'Platform' : '平台',
    all: en ? 'All' : '全部',
    selected: en ? 'Selected' : '已选',
    reset: en ? 'Reset' : '重置',
    results: en ? 'Results' : '筛选结果',
    resultsMono: '/ RESULTS',
    articles: en ? 'articles' : '篇',
    showAll: (n: number) => (en ? `Show all ${n} articles` : `显示全部 ${n} 篇`),
    hint: en
      ? 'Pick a tag from the wall above, or search — filters combine with your selection.'
      : '从上面挑一个标签,或直接搜索——筛选条件与已选标签叠加生效。',
    empty: en
      ? 'No article matches this combination. Loosen a filter or reset.'
      : '没有文章同时满足这些条件,放宽一两个筛选,或者直接重置。',
    clear: en ? 'Clear filters' : '清空筛选',
    noData: en ? 'Tag index data is missing.' : '标签索引数据未生成。',
    minutes: (m: number) => (en ? `${m} min` : `${m} 分钟`),
  }
})

const DIFFS = [
  { key: 'beginner', zh: '入门', en: 'Beginner' },
  { key: 'intermediate', zh: '进阶', en: 'Intermediate' },
  { key: 'advanced', zh: '高级', en: 'Advanced' },
] as const
const PLATS = [
  { key: 'host', zh: '通用', en: 'Any platform' },
  { key: 'stm32f1', zh: 'STM32F1', en: 'STM32F1' },
  { key: 'stm32f4', zh: 'STM32F4', en: 'STM32F4' },
] as const

// ── 状态 ────────────────────────────────────────────────────
const selected = ref<string[]>([])
const diff = ref('all')
const plat = ref('all')
const query = ref('')
const visible = ref(50)
let hydrated = false

const topicTags = computed(() => {
  const set = new Set<string>()
  for (const c of index.value?.categories ?? []) for (const tg of c.tags) set.add(tg.name)
  return set
})
const countOf = (tag: string) =>
  index.value?.categories.flatMap(c => c.tags).find(x => x.name === tag)?.count

const catOf = computed(() => {
  const m = new Map<string, string>()
  for (const c of index.value?.categories ?? []) for (const tg of c.tags) m.set(tg.name, c.key)
  return m
})

const hasFilter = computed(() => selected.value.length > 0 || query.value.trim() !== ''
  || diff.value !== 'all' || plat.value !== 'all')

function toggle(tag: string) {
  selected.value = selected.value.includes(tag)
    ? selected.value.filter(x => x !== tag)
    : [...selected.value, tag]
}

function resetAll() {
  selected.value = []
  diff.value = 'all'
  plat.value = 'all'
  query.value = ''
}

// 搜索词同时收窄标签墙(按标签名)与结果(标题/标签/卷名)
const wallCats = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!q) return index.value?.categories ?? []
  return (index.value?.categories ?? [])
    .map(c => ({ ...c, tags: c.tags.filter(tg => tg.name.toLowerCase().includes(q)) }))
    .filter(c => c.tags.length > 0)
})

const blobCache = new Map<string, string>()
function blob(a: TagArticle): string {
  let s = blobCache.get(a.h)
  if (s === undefined) {
    const vol = index.value?.volLabels[a.v] ?? a.v
    s = `${a.t} ${a.tg.join(' ')} ${vol}`.toLowerCase()
    blobCache.set(a.h, s)
  }
  return s
}

const results = computed<TagArticle[]>(() => {
  const data = index.value
  if (!data) return []
  const q = query.value.trim().toLowerCase()
  // 跨类交集:每个「有已选标签的类」都要求文章至少命中其中一个(类内并集)
  const byCat = new Map<string, string[]>()
  for (const tag of selected.value) {
    const cat = catOf.value.get(tag)
    if (!cat) continue
    byCat.set(cat, [...(byCat.get(cat) ?? []), tag])
  }
  return data.articles.filter((a) => {
    if (diff.value !== 'all' && a.d !== diff.value) return false
    if (plat.value !== 'all' && a.p !== plat.value) return false
    for (const tags of byCat.values()) {
      if (!tags.some(tag => a.tg.includes(tag))) return false
    }
    if (q && !blob(a).includes(q)) return false
    return true
  })
})

watch([selected, diff, plat, query], () => { visible.value = 50 })
watch([selected, diff, plat, query], syncUrl, { deep: true })

// ── URL 同步(挂载后启用,SSR 首屏保持默认态,无水合差异) ────
function readParams() {
  const sp = new URLSearchParams(window.location.search)
  // 去重:手工/分享 URL 可能带重复 tag 参数,不去重会渲染重复 chip 且 keyed patch 出错
  selected.value = [...new Set(sp.getAll('tag'))].filter(tag => topicTags.value.has(tag))
  const d = sp.get('d')
  diff.value = d && DIFFS.some(x => x.key === d) ? d : 'all'
  const p = sp.get('p')
  plat.value = p && PLATS.some(x => x.key === p) ? p : 'all'
  query.value = sp.get('q') ?? ''
}

function syncUrl() {
  if (!hydrated || typeof window === 'undefined') return
  const sp = new URLSearchParams()
  for (const tag of selected.value) sp.append('tag', tag)
  if (diff.value !== 'all') sp.set('d', diff.value)
  if (plat.value !== 'all') sp.set('p', plat.value)
  if (query.value.trim()) sp.set('q', query.value.trim())
  const s = sp.toString()
  const next = window.location.pathname + (s ? `?${s}` : '') + window.location.hash
  const current = window.location.pathname + window.location.search + window.location.hash
  if (next === current) return
  // 必须保留 history.state:VitePress 路由对 state===null 的 popstate 直接忽略,
  // 传 null 会让「后退离开再前进回来」时 URL 与页面脱节(weekly.ts 同款守卫)
  window.history.replaceState(window.history.state, '', next)
}

function onPop() { readParams() }
onMounted(() => {
  readParams()
  hydrated = true
  window.addEventListener('popstate', onPop)
})
onUnmounted(() => window.removeEventListener('popstate', onPop))

function volLabel(v: string) { return index.value?.volLabels[v] ?? v }
function diffLabel(key: string) {
  const d = DIFFS.find(x => x.key === key)
  return d ? (isEn.value ? d.en : d.zh) : key
}
</script>

<template>
  <div class="tag-explorer">
    <header class="te-head">
      <p class="te-eyebrow">{{ t.eyebrow }} <span class="te-mono">{{ t.mono }}</span></p>
      <h1 class="te-h1">{{ t.h1 }}</h1>
      <p class="te-sub">
        <span v-if="index">{{ index.tagCount }} {{ t.subIntro }} · {{ index.articleCount }} {{ t.subOver }}。</span>
        {{ t.subRule }}
      </p>
    </header>

    <div v-if="!ready" class="te-notice">{{ t.noData }}</div>

    <template v-else>
      <div class="te-toolbar">
        <input
          v-model="query" class="te-search" type="search"
          :placeholder="t.searchPh" :aria-label="t.searchPh"
        >
        <div class="te-facets">
          <div class="te-facet">
            <span class="te-facet-name">{{ t.diffLabel }}</span>
            <button class="te-facet-chip" :class="{ 'is-on': diff === 'all' }" @click="diff = 'all'">{{ t.all }}</button>
            <button
              v-for="d in DIFFS" :key="d.key" class="te-facet-chip"
              :class="{ 'is-on': diff === d.key }" @click="diff = diff === d.key ? 'all' : d.key"
            >{{ isEn ? d.en : d.zh }}</button>
          </div>
          <div class="te-facet">
            <span class="te-facet-name">{{ t.platLabel }}</span>
            <button class="te-facet-chip" :class="{ 'is-on': plat === 'all' }" @click="plat = 'all'">{{ t.all }}</button>
            <button
              v-for="p in PLATS" :key="p.key" class="te-facet-chip"
              :class="{ 'is-on': plat === p.key }" @click="plat = plat === p.key ? 'all' : p.key"
            >{{ isEn ? p.en : p.zh }}</button>
          </div>
        </div>
      </div>

      <div v-if="selected.length" class="te-selected">
        <span class="te-facet-name">{{ t.selected }}</span>
        <button
          v-for="tag in selected" :key="tag" class="te-chip is-on"
          :aria-pressed="true" @click="toggle(tag)"
        >{{ tag }} <span class="te-count">{{ countOf(tag) ?? '·' }}</span> <span class="te-x" aria-hidden="true">×</span></button>
        <button class="te-reset" @click="resetAll">{{ t.reset }}</button>
      </div>

      <section class="te-wall" aria-label="tags">
        <div v-for="cat in wallCats" :key="cat.key" class="te-cat">
          <div class="te-cat-head">
            {{ cat.label }} <span v-if="!isEn" class="te-mono">/ {{ cat.labelEn.toUpperCase() }}</span>
          </div>
          <div class="te-cat-tags">
            <button
              v-for="tg in cat.tags" :key="tg.name" class="te-chip"
              :class="{ 'is-on': selected.includes(tg.name) }"
              :aria-pressed="selected.includes(tg.name)" @click="toggle(tg.name)"
            >{{ tg.name }} <span class="te-count">{{ tg.count }}</span></button>
          </div>
        </div>
        <p v-if="!wallCats.length" class="te-wall-empty">—</p>
      </section>

      <section class="te-results">
        <div class="te-results-head">
          <span>{{ t.results }} <span class="te-mono">{{ t.resultsMono }}</span></span>
          <span class="te-results-count">
            {{ results.length }} {{ t.articles }}
            <button v-if="hasFilter" class="te-reset" @click="resetAll">{{ t.reset }}</button>
          </span>
        </div>

        <p v-if="!hasFilter" class="te-hint">{{ t.hint }}</p>
        <div v-else-if="!results.length" class="te-empty">
          <p>{{ t.empty }}</p>
          <button class="te-reset" @click="resetAll">{{ t.clear }}</button>
        </div>
        <template v-else>
          <ul class="te-list">
            <li v-for="a in results.slice(0, visible)" :key="a.h" class="te-card">
              <a class="te-card-title" :href="withBase(a.h)">{{ a.t }} <span aria-hidden="true">↗</span></a>
              <p class="te-card-meta">
                <span class="te-vol">{{ volLabel(a.v) }}</span>
                <template v-if="a.d">
                  <span class="te-dot" :data-d="a.d" aria-hidden="true"></span>{{ diffLabel(a.d) }}
                </template>
                <span v-if="a.m">{{ t.minutes(a.m) }}</span>
              </p>
              <div v-if="a.tg.length" class="te-card-tags">
                <button
                  v-for="tag in a.tg" :key="tag" class="te-mini"
                  :class="{ 'is-on': selected.includes(tag) }"
                  :aria-pressed="selected.includes(tag)" @click="toggle(tag)"
                >{{ tag }}</button>
              </div>
            </li>
          </ul>
          <button v-if="results.length > visible" class="te-more" @click="visible = results.length">
            {{ t.showAll(results.length) }} ↓
          </button>
        </template>
      </section>
    </template>
  </div>
</template>

<style scoped>
.tag-explorer {
  --te-radius: 8px;
  font-size: 14px;
  color: var(--vp-c-text-1);
}
.te-mono {
  font-family: var(--vp-font-family-mono);
  font-size: .74em;
  letter-spacing: .06em;
  color: var(--vp-c-text-3);
  font-weight: 400;
}

/* ── 头部 ── */
.te-head { padding: 12px 0 26px; }
.te-eyebrow {
  margin: 0 0 14px;
  font-family: var(--vp-font-family-mono);
  font-size: 12px;
  letter-spacing: .035em;
  color: var(--vp-c-brand-1);
}
.te-h1 {
  margin: 0 0 14px;
  font-size: clamp(30px, 4vw, 44px);
  line-height: 1.2;
  font-weight: 800;
  letter-spacing: -.04em;
}
.te-sub { margin: 0; color: var(--vp-c-text-2); font-size: 14px; line-height: 1.9; }
.te-sub span { color: var(--vp-c-text-1); font-weight: 600; }

/* ── 工具栏 ── */
.te-toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  align-items: center;
  padding: 16px 0;
  border-top: 1px solid var(--vp-c-divider);
}
.te-search {
  flex: 1 1 240px;
  min-width: 200px;
  padding: 9px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  background: var(--vp-c-bg-soft);
  color: var(--vp-c-text-1);
  font-size: 14px;
  transition: border-color .18s;
}
.te-search:focus { outline: none; border-color: var(--vp-c-brand-1); }
.te-search::placeholder { color: var(--vp-c-text-3); }
.te-facets { display: flex; flex-direction: column; gap: 8px; }
.te-facet { display: flex; align-items: center; flex-wrap: wrap; gap: 6px; }
.te-facet-name {
  font-size: 12px;
  color: var(--vp-c-text-3);
  margin-right: 2px;
  white-space: nowrap;
}
.te-facet-chip {
  padding: 3px 10px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 999px;
  background: transparent;
  color: var(--vp-c-text-2);
  font-size: 12px;
  cursor: pointer;
  transition: all .15s;
}
.te-facet-chip:hover { border-color: var(--vp-c-brand-1); color: var(--vp-c-brand-1); }
.te-facet-chip.is-on {
  background: var(--vp-c-brand-soft-2);
  border-color: var(--vp-c-brand-1);
  color: var(--vp-c-brand-1);
  font-weight: 600;
}

/* ── 已选行 ── */
.te-selected {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
  padding: 10px 14px;
  margin-bottom: 6px;
  background: var(--vp-c-brand-soft);
  border-radius: var(--te-radius);
}

/* ── 标签墙 ── */
.te-wall { padding: 10px 0 26px; border-top: 1px solid var(--vp-c-divider); }
.te-cat { padding: 14px 0 4px; }
.te-cat + .te-cat { border-top: 1px dashed var(--vp-c-divider); }
.te-cat-head {
  font-size: 13px;
  font-weight: 650;
  color: var(--vp-c-text-1);
  margin-bottom: 10px;
}
.te-cat-tags { display: flex; flex-wrap: wrap; gap: 8px; }
.te-wall-empty { color: var(--vp-c-text-3); text-align: center; padding: 18px 0; }
.te-chip {
  display: inline-flex;
  align-items: baseline;
  gap: 7px;
  padding: 5px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 999px;
  background: var(--vp-c-bg-soft);
  color: var(--vp-c-text-1);
  font-size: 13px;
  cursor: pointer;
  transition: border-color .15s, background .15s, color .15s;
}
.te-chip:hover { border-color: var(--vp-c-brand-1); }
.te-chip.is-on {
  background: var(--vp-c-brand-1);
  border-color: var(--vp-c-brand-1);
  color: #fff;
}
.te-chip.is-on .te-count { color: rgba(255, 255, 255, .8); }
.te-chip .te-x { font-weight: 700; }
.te-count {
  font-family: var(--vp-font-family-mono);
  font-size: .78em;
  color: var(--vp-c-text-3);
}
.te-reset {
  border: none;
  background: none;
  color: var(--vp-c-brand-1);
  font-size: 13px;
  cursor: pointer;
  padding: 2px 4px;
}
.te-reset:hover { text-decoration: underline; }

/* ── 结果区 ── */
.te-results { border-top: 1px solid var(--vp-c-divider); padding: 16px 0 40px; }
.te-results-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 10px;
  font-size: 14px;
  font-weight: 650;
  padding-bottom: 14px;
}
.te-results-count { font-size: 12px; font-weight: 400; color: var(--vp-c-text-2); }
.te-hint {
  color: var(--vp-c-text-2);
  font-size: 14px;
  padding: 26px 0;
  text-align: center;
}
.te-empty { text-align: center; padding: 26px 0; }
.te-empty p { color: var(--vp-c-text-2); margin: 0 0 8px; }
.te-list { list-style: none; margin: 0; padding: 0; }
.te-card {
  padding: 14px 16px;
  border: 1px solid var(--vp-c-divider);
  border-radius: var(--te-radius);
  margin-bottom: 10px;
  transition: border-color .18s;
}
.te-card:hover { border-color: var(--vp-c-brand-3); }
.te-card-title {
  font-size: 15px;
  font-weight: 650;
  color: var(--vp-c-text-1);
  text-decoration: none;
  display: inline-block;
}
.te-card-title:hover { color: var(--vp-c-brand-1); }
.te-card-title span { color: var(--vp-c-text-3); font-size: .85em; }
.te-card-meta {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px;
  margin: 6px 0 0;
  color: var(--vp-c-text-2);
  font-size: 12.5px;
}
.te-dot { width: 7px; height: 7px; border-radius: 50%; display: inline-block; }
.te-dot[data-d="beginner"] { background: var(--vp-c-green-1); }
.te-dot[data-d="intermediate"] { background: var(--vp-c-yellow-1); }
.te-dot[data-d="advanced"] { background: var(--vp-c-red-1); }
.te-card-tags { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 9px; }
.te-mini {
  padding: 2px 8px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 4px;
  background: transparent;
  color: var(--vp-c-text-3);
  font-size: 11.5px;
  cursor: pointer;
  transition: all .15s;
}
.te-mini:hover { border-color: var(--vp-c-brand-1); color: var(--vp-c-brand-1); }
.te-mini.is-on {
  background: var(--vp-c-brand-soft-2);
  border-color: var(--vp-c-brand-1);
  color: var(--vp-c-brand-1);
}
.te-more {
  display: block;
  width: 100%;
  padding: 10px;
  margin-top: 4px;
  border: 1px dashed var(--vp-c-divider);
  border-radius: var(--te-radius);
  background: none;
  color: var(--vp-c-text-2);
  font-size: 13px;
  cursor: pointer;
  transition: all .15s;
}
.te-more:hover { border-color: var(--vp-c-brand-1); color: var(--vp-c-brand-1); }
.te-notice { color: var(--vp-c-text-2); padding: 20px 0; }

button:focus-visible,
.te-search:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}

@media (max-width: 768px) {
  .te-facets { width: 100%; }
  .te-search { flex-basis: 100%; }
}
</style>
