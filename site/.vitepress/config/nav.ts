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
      { text: '卷十 · 课程与演讲笔记', link: '/vol10-open-lecture-notes/' },
      { text: '编译与链接', link: '/compilation/' },
      { text: '实战项目', link: '/projects/' },
    ],
  },
  { text: '参考', link: '/cpp-reference/' },
  { text: '附录', link: '/appendix/' },
  { text: '社区', link: '/community/' },
  { text: '贡献者', link: '/team/' },
]

export const navEn: DefaultTheme.NavItem[] = [
  { text: 'Home', link: '/en/' },
  {
    text: 'Fundamentals',
    items: [
      { text: 'Vol.1 Fundamentals', link: '/en/vol1-fundamentals/' },
      { text: 'Vol.2 Modern Features', link: '/en/vol2-modern-features/' },
    ],
  },
  {
    text: 'Advanced',
    items: [
      { text: 'Vol.3 Standard Library', link: '/en/vol3-standard-library/' },
      { text: 'Vol.4 Advanced Topics', link: '/en/vol4-advanced/' },
    ],
  },
  {
    text: 'Engineering',
    items: [
      { text: 'Vol.5 Concurrency', link: '/en/vol5-concurrency/' },
      { text: 'Vol.6 Performance', link: '/en/vol6-performance/' },
      { text: 'Vol.7 Engineering', link: '/en/vol7-engineering/' },
    ],
  },
  {
    text: 'Domains',
    items: [
      { text: 'Vol.8 Domain Applications', link: '/en/vol8-domains/' },
      { text: 'Vol.9 Open Source Projects', link: '/en/vol9-open-source-project-learn/' },
      { text: 'Vol.10 Courses & Talks', link: '/en/vol10-open-lecture-notes/' },
      { text: 'Compilation & Linking', link: '/en/compilation/' },
      { text: 'Projects', link: '/en/projects/' },
    ],
  },
  { text: 'Reference', link: '/en/cpp-reference/' },
  { text: 'Appendix', link: '/en/appendix/' },
  { text: 'Team', link: '/en/team/' },
]
