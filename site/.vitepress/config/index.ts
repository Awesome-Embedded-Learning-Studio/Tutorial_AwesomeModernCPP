import { defineConfig } from 'vitepress'
import withDrawio from '@dhlx/vitepress-plugin-drawio'
import { navEn } from './nav'
import { applyWeeklyPageData, generateWeeklyManifest, MANIFEST_REL } from './weekly-manifest'
import { applyTagsPageData } from './tags-manifest'
import { buildSidebar } from './sidebar'
import { sharedThemeConfig, sharedMarkdown, makeSocialLinks, localSearchBoxAlias } from './shared'
import { createReadStream, existsSync } from 'node:fs'
import { join, normalize } from 'node:path'
import { fileURLToPath } from 'node:url'

// dev 模式下把 code/ 下两个源码目录作为静态资源服务，让 OnlineCompilerDemo / QuizProblem
// 等组件在 dev 下也能 fetch 到源码。build 时由 scripts/build.ts 末尾 copy 进 dist，
// 故这里只需覆盖 dev；SITE_BASE 须与下方 base 保持一致。
const PROJECT_ROOT = fileURLToPath(new URL('../../../', import.meta.url))
const SITE_BASE = '/Tutorial_AwesomeModernCPP/'
// [URL 前缀, 对应仓库物理目录];weekly-problems 题目四件套走 code/volumn_codes/
const CODE_STATIC_ROOTS: Array<{ prefix: string; root: string }> = [
  { prefix: 'code/examples/', root: normalize(join(PROJECT_ROOT, 'code', 'examples')) },
  { prefix: 'code/volumn_codes/', root: normalize(join(PROJECT_ROOT, 'code', 'volumn_codes')) },
]

function serveCodeExamplesInDev() {
  return {
    name: 'serve-code-examples-in-dev',
    apply: 'serve' as const,
    configureServer(server: { middlewares: { use: (m: any) => void } }) {
      server.middlewares.use((req: any, res: any, next: any) => {
        const url = decodeURIComponent(String(req.url ?? '').split('?')[0])
        const rel = url.startsWith(SITE_BASE) ? url.slice(SITE_BASE.length) : url
        // 每周一些题 manifest:dev 下现生成,改题即生效(build 时由 build.ts 写进 dist)
        if (rel === MANIFEST_REL) {
          res.setHeader('Content-Type', 'application/json; charset=utf-8')
          res.end(generateWeeklyManifest(PROJECT_ROOT))
          return
        }
        for (const { prefix, root } of CODE_STATIC_ROOTS) {
          if (!rel.startsWith(prefix)) continue
          // 规范化后必须仍落在对应目录内，防止路径穿越
          const filePath = normalize(join(root, rel.slice(prefix.length)))
          if (!filePath.startsWith(root) || !existsSync(filePath)) continue
          res.setHeader('Content-Type', 'text/plain; charset=utf-8')
          createReadStream(filePath).pipe(res)
          return
        }
        next()
      })
    },
  }
}

export default withDrawio(defineConfig({
  vite: {
    plugins: [serveCodeExamplesInDev()],
    resolve: {
      alias: localSearchBoxAlias,
    },
    ssr: {
      // mermaid / @panzoom/panzoom 都只在客户端 onMounted 后动态 import 求值,
      // SSR 阶段不能也不应打包求值(mermaid 访问 document、panzoom 访问 DOM)。
      external: ['mermaid', '@panzoom/panzoom'],
    },
  },

  srcDir: '../documents',
  transformPageData(pageData) {
    applyWeeklyPageData(pageData, PROJECT_ROOT)
    applyTagsPageData(pageData)
  },

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
        // root 构建里 EN locale 只覆盖这几项,其余继承上面的中文 themeConfig;
        // socialLinks 不补 base 前缀,必须显式给 EN 自己的加群页全路径
        socialLinks: makeSocialLinks('/Tutorial_AwesomeModernCPP/en/community/join'),
        editLink: {
          pattern: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/edit/main/documents/:path',
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
      `(function(){try{var w=parseInt(localStorage.getItem('vp-sidebar-width'));if(!w||w<200||w>480){w=240;}document.documentElement.style.setProperty('--vp-sidebar-width',w+'px');var a=parseInt(localStorage.getItem('vp-aside-width'));if(!a||a<180||a>360){a=256;}document.documentElement.style.setProperty('--vp-aside-width',a+'px');}catch(e){}})()`,
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
})
// 注: 不传 zoom —— 插件会把它翻译成每张图的 toolbar 配置, GraphViewer 据此
// 渲染 body 级绝对定位的悬浮工具条(坐标初始化时定格, 滚动后飘到图外压正文),
// 2026-09-05 幽灵工具条事故的真凶, 勿再加回。
