import type { DefaultTheme, PageData } from 'vitepress'
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { join } from 'node:path'
import { navZh, navEn } from './nav'
import { getGitTimestampMs } from './git-timestamp'
import { kbdPlugin } from '../plugins/kbd-plugin'
import { cppTemplateEscapePlugin } from '../plugins/escape-cpp-templates'
import { mermaidPlugin } from '../plugins/mermaid-plugin'
import { codeFoldPlugin } from '../plugins/code-fold-plugin'
import { getBuildInfo } from './build-info'

// 模块加载时算一次,两个 themeConfig 函数共用;同一构建进程内一致。
const buildInfo = getBuildInfo()

// 单一 markdown 配置来源：index.ts(dev/单体 build) 和 scripts/build.ts 分卷构建共用。
// 改 markdown 只改这一处，避免两份重复配置漏改——languageAlias 曾因此只改了 index.ts、
// 漏掉 build.ts 走的 sharedBase，导致分卷构建仍刷 Shiki 告警。
export const sharedMarkdown = {
  lineNumbers: true,
  math: true,
  // ld(GNU linker script)、nasm(NASM 汇编)不在 Shiki 默认 bundle，
  // 映射到近似语言，避免 "language not loaded, falling back to txt" 告警刷屏。
  languageAlias: {
    ld: 'c',
    nasm: 'asm',
  },
  theme: {
    light: 'github-light',
    dark: 'github-dark',
  },
  config(md) {
    cppTemplateEscapePlugin(md)
    md.use(kbdPlugin)
    md.use(mermaidPlugin)
    md.use(codeFoldPlugin)
  },
}

export const sharedBase = {
  base: '/Tutorial_AwesomeModernCPP/',
  cleanUrls: true,
  lastUpdated: true,

  // 分卷构建(scripts/build.ts)把 md 复制到 gitignored 的临时目录再交给 VitePress,
  // VitePress 默认对副本跑 `git log` 拿不到历史,"Last Updated" 渲染不出来。这里改用
  // documents/ 下真实源文件的提交时间覆盖 pageData.lastUpdated。详见 git-timestamp.ts。
  async transformPageData(pageData: PageData) {
    const ms = getGitTimestampMs(pageData.relativePath)
    if (ms) {
      pageData.lastUpdated = ms
    }
  },

  vite: {
    build: {
      chunkSizeWarningLimit: 5000,
    },
    assetsInclude: ['**/*.drawio'],
  },

  vue: {
    template: {
      compilerOptions: {
        isCustomElement: (tag: string) => tag.includes('-') || tag.includes('.'),
      },
    },
  },

  head: [
    ['link', { rel: 'icon', href: '/Tutorial_AwesomeModernCPP/favicon.ico' }],
    // 浏览器地址栏/壁纸融合(明暗双值,与站点暖中性底一致)
    ['meta', { name: 'theme-color', content: '#F7F3EC' }],
    ['meta', { name: 'theme-color', content: '#17120E', media: '(prefers-color-scheme: dark)' }],
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
}

const SEARCH_CACHE_DIR = join(fileURLToPath(new URL('./', import.meta.url)), '..', '.search-cache')
const SEARCH_CACHE_BUNDLE = join(SEARCH_CACHE_DIR, 'bundle.json')
// 渲染管线行为变化时(markdown 插件增删、vitepress 升级)手动递增,使旧缓存整体失效
const SEARCH_CACHE_VERSION = 'v1'

let searchRenderBundle: Record<string, string> | null = null
let searchRenderDirty = false
let searchRenderFlushTimer: ReturnType<typeof setTimeout> | null = null

function loadSearchRenderBundle(): Record<string, string> {
  if (searchRenderBundle === null) {
    try {
      searchRenderBundle = JSON.parse(readFileSync(SEARCH_CACHE_BUNDLE, 'utf8'))
    } catch {
      searchRenderBundle = {}
    }
  }
  return searchRenderBundle
}

function scheduleSearchRenderFlush() {
  searchRenderDirty = true
  if (searchRenderFlushTimer === null) {
    searchRenderFlushTimer = setTimeout(() => {
      searchRenderFlushTimer = null
      if (!searchRenderDirty) return
      searchRenderDirty = false
      try {
        mkdirSync(SEARCH_CACHE_DIR, { recursive: true })
        writeFileSync(SEARCH_CACHE_BUNDLE, JSON.stringify(loadSearchRenderBundle()))
      } catch {
        // 缓存写失败不影响索引构建,只是下次还得重渲染
      }
    }, 500)
  }
}

const localSearchOptions = {
  _render(src: string, env: Record<string, any>, md: any) {
    const key = createHash('sha1').update(SEARCH_CACHE_VERSION).update(src).digest('hex')
    const bundle = loadSearchRenderBundle()
    if (key in bundle) {
      return bundle[key]
    }
    const fast = process.env.npm_lifecycle_event === 'dev'
    const saved = md.options.highlight
    if (fast) md.options.highlight = undefined
    let html: string
    try {
      html = md.render(src, env)
    } finally {
      if (fast) md.options.highlight = saved
    }
    // 与 vitepress 默认行为对齐:frontmatter 声明 search: false 的页面不入索引
    bundle[key] = env.frontmatter?.search === false ? '' : html
    scheduleSearchRenderFlush()
    return bundle[key]
  },
}

// socialLinks 构造器:CN/EN 各指向自己语言的加群页(/community/join)。
// 企鹅图标来自 simple-icons 的 QQ 剪影(CC0)。socialLinks 渲染时不补 base 前缀,
// 站内路径必须写全(含 /Tutorial_AwesomeModernCPP 前缀)。
export function makeSocialLinks(joinLink: string): DefaultTheme.SocialLink[] {
  return [
    { icon: 'github', link: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP' },
    {
      icon: {
        svg: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M21.395 15.035a40 40 0 0 0-.803-2.264l-1.079-2.695c.001-.032.014-.562.014-.836C19.526 4.632 17.351 0 12 0S4.474 4.632 4.474 9.241c0 .274.013.804.014.836l-1.08 2.695a39 39 0 0 0-.802 2.264c-1.021 3.283-.69 4.643-.438 4.673.54.065 2.103-2.472 2.103-2.472c0 1.469.756 3.387 2.394 4.771c-.612.188-1.363.479-1.845.835c-.434.32-.379.646-.301.778c.343.578 5.883.369 7.482.189c1.6.18 7.14.389 7.483-.189c.078-.132.132-.458-.301-.778c-.483-.356-1.233-.646-1.846-.836c1.637-1.384 2.393-3.302 2.393-4.771c0 0 1.563 2.537 2.103 2.472c.251-.03.581-1.39-.438-4.673"/></svg>',
      },
      link: joinLink,
    },
  ]
}

export function sharedThemeConfig(): DefaultTheme.Config {
  return {
    nav: navZh,
    search: {
      provider: 'local',
      options: localSearchOptions,
    },
    editLink: {
      pattern: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/edit/main/documents/:path',
      text: '在 GitHub 上编辑此页',
    },
    footer: {
      message: `${buildInfo.version} · ${buildInfo.sha} · ${buildInfo.date}`,
      copyright: 'Copyright 2025-2026 Charliechen',
    },
    socialLinks: makeSocialLinks('/Tutorial_AwesomeModernCPP/community/join'),
  }
}

export function sharedEnThemeConfig(): DefaultTheme.Config {
  return {
    nav: navEn,
    search: {
      provider: 'local',
      options: localSearchOptions,
    },
    editLink: {
      pattern: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/edit/main/documents/en/:path',
      text: 'Edit this page on GitHub',
    },
    footer: {
      message: `${buildInfo.version} · ${buildInfo.sha} · ${buildInfo.date}`,
      copyright: 'Copyright 2025-2026 Charliechen',
    },
    socialLinks: makeSocialLinks('/Tutorial_AwesomeModernCPP/en/community/join'),
  }
}
