import { defineConfig } from 'vitepress'
import { navZh, navEn } from './nav'
import { buildSidebar } from './sidebar'
import { kbdPlugin } from '../plugins/kbd-plugin'

export default defineConfig({
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
  ],

  markdown: {
    lineNumbers: true,
    math: true,
    theme: {
      light: 'github-light',
      dark: 'github-dark',
    },
    config(md) {
      md.use(kbdPlugin)
    },
  },

  themeConfig: {
    nav: navZh,
    sidebar: buildSidebar(),

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
  },
})
