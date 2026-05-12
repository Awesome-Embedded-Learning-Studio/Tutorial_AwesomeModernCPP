import DefaultTheme from 'vitepress/theme'
import { h } from 'vue'
import type { Theme } from 'vitepress'
import HomeTipBanner from './components/HomeTipBanner.vue'
import ChapterNav from './components/ChapterNav.vue'
import ChapterLink from './components/ChapterLink.vue'
import './custom.css'

export default {
  extends: DefaultTheme,
  Layout() {
    return h(DefaultTheme.Layout, null, {
      'home-features-before': () => h(HomeTipBanner)
    })
  },
  enhanceApp({ app }) {
    app.component('ChapterNav', ChapterNav)
    app.component('ChapterLink', ChapterLink)
  }
} satisfies Theme
