import type { DefaultTheme } from 'vitepress'
import { navZh, navEn } from './nav'
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
