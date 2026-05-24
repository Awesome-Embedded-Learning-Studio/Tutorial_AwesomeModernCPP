import DefaultTheme from 'vitepress/theme'
import { h } from 'vue'
import type { Theme } from 'vitepress'
import HomeTipBanner from './components/HomeTipBanner.vue'
import ChapterNav from './components/ChapterNav.vue'
import ChapterLink from './components/ChapterLink.vue'
import TalkInfoCard from './components/TalkInfoCard.vue'
import RefLink from './components/RefLink.vue'
import ReferenceCard from './components/ReferenceCard.vue'
import ReferenceItem from './components/ReferenceItem.vue'
import OnlineCompilerDemo from './components/OnlineCompilerDemo.vue'
import { setupMermaid } from './mermaid-client'
import './custom.css'

export default {
  extends: DefaultTheme,
  Layout() {
    return h(DefaultTheme.Layout, null, {
      'home-features-before': () => h(HomeTipBanner)
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
