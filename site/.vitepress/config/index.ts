import { defineConfig } from 'vitepress'
import withDrawio from '@dhlx/vitepress-plugin-drawio'
import { navEn } from './nav'
import { buildSidebar } from './sidebar'
import { sharedThemeConfig, sharedMarkdown } from './shared'
import { createReadStream, existsSync } from 'node:fs'
import { join, normalize } from 'node:path'
import { fileURLToPath } from 'node:url'

// dev 模式下把 code/examples 作为静态资源服务，让 OnlineCompilerDemo 等组件在 dev 下也能 fetch 到源码。
// build 时由 scripts/build.ts 末尾 copy 进 dist，故这里只需覆盖 dev；SITE_BASE 须与下方 base 保持一致。
const PROJECT_ROOT = fileURLToPath(new URL('../../../', import.meta.url))
const EXAMPLES_ROOT = normalize(join(PROJECT_ROOT, 'code', 'examples'))
const SITE_BASE = '/Tutorial_AwesomeModernCPP/'

function serveCodeExamplesInDev() {
  return {
    name: 'serve-code-examples-in-dev',
    apply: 'serve' as const,
    configureServer(server: { middlewares: { use: (m: any) => void } }) {
      server.middlewares.use((req: any, res: any, next: any) => {
        const url = decodeURIComponent(String(req.url ?? '').split('?')[0])
        const rel = url.startsWith(SITE_BASE) ? url.slice(SITE_BASE.length) : url
        if (!rel.startsWith('code/examples/')) return next()
        // 规范化后必须仍落在 code/examples 内，防止路径穿越
        const filePath = normalize(join(EXAMPLES_ROOT, rel.slice('code/examples/'.length)))
        if (!filePath.startsWith(EXAMPLES_ROOT) || !existsSync(filePath)) return next()
        res.setHeader('Content-Type', 'text/plain; charset=utf-8')
        createReadStream(filePath).pipe(res)
      })
    },
  }
}

export default withDrawio(defineConfig({
  vite: {
    plugins: [serveCodeExamplesInDev()],
    ssr: {
      external: ['mermaid'],
    },
  },

  srcDir: '../documents',

  title: '现代 C++ 教程',
  description: '系统化的现代 C++ 教程 — 从基础入门到领域实战',
  lang: 'zh-CN',
  base: '/Tutorial_AwesomeModernCPP/',
  cleanUrls: true,
  lastUpdated: true,

  vue: {
    template: {
      compilerOptions: {
        isCustomElement: (tag: string) => tag.includes('-') || tag.includes('.'),
      },
    },
  },

  locales: {
    root: {
      label: '中文',
      lang: 'zh-CN',
      title: '现代 C++ 教程',
      description: '系统化的现代 C++ 教程 — 从基础入门到领域实战',
    },
    en: {
      label: 'English',
      lang: 'en-US',
      title: 'Modern C++ Tutorial',
      description: 'A systematic modern C++ tutorial — from fundamentals to domain practice',
      link: '/en/',
      themeConfig: {
        nav: navEn,
        editLink: {
          pattern: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/edit/main/documents/en/:path',
          text: 'Edit this page on GitHub',
        },
      },
    },
  },

  head: [
    ['link', { rel: 'icon', href: '/Tutorial_AwesomeModernCPP/favicon.ico' }],
    // 首屏立即应用字号档(从 localStorage 读,默认 medium),防刷新闪烁。
    // 与 FontSizeSwitcher.vue 的 STORAGE_KEY('vp-font-size')保持一致。
    [
      'script',
      {},
      `(function(){try{var s=localStorage.getItem('vp-font-size')||'normal';if(s!=='xxsmall'&&s!=='small'&&s!=='normal'&&s!=='large'&&s!=='xxlarge'){s='normal';}document.documentElement.dataset.fontSize=s;}catch(e){}})()`,
    ],
    // 首屏立即应用侧栏宽度(左导航 + 右大纲),防刷新闪烁。key 与 ResizableSidebar.vue 一致。
    [
      'script',
      {},
      `(function(){try{var w=parseInt(localStorage.getItem('vp-sidebar-width'));if(!w||w<200||w>480){w=272;}document.documentElement.style.setProperty('--vp-sidebar-width',w+'px');var a=parseInt(localStorage.getItem('vp-aside-width'));if(!a||a<180||a>360){a=256;}document.documentElement.style.setProperty('--vp-aside-width',a+'px');}catch(e){}})()`,
    ],
  ],

  markdown: sharedMarkdown,

  themeConfig: {
    ...sharedThemeConfig(),
    sidebar: buildSidebar(),
  },
}), {
  width: '100%',
  height: '600px',
  darkMode: 'auto',
  resize: true,
  zoom: true,
})
