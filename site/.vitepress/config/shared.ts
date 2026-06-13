import type { DefaultTheme } from 'vitepress'
import { navZh, navEn } from './nav'
import { kbdPlugin } from '../plugins/kbd-plugin'
import { cppTemplateEscapePlugin } from '../plugins/escape-cpp-templates'
import { mermaidPlugin } from '../plugins/mermaid-plugin'
import { getBuildInfo } from './build-info'

// 模块加载时算一次,两个 themeConfig 函数共用;同一构建进程内一致。
const buildInfo = getBuildInfo()

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
  ],

  markdown: {
    lineNumbers: true,
    math: true,
    theme: {
      light: 'github-light',
      dark: 'github-dark',
    },
    config(md) {
      cppTemplateEscapePlugin(md)
      md.use(kbdPlugin)
      md.use(mermaidPlugin)
    },
  },
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
