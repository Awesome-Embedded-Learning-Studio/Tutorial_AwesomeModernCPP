import type { DefaultTheme } from 'vitepress'
import { navZh, navEn } from './nav'
import { kbdPlugin } from '../plugins/kbd-plugin'
import { cppTemplateEscapePlugin } from '../plugins/escape-cpp-templates'

export const sharedBase = {
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
      message: '基于 VitePress 构建',
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
      message: 'Built with VitePress',
      copyright: 'Copyright 2025-2026 Charliechen',
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP' },
    ],
  }
}
