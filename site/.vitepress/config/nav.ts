import type { DefaultTheme } from 'vitepress'

export const navZh: DefaultTheme.NavItem[] = [
  { text: '首页', link: '/' },
  {
    text: '基础与特性',
    items: [
      { text: '卷一 · 基础入门', link: '/vol1-fundamentals/' },
      { text: '卷二 · 现代特性', link: '/vol2-modern-features/' },
    ],
  },
  {
    text: '标准库与高级',
    items: [
      { text: '卷三 · 标准库深入', link: '/vol3-standard-library/' },
      { text: '卷四 · 高级主题', link: '/vol4-advanced/' },
    ],
  },
  {
    text: '工程实践',
    items: [
      { text: '卷五 · 并发编程', link: '/vol5-concurrency/' },
      { text: '卷六 · 性能优化', link: '/vol6-performance/' },
      { text: '卷七 · 工程实践', link: '/vol7-engineering/' },
    ],
  },
  {
    text: '领域实战',
    items: [
      { text: '卷八 · 领域应用', link: '/vol8-domains/' },
      { text: '卷九 · 开源项目学习', link: '/vol9-open-source-project-learn/' },
      { text: '编译与链接', link: '/compilation/' },
      { text: '实战项目', link: '/projects/' },
    ],
  },
  { text: '参考', link: '/cpp-reference/' },
  { text: '附录', link: '/appendix/' },
]

export const navEn: DefaultTheme.NavItem[] = [
  { text: 'Home', link: '/en/' },
  {
    text: 'Fundamentals',
    items: [
      { text: 'Vol.1 Fundamentals', link: '/en/vol1-fundamentals/' },
    ],
  },
]
