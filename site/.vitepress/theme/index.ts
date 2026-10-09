import DefaultTheme from 'vitepress/theme'
import { h } from 'vue'
import type { Theme } from 'vitepress'
import HomeTipBanner from './components/HomeTipBanner.vue'
import ReadingProgress from './components/ReadingProgress.vue'
import ScreenshotCarousel from './components/ScreenshotCarousel.vue'
import ChapterNav from './components/ChapterNav.vue'
import ChapterLink from './components/ChapterLink.vue'
import TalkInfoCard from './components/TalkInfoCard.vue'
import RefLink from './components/RefLink.vue'
import ReferenceCard from './components/ReferenceCard.vue'
import ReferenceItem from './components/ReferenceItem.vue'
import OnlineCompilerDemo from './components/OnlineCompilerDemo.vue'
import QuizProblem from './components/QuizProblem.vue'
import QuizSolutions from './components/QuizSolutions.vue'
import QuizHome from './components/QuizHome.vue'
import WeeklyHomeWidget from './components/WeeklyHomeWidget.vue'
import WeeklyPageHeader from './components/WeeklyPageHeader.vue'
import WeeklyPracticeProvider from './components/WeeklyPracticeProvider.vue'
import HomeHeroVisual from './components/HomeHeroVisual.vue'
import ProofStrip from './components/ProofStrip.vue'
import HomePathExplorer from './components/HomePathExplorer.vue'
import FontSizeSwitcher from './components/FontSizeSwitcher.vue'
import ResizableSidebar from './components/ResizableSidebar.vue'
import { setupMermaid } from './mermaid-client'
import MermaidLightbox from './components/MermaidLightbox.vue'
import { setupDocImageZoom } from './image-client'
import ImageLightbox from './components/ImageLightbox.vue'
import NavSpinner from './components/NavSpinner.vue'
import QQGroupCard from './components/QQGroupCard.vue'
import Anim from './components/Anim.vue'
import TagExplorer from './components/TagExplorer.vue'
import DocTags from './components/DocTags.vue'
import StatusToast from './components/StatusToast.vue'
import BookmarkButton from './components/BookmarkButton.vue'
import BookmarkList from './components/BookmarkList.vue'
import NoteCapture from './components/NoteCapture.vue'
import ArticleNotes from './components/ArticleNotes.vue'
import { setupBookmarkRestore, setupBookmarkAutoTrack } from './composables/useBookmarks'
import ArticleContributors from './components/ArticleContributors.vue'
import { setupDevFakeLag } from './dev-fake-lag'
import './custom.css'
import './article-code.css'
import './article-quote.css'
import './article-table.css'
import './article-list.css'
import './weekly.css'
import './tags.css'
import './article-contributors.css'

export default {
  extends: DefaultTheme,
  Layout() {
    return h(WeeklyPracticeProvider, null, { default: () => h(DefaultTheme.Layout, null, {
      'layout-top': () => [h(NavSpinner), h(ReadingProgress), h(ResizableSidebar), h(MermaidLightbox), h(ImageLightbox), h(StatusToast), h(NoteCapture)],
      'doc-before': () => [h(WeeklyPageHeader), h(ArticleContributors, { variant: 'meta' })],
      'doc-footer-before': () => [h(ArticleContributors, { automatic: true }), h(DocTags)],
      'aside-outline-after': () => h(ArticleNotes),
      'home-hero-image': () => h(HomeHeroVisual),
      'home-hero-actions-after': () => h('div', { class: 'proof-on-mobile' }, [h(WeeklyHomeWidget), h(ProofStrip)]),
      'home-hero-after': () => [h(WeeklyHomeWidget), h('div', { class: 'proof-on-desktop' }, [h(ProofStrip)])],
      'home-features-before': () =>
        h('div', { class: 'home-pre-features' }, [h(ScreenshotCarousel), h(HomeTipBanner)]),
      'home-features-after': () => h(HomePathExplorer),
      'nav-bar-content-after': () => [h(FontSizeSwitcher), h(BookmarkButton)],
      'nav-screen-content-after': () => [h(FontSizeSwitcher), h(BookmarkButton)],
    }) })
  },
  setup() {
    setupMermaid()
    setupDocImageZoom()
    setupDevFakeLag()
    setupBookmarkRestore()
    setupBookmarkAutoTrack()
  },
  enhanceApp({ app }) {
    app.component('ChapterNav', ChapterNav)
    app.component('ChapterLink', ChapterLink)
    app.component('TalkInfoCard', TalkInfoCard)
    app.component('RefLink', RefLink)
    app.component('ReferenceCard', ReferenceCard)
    app.component('ReferenceItem', ReferenceItem)
    app.component('OnlineCompilerDemo', OnlineCompilerDemo)
    app.component('QuizProblem', QuizProblem)
    app.component('QuizSolutions', QuizSolutions)
    app.component('QuizHome', QuizHome)
    app.component('QQGroupCard', QQGroupCard)
    app.component('TagExplorer', TagExplorer)
    app.component('ArticleContributors', ArticleContributors)
    app.component('Anim', Anim)
    app.component('BookmarkList', BookmarkList)
  }
} satisfies Theme
