<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useData, withBase } from 'vitepress'
import { COMMUNITY_CATEGORIES, type CommunityArticle, type CommunityStatus } from '../utils/community'
import { subscribeAfterRouteChange } from '../router-hooks'
import CommunityArticleCard from './CommunityArticleCard.vue'
import CommunityAvatar from './CommunityAvatar.vue'

type View = 'articles' | 'authors' | 'topics'
const props = withDefaults(defineProps<{ view?: View; status?: CommunityStatus; compact?: boolean }>(), { view: 'articles', compact: false })
const { frontmatter, page } = useData()
const articles = computed(() => (frontmatter.value.communityArticles ?? []) as CommunityArticle[])
const locale = computed(() => page.value.relativePath.startsWith('en/') ? 'en' : 'zh')
const en = computed(() => locale.value === 'en')
const root = computed(() => en.value ? '/en/community/' : '/community/')
const authorId = ref('')
const categoryId = ref('')
const legacyStatus = ref('')
const people = computed(() => {
  const map = new Map<string, { github: string; name: string; count: number; categories: Set<CommunityArticle['category']> }>()
  for (const article of articles.value) for (const person of article.authors) {
    const key = person.github.toLowerCase()
    const existing = map.get(key)
    if (existing) { existing.count++; existing.categories.add(article.category) }
    else map.set(key, { ...person, count: 1, categories: new Set([article.category]) })
  }
  return [...map.values()].sort((a, b) => b.count - a.count || a.github.localeCompare(b.github))
})
const author = computed(() => people.value.find(person => person.github.toLowerCase() === authorId.value.toLowerCase()))
const category = computed(() => Object.hasOwn(COMMUNITY_CATEGORIES, categoryId.value)
  ? categoryId.value as keyof typeof COMMUNITY_CATEGORIES : undefined)
// Preserve shared links from the previous directory while the navigation now has separate destinations.
const activeView = computed(() => props.view !== 'articles' ? props.view : author.value ? 'authors' : category.value ? 'topics' : 'articles')
const isAuthorDirectory = computed(() => activeView.value === 'authors' && !author.value)
const isTopicDirectory = computed(() => activeView.value === 'topics' && !category.value)
const stories = computed(() => articles.value.filter(article =>
  (!(props.status || legacyStatus.value) || article.status === (props.status || legacyStatus.value))
  && (activeView.value !== 'authors' || !author.value || article.authors.some(person => person.github.toLowerCase() === author.value!.github.toLowerCase()))
  && (activeView.value !== 'topics' || !category.value || article.category === category.value),
))
const featured = computed(() => !props.compact && activeView.value === 'articles' ? stories.value[0] : undefined)
const remaining = computed(() => featured.value ? stories.value.slice(1) : stories.value)
const copy = computed(() => en.value ? {
  title: 'The community journal.', intro: 'Notes from learning, building, and understanding C++ together.',
  articles: 'Articles', topics: 'Sections', authors: 'Authors', submit: 'Write with us', guide: 'Submission guide',
  latest: 'From the community', more: 'More to read', allAuthors: 'All authors', allTopics: 'All sections',
  authorsTitle: 'Meet the people behind the notes.', authorsIntro: 'Follow an author’s work, one article at a time.',
  topicsTitle: 'Find your next read.', topicsIntro: 'A few ways into the ideas and experience shared here.',
  profile: 'GitHub profile',
  empty: 'The first story is still to come.', emptyHint: 'Have something to share? We would love to read it.',
  articleCount: (n: number) => `${n} ${n === 1 ? 'article' : 'articles'}`,
  footer: 'There is room for your experience here.', footerSub: 'A learning note, a debugging story, or a question you worked through.',
  community: 'Community', incoming: 'First publications', reviewed: 'Reviewed articles', join: 'Join the conversation',
} : {
  title: '社区文章', intro: '关于 C++ 的学习、实践，还有那些值得记下来的思考。',
  articles: '文章', topics: '栏目', authors: '作者', submit: '写一篇文章', guide: '投稿指南',
  latest: '来自社区', more: '接着读', allAuthors: '全部作者', allTopics: '全部栏目',
  authorsTitle: '认识文章背后的人。', authorsIntro: '从一篇文章出发，看看作者还分享过什么。',
  topicsTitle: '找到你感兴趣的方向。', topicsIntro: '学习中的记录，工程里的经验，顺着一个话题慢慢读。',
  profile: 'GitHub 主页',
  empty: '这里的第一篇文章，还在等你。', emptyHint: '有一段经验想分享？欢迎把它写下来。',
  articleCount: (n: number) => `${n} 篇文章`,
  footer: '也留一个位置，给你的经验。', footerSub: '一次排错、一份笔记、一个终于想明白的问题，都可以是开始。',
  community: '社区', incoming: '初刊文章', reviewed: '已审阅收录', join: '加入交流',
})
const topicDescriptions = {
  'learning-notes': { zh: '把概念弄懂，把实验记下。从一个问题开始，留下学习的过程。', en: 'Concepts, experiments, and the questions that led to understanding.' },
  engineering: { zh: '构建、调试、工具链与项目实践。聊聊代码进入真实工程之后的事。', en: 'Builds, debugging, tools, and lessons from real projects.' },
  'source-reading': { zh: '走进实现细节，读懂设计取舍。沿着源码，看看代码为什么这样写。', en: 'Read implementations closely and discover the decisions behind them.' },
  qa: { zh: '把问过的问题和查过的资料整理好，让下一位读者少绕一点路。', en: 'Questions worked through, with answers and references to return to.' },
}
const topicMarks = { 'learning-notes': '01', engineering: '02', 'source-reading': '03', qa: '04' }
const topicCount = (key: string) => articles.value.filter(article => article.category === key).length
function readUrl() {
  const params = new URLSearchParams(window.location.search)
  authorId.value = params.get('author') ?? ''
  categoryId.value = params.get('category') ?? ''
  legacyStatus.value = ['incoming', 'articles'].includes(params.get('status') ?? '') ? params.get('status')! : ''
}
const unsubscribe = typeof window !== 'undefined' ? subscribeAfterRouteChange(readUrl) : () => {}
onMounted(readUrl)
onUnmounted(unsubscribe)
</script>

<template>
  <div class="community-blog" :class="{ 'community-blog--compact': compact }">
    <template v-if="!compact">
      <header class="community-masthead">
        <div>
          <p class="community-kicker"><span aria-hidden="true"></span> TAMCPP · COMMUNITY</p>
          <h1>{{ copy.title }}</h1>
          <p class="community-intro">{{ copy.intro }}</p>
        </div>
        <a class="community-write" :href="withBase(`${root}submit/`)">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><path d="m15 5 4 4M4 20l4-1 12-12a2.8 2.8 0 0 0-4-4L4 15z" /></svg>
          {{ copy.submit }}
        </a>
      </header>
      <nav class="community-navigation" :aria-label="copy.community">
        <div class="community-nav-pages">
          <a :href="withBase(root)" :aria-current="activeView === 'articles' ? 'page' : undefined">{{ copy.articles }}</a>
          <a :href="withBase(`${root}topics/`)" :aria-current="activeView === 'topics' ? 'page' : undefined">{{ copy.topics }}</a>
          <a :href="withBase(`${root}authors/`)" :aria-current="activeView === 'authors' ? 'page' : undefined">{{ copy.authors }}</a>
        </div>
        <a class="community-guide-link" :href="withBase(`${root}submit/`)">{{ copy.guide }} <span aria-hidden="true">↗</span></a>
      </nav>
    </template>

    <section v-if="isAuthorDirectory" aria-labelledby="community-directory-title">
      <div class="community-page-heading"><h2 id="community-directory-title">{{ copy.authorsTitle }}</h2><p>{{ copy.authorsIntro }}</p></div>
      <div class="community-author-directory">
        <a v-for="person in people" :key="person.github" class="community-author-profile" :href="withBase(`${root}authors/?author=${encodeURIComponent(person.github)}`)">
          <CommunityAvatar :github="person.github" :name="person.name" :size="72" />
          <h3>{{ person.name }}</h3>
          <span class="community-author-handle">@{{ person.github }}</span>
          <p>{{ [...person.categories].map(key => COMMUNITY_CATEGORIES[key][locale]).join(' · ') }}</p>
          <div><span>{{ copy.articleCount(person.count) }}</span><span aria-hidden="true">↗</span></div>
        </a>
      </div>
    </section>

    <section v-else-if="isTopicDirectory" aria-labelledby="community-directory-title">
      <div class="community-page-heading"><h2 id="community-directory-title">{{ copy.topicsTitle }}</h2><p>{{ copy.topicsIntro }}</p></div>
      <div class="community-topic-directory">
        <a v-for="(names, key) in COMMUNITY_CATEGORIES" :key="key" class="community-topic" :href="withBase(`${root}topics/?category=${key}`)">
          <span class="community-topic-number" aria-hidden="true">{{ topicMarks[key] }}</span>
          <div><h3>{{ names[locale] }}</h3><p>{{ topicDescriptions[key][locale] }}</p><span class="community-topic-count">{{ copy.articleCount(topicCount(key)) }}</span></div>
          <span class="community-topic-arrow" aria-hidden="true">↗</span>
        </a>
      </div>
    </section>

    <section v-else class="community-feed" aria-label="copy.articles">
      <header v-if="author && activeView === 'authors'" class="community-profile-heading">
        <a class="community-back" :href="withBase(`${root}authors/`)">← {{ copy.allAuthors }}</a>
        <div class="community-profile-person">
          <CommunityAvatar :github="author.github" :name="author.name" :size="80" />
          <div><h2>{{ author.name }}</h2><p>{{ copy.articleCount(stories.length) }}<span aria-hidden="true"> · </span><a :href="`https://github.com/${author.github}`" target="_blank" rel="noopener noreferrer">{{ copy.profile }} ↗</a></p></div>
        </div>
      </header>
      <header v-else-if="category && activeView === 'topics'" class="community-page-heading">
        <a class="community-back" :href="withBase(`${root}topics/`)">← {{ copy.allTopics }}</a>
        <h2>{{ COMMUNITY_CATEGORIES[category][locale] }}</h2><p>{{ topicDescriptions[category][locale] }}</p>
      </header>
      <template v-if="stories.length">
        <template v-if="featured">
          <div class="community-section-label"><span>{{ copy.latest }}</span><span>THE JOURNAL</span></div>
          <CommunityArticleCard :article="featured" :locale="locale" featured />
        </template>
        <div v-if="remaining.length" class="community-next-stories">
          <div v-if="featured" class="community-section-label"><h2>{{ copy.more }}</h2><span>{{ copy.articleCount(remaining.length) }}</span></div>
          <div class="community-story-grid" :class="{ 'community-story-grid--archive': !featured }">
            <CommunityArticleCard v-for="article in remaining" :key="article.url" :article="article" :locale="locale" />
          </div>
        </div>
      </template>
      <div v-else class="community-empty">
        <span class="community-empty-mark" aria-hidden="true">[ &nbsp; ]</span>
        <h3>{{ copy.empty }}</h3><p>{{ copy.emptyHint }}</p>
        <a :href="withBase(`${root}submit/`)">{{ copy.submit }} <span aria-hidden="true">↗</span></a>
      </div>
    </section>

    <footer v-if="!compact" class="community-endnote">
      <div><h2>{{ copy.footer }}</h2><p>{{ copy.footerSub }}</p></div>
      <a :href="withBase(`${root}submit/`)">{{ copy.submit }} <span aria-hidden="true">↗</span></a>
    </footer>
    <nav v-if="!compact" class="community-footnotes" :aria-label="copy.community">
      <span>TAMCPP / COMMUNITY</span>
      <div><a :href="withBase(`${root}incoming/`)">{{ copy.incoming }}</a><a :href="withBase(`${root}articles/`)">{{ copy.reviewed }}</a><a :href="withBase(`${root}join`)">{{ copy.join }}</a></div>
    </nav>
  </div>
</template>

<style scoped>
.community-blog { width: 100%; max-width: 1184px; margin: 0 auto; padding: 58px 32px 40px; color: var(--vp-c-text-1); }
.community-blog--compact { padding: 16px 0; }
.community-masthead { display: flex; justify-content: space-between; align-items: center; gap: 32px; padding-bottom: 36px; }
.community-kicker { display: flex; align-items: center; gap: 9px; margin: 0 0 20px; color: var(--vp-c-text-2); font-family: var(--vp-font-family-mono); font-size: 11px; letter-spacing: .12em; }
.community-kicker > span { width: 7px; height: 7px; border-radius: 50%; background: var(--vp-c-brand-1); }
.community-masthead h1 { margin: 0; font-size: clamp(34px, 4vw, 48px); font-weight: 650; letter-spacing: -.055em; line-height: 1.3; }
.community-intro { margin: 16px 0 0; font-size: 15px; line-height: 1.8; color: var(--vp-c-text-2); }
.community-write { display: flex; flex-shrink: 0; align-items: center; gap: 10px; padding: 11px 17px; border: 1px solid var(--vp-c-text-1); border-radius: 6px; color: var(--vp-c-text-1); font-size: 13px; font-weight: 500; text-decoration: none; transition: background .15s; }
.community-write:hover { background: var(--vp-c-bg-soft); }
.community-navigation { display: flex; justify-content: space-between; align-items: center; gap: 24px; border-bottom: 1px solid var(--vp-c-divider); margin-bottom: 36px; }
.community-nav-pages { display: flex; gap: 34px; }
.community-nav-pages > a { position: relative; padding: 16px 1px; color: var(--vp-c-text-2); font-size: 14px; text-decoration: none; }
.community-nav-pages > a[aria-current] { color: var(--vp-c-text-1); font-weight: 600; }
.community-nav-pages > a[aria-current]::after { position: absolute; bottom: -1px; right: 0; left: 0; height: 2px; background: var(--vp-c-text-1); content: ''; }
.community-guide-link { color: var(--vp-c-text-2); font-size: 12px; text-decoration: none; white-space: nowrap; }
.community-guide-link > span { margin-left: 8px; }
.community-section-label { display: flex; justify-content: space-between; align-items: baseline; gap: 16px; margin: 0 0 18px; font-size: 13px; }
.community-section-label > :last-child { color: var(--vp-c-text-2); font-size: 10px; letter-spacing: .08em; }
.community-section-label h2 { margin: 0; padding: 0; border: 0; font-size: 15px; font-weight: 600; }
.community-next-stories { margin-top: 38px; }
.community-story-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 20px; }
.community-story-grid--archive { grid-template-columns: repeat(2, minmax(0, 1fr)); }
.community-page-heading { padding: 14px 0 30px; }
.community-page-heading h2, .community-profile-heading h2 { margin: 0; padding: 0; border: 0; font-size: 30px; font-weight: 600; line-height: 1.5; letter-spacing: -.035em; }
.community-page-heading p { margin: 12px 0 0; color: var(--vp-c-text-2); font-size: 14px; line-height: 1.8; }
.community-author-directory { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 24px; }
.community-author-profile { display: flex; flex-direction: column; align-items: flex-start; min-width: 0; padding: 32px; border: 1px solid var(--vp-c-divider); border-radius: 12px; color: var(--vp-c-text-1); background: var(--vp-c-bg-elv); text-decoration: none; transition: border-color .2s; }
.community-author-profile:hover { border-color: var(--vp-c-brand-1); }
.community-author-profile h3 { margin: 22px 0 4px; font-size: 20px; font-weight: 600; }
.community-author-handle { font-size: 12px; color: var(--vp-c-text-2); overflow-wrap: anywhere; }
.community-author-profile > p { margin: 24px 0; color: var(--vp-c-text-2); font-size: 13px; }
.community-author-profile > div { display: flex; justify-content: space-between; align-items: center; align-self: stretch; padding-top: 18px; margin-top: auto; border-top: 1px solid var(--vp-c-divider); font-size: 12px; }
.community-author-profile > div > :last-child { font-size: 20px; color: var(--vp-c-brand-1); }
.community-topic-directory { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 20px; }
.community-topic { position: relative; display: flex; gap: 24px; align-items: flex-start; padding: 32px; border: 1px solid var(--vp-c-divider); border-radius: 12px; color: var(--vp-c-text-1); background: var(--vp-c-bg-elv); text-decoration: none; transition: border-color .2s; }
.community-topic:hover { border-color: var(--vp-c-brand-1); }
.community-topic-number { padding-top: 4px; color: var(--vp-c-brand-1); font-family: var(--vp-font-family-mono); font-size: 12px; }
.community-topic h3 { margin: 0 0 12px; font-size: 22px; font-weight: 600; }
.community-topic p { margin: 0 0 24px; font-size: 14px; line-height: 1.85; color: var(--vp-c-text-2); }
.community-topic-count { font-size: 12px; color: var(--vp-c-text-2); }
.community-topic-arrow { position: absolute; right: 24px; bottom: 24px; color: var(--vp-c-brand-1); font-size: 20px; }
.community-back { display: inline-block; margin-bottom: 26px; color: var(--vp-c-text-2); font-size: 12px; text-decoration: none; }
.community-profile-heading { padding: 10px 0 0; }
.community-profile-person { display: flex; align-items: center; gap: 22px; }
.community-profile-person p { margin: 10px 0 0; color: var(--vp-c-text-2); font-size: 13px; }
.community-profile-person p a { color: inherit; text-decoration: none; }
.community-empty { padding: 48px 24px 64px; text-align: center; background: var(--vp-c-bg-soft); border-radius: 12px; }
.community-empty-mark { display: block; margin-bottom: 24px; color: var(--vp-c-brand-1); font: 38px var(--vp-font-family-mono); }
.community-empty h3 { margin: 0 0 12px; font-size: 20px; font-weight: 500; }
.community-empty p { color: var(--vp-c-text-2); font-size: 14px; }
.community-empty a { display: inline-block; margin-top: 18px; font-size: 14px; color: var(--vp-c-brand-1); text-decoration: none; }
.community-endnote { display: flex; justify-content: space-between; align-items: center; gap: 32px; padding: 32px 0; margin-top: 64px; border-top: 1px solid var(--vp-c-divider); border-bottom: 1px solid var(--vp-c-divider); }
.community-endnote h2 { margin: 0; padding: 0; border: 0; font-size: 22px; font-weight: 500; letter-spacing: -.025em; }
.community-endnote p { margin: 12px 0 0; font-size: 13px; line-height: 1.8; color: var(--vp-c-text-2); }
.community-endnote > a { flex-shrink: 0; color: var(--vp-c-brand-1); font-size: 13px; text-decoration: none; }
.community-endnote > a > span { margin-left: 12px; }
.community-footnotes { display: flex; justify-content: space-between; align-items: baseline; gap: 20px; margin-top: 24px; color: var(--vp-c-text-2); font-size: 11px; }
.community-footnotes > span { font-family: var(--vp-font-family-mono); letter-spacing: .05em; }
.community-footnotes > div { display: flex; flex-wrap: wrap; gap: 20px; }
.community-footnotes a { color: inherit; text-decoration: none; }
.community-blog a:focus-visible { outline: 2px solid var(--vp-c-brand-1); outline-offset: 5px; border-radius: 2px; }
.community-blog--compact .community-story-grid { grid-template-columns: 1fr; }
.community-blog--compact .community-next-stories { margin-top: 0; }
@media (max-width: 1023px) {
  .community-story-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .community-author-directory { grid-template-columns: repeat(2, minmax(0, 1fr)); }
}
@media (max-width: 767px) {
  .community-blog { padding: 32px 22px; }
  .community-blog--compact { padding: 12px 0; }
  .community-masthead { align-items: flex-start; gap: 16px; padding-bottom: 22px; }
  .community-masthead h1 { font-size: 34px; }
  .community-kicker { font-size: 9px; letter-spacing: .08em; margin-bottom: 16px; }
  .community-intro { max-width: 310px; font-size: 13px; }
  .community-write { padding: 9px 11px; font-size: 12px; gap: 6px; margin-top: 28px; }
  .community-write svg { display: none; }
  .community-navigation { gap: 12px; margin-bottom: 26px; }
  .community-nav-pages { gap: 26px; }
  .community-guide-link { font-size: 11px; }
  .community-story-grid, .community-topic-directory, .community-author-directory { grid-template-columns: 1fr; gap: 16px; }
  .community-page-heading h2, .community-profile-heading h2 { font-size: 25px; }
  .community-topic { padding: 26px; gap: 18px; }
  .community-endnote { align-items: flex-start; flex-direction: column; margin-top: 42px; gap: 22px; }
  .community-endnote h2 { font-size: 20px; }
  .community-footnotes { flex-direction: column; gap: 14px; font-size: 10px; }
  .community-profile-person { gap: 16px; }
}
@media (max-width: 420px) {
  .community-navigation { flex-wrap: wrap; gap: 0 12px; }
  .community-guide-link { padding: 12px 0; }
  .community-masthead:lang(en) { flex-wrap: wrap; }
  .community-masthead:lang(en) .community-write { margin-top: 0; }
}
@media (prefers-reduced-motion: reduce) { .community-author-profile, .community-topic, .community-write { transition: none; } }
</style>
