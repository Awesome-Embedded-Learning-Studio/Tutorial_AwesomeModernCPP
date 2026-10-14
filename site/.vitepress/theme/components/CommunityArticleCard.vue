<script setup lang="ts">
import { computed } from 'vue'
import { withBase } from 'vitepress'
import CommunityAvatar from './CommunityAvatar.vue'
import { COMMUNITY_CATEGORIES, type CommunityArticle } from '../utils/community'

const props = withDefaults(defineProps<{ article: CommunityArticle; locale: 'zh' | 'en'; featured?: boolean }>(), { featured: false })
const root = computed(() => props.locale === 'en' ? '/en/community/' : '/community/')
const copy = computed(() => props.locale === 'en'
  ? { read: 'Read article', minutes: 'min read', incoming: 'Open for review', articles: 'Reviewed', source: 'Community submission' }
  : { read: '阅读全文', minutes: '分钟阅读', incoming: '初刊', articles: '已审阅', source: '社区来稿' })
const date = computed(() => props.article.publishedAt?.replaceAll('-', '.'))
</script>

<template>
  <article class="community-story" :class="{ 'community-story--featured': featured }">
    <div class="community-story-body">
      <div class="community-story-meta">
        <a :href="withBase(`${root}topics/?category=${article.category}`)">{{ COMMUNITY_CATEGORIES[article.category][locale] }}</a>
        <span aria-hidden="true">·</span>
        <time v-if="date" :datetime="article.publishedAt">{{ date }}</time>
        <span v-else>{{ copy.source }}</span>
      </div>
      <h2><a class="community-story-title" :href="withBase(article.url)">{{ article.title }}</a></h2>
      <p class="community-story-description">{{ article.description }}</p>
      <div class="community-story-bottom">
        <div class="community-story-authors">
          <a v-for="person in article.authors" :key="person.github" class="community-byline"
            :href="withBase(`${root}authors/?author=${encodeURIComponent(person.github)}`)">
            <CommunityAvatar :github="person.github" :name="person.name" :size="featured ? 40 : 32" />
            <span>{{ person.name }}</span>
          </a>
          <span v-if="!article.authors.length" class="community-story-source">{{ copy.source }}</span>
        </div>
        <span class="community-story-time">{{ article.readingTime ? `${article.readingTime} ${copy.minutes}` : copy[article.status] }}</span>
      </div>
    </div>
    <div v-if="featured" class="community-story-paper" aria-hidden="true">
      <span class="community-paper-edition">THE COMMUNITY NOTEBOOK</span>
      <span class="community-paper-symbol">C<span>++</span></span>
      <div class="community-paper-rule"></div>
      <span class="community-paper-category">{{ COMMUNITY_CATEGORIES[article.category][locale] }}</span>
      <span class="community-paper-caption">{{ locale === 'en' ? 'NOTES FROM PRACTICE' : '记录 · 实践 · 思考' }}</span>
      <span class="community-paper-corner">↗</span>
    </div>
    <span class="community-story-read" aria-hidden="true">{{ copy.read }} <span>↗</span></span>
  </article>
</template>

<style scoped>
.community-story { position: relative; display: flex; flex-direction: column; min-width: 0; padding: 28px; border: 1px solid var(--vp-c-divider); border-radius: 12px; background: var(--vp-c-bg-elv); transition: border-color .2s, box-shadow .2s, transform .2s; }
.community-story:hover { border-color: var(--vp-c-brand-1); box-shadow: 0 8px 26px rgb(0 0 0 / .035); transform: translateY(-2px); }
.community-story-body { display: flex; flex: 1; flex-direction: column; min-width: 0; }
.community-story-meta { display: flex; flex-wrap: wrap; align-items: center; gap: 9px; font-size: 12px; line-height: 1.5; color: var(--vp-c-text-2); }
.community-story-meta a { position: relative; z-index: 2; color: var(--vp-c-brand-1); font-weight: 600; text-decoration: none; }
.community-story h2 { margin: 18px 0 12px; padding: 0; border: 0; font-size: 22px; line-height: 1.55; font-weight: 650; letter-spacing: -.035em; text-wrap: balance; }
.community-story-title { color: var(--vp-c-text-1); text-decoration: none; }
.community-story-title::after { position: absolute; inset: 0; z-index: 1; border-radius: 12px; content: ''; }
.community-story-description { margin: 0 0 28px; color: var(--vp-c-text-2); font-size: 14px; line-height: 1.85; }
.community-story-bottom { display: flex; flex-wrap: wrap; align-items: center; gap: 12px; margin-top: auto; }
.community-story-authors { display: flex; flex-wrap: wrap; gap: 10px; }
.community-byline { position: relative; z-index: 2; display: inline-flex; align-items: center; gap: 9px; color: var(--vp-c-text-1); font-size: 13px; font-weight: 500; text-decoration: none; }
.community-byline:hover { color: var(--vp-c-brand-1); }
.community-story-time, .community-story-source { color: var(--vp-c-text-2); font-size: 12px; }
.community-story-time { margin-left: auto; white-space: nowrap; }
.community-story-read { display: flex; align-items: center; justify-content: space-between; margin-top: 24px; padding-top: 18px; border-top: 1px solid var(--vp-c-divider); color: var(--vp-c-text-2); font-size: 12px; }
.community-story-read > span { color: var(--vp-c-brand-1); font-size: 18px; }
.community-story a:focus-visible { outline: 2px solid var(--vp-c-brand-1); outline-offset: 5px; border-radius: 2px; }
.community-story--featured { display: grid; grid-template-columns: minmax(0, 1.45fr) minmax(0, 1fr); padding: 0; overflow: hidden; }
.community-story--featured .community-story-body { padding: 44px; }
.community-story--featured h2 { max-width: 580px; margin-top: 24px; font-size: clamp(27px, 3vw, 36px); line-height: 1.45; }
.community-story--featured .community-story-description { max-width: 560px; font-size: 15px; }
.community-story--featured .community-story-bottom { padding-top: 14px; }
.community-story--featured .community-story-read { display: none; }
.community-story-paper { position: relative; display: flex; flex-direction: column; justify-content: center; min-height: 340px; padding: 36px 42px; overflow: hidden; color: #285bb1; background: #e9eef5; border-left: 1px solid var(--vp-c-divider); }
.community-paper-edition { position: absolute; top: 30px; font-size: 10px; letter-spacing: .16em; font-weight: 600; }
.community-paper-symbol { font-family: Georgia, 'Times New Roman', serif; font-weight: 600; font-size: clamp(94px, 12vw, 148px); line-height: 1.2; letter-spacing: -.08em; }
.community-paper-symbol > span { font-size: .55em; vertical-align: top; line-height: 1.7; letter-spacing: -.05em; }
.community-paper-rule { width: 44px; height: 2px; margin: 16px 0 20px; background: currentColor; }
.community-paper-category { font-size: 22px; font-weight: 600; letter-spacing: .06em; }
.community-paper-caption { margin-top: 8px; font-size: 10px; letter-spacing: .14em; opacity: .75; }
.community-paper-corner { position: absolute; right: 30px; bottom: 28px; font-size: 32px; font-weight: 300; }
:global(.dark) .community-story-paper { background: #202a38; color: #a9c5ed; }
@media (max-width: 767px) {
  .community-story { padding: 24px; }
  .community-story--featured { padding: 0; grid-template-columns: 1fr; }
  .community-story--featured .community-story-body { padding: 28px; }
  .community-story-paper { display: none; }
  .community-story--featured h2 { font-size: 28px; }
  .community-story h2 { font-size: 21px; }
}
@media (prefers-reduced-motion: reduce) { .community-story { transition: none; } .community-story:hover { transform: none; } }
</style>
