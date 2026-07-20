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
import HomeHeroVisual from './components/HomeHeroVisual.vue'
import ProofStrip from './components/ProofStrip.vue'
import HomeRoadmap from './components/HomeRoadmap.vue'
import FontSizeSwitcher from './components/FontSizeSwitcher.vue'
import ResizableSidebar from './components/ResizableSidebar.vue'
import { setupMermaid } from './mermaid-client'
import MermaidLightbox from './components/MermaidLightbox.vue'
import './custom.css'

export default {
  extends: DefaultTheme,
  Layout() {
    return h(DefaultTheme.Layout, null, {
      'layout-top': () => [h(ReadingProgress), h(ResizableSidebar), h(MermaidLightbox)],
      'home-hero-image': () => h(HomeHeroVisual),
      'home-hero-actions-after': () => h('div', { class: 'proof-on-mobile' }, [h(ProofStrip)]),
      'home-hero-after': () => h('div', { class: 'proof-on-desktop' }, [h(ProofStrip)]),
      'home-features-before': () =>
        h('div', { class: 'home-pre-features' }, [h(ScreenshotCarousel), h(HomeTipBanner)]),
      'home-features-after': () => h(HomeRoadmap),
      'nav-bar-content-after': () => h(FontSizeSwitcher),
      'nav-screen-content-after': () => h(FontSizeSwitcher),
    })
  },
  setup() {
    setupMermaid()
  },
  enhanceApp({ app }) {
    app.component('ChapterNav', ChapterNav)
    app.component('ChapterLink', ChapterLink)
    app.component('TalkInfoCard', TalkInfoCard)
    app.component('RefLink', RefLink)
    app.component('ReferenceCard', ReferenceCard)
    app.component('ReferenceItem', ReferenceItem)
    app.component('OnlineCompilerDemo', OnlineCompilerDemo)
  }
} satisfies Theme
