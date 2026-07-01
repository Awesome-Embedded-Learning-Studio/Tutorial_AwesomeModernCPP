import type { DefaultTheme, PageData } from 'vitepress'
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
}

export function sharedThemeConfig(): DefaultTheme.Config {
  return {
    nav: navZh,
    search: {
      provider: 'local',
    },
    editLink: {
      pattern: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/edit/main/documents/:path',
      text: '在 GitHub 上编辑此页',
    },
    footer: {
      message: `${buildInfo.version} · ${buildInfo.sha} · ${buildInfo.date}`,
      copyright: 'Copyright 2025-2026 Charliechen',
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP' },
    ],
  }
}

export function sharedEnThemeConfig(): DefaultTheme.Config {
  return {
    nav: navEn,
    search: {
      provider: 'local',
    },
    editLink: {
      pattern: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/edit/main/documents/en/:path',
      text: 'Edit this page on GitHub',
    },
    footer: {
      message: `${buildInfo.version} · ${buildInfo.sha} · ${buildInfo.date}`,
      copyright: 'Copyright 2025-2026 Charliechen',
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP' },
    ],
  }
}
