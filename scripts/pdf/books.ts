import type { BookDefinition, BookLanguage, BookLocale, ContentUnit } from './model'

export const CONTENT_UNITS: readonly ContentUnit[] = [
  { id: 'getting-started', sourceDir: 'getting-started', urlPrefix: '/getting-started' },
  { id: 'vol1', sourceDir: 'vol1-fundamentals', urlPrefix: '/vol1-fundamentals' },
  { id: 'vol2', sourceDir: 'vol2-modern-features', urlPrefix: '/vol2-modern-features' },
  { id: 'vol3', sourceDir: 'vol3-standard-library', urlPrefix: '/vol3-standard-library' },
  { id: 'vol4', sourceDir: 'vol4-advanced', urlPrefix: '/vol4-advanced' },
  { id: 'vol5', sourceDir: 'vol5-concurrency', urlPrefix: '/vol5-concurrency' },
  { id: 'vol6', sourceDir: 'vol6-performance', urlPrefix: '/vol6-performance' },
  { id: 'vol7', sourceDir: 'vol7-engineering', urlPrefix: '/vol7-engineering' },
  { id: 'vol8', sourceDir: 'vol8-domains', urlPrefix: '/vol8-domains' },
  { id: 'vol9', sourceDir: 'vol9-open-source-project-learn', urlPrefix: '/vol9-open-source-project-learn' },
  { id: 'vol10', sourceDir: 'vol10-open-lecture-notes', urlPrefix: '/vol10-open-lecture-notes' },
  { id: 'compilation', sourceDir: 'compilation', urlPrefix: '/compilation' },
  { id: 'cpp-reference', sourceDir: 'cpp-reference', urlPrefix: '/cpp-reference' },
  { id: 'projects', sourceDir: 'projects', urlPrefix: '/projects' },
  { id: 'community', sourceDir: 'community', urlPrefix: '/community' },
  { id: 'roadmap', sourceDir: 'roadmap', urlPrefix: '/roadmap' },
  { id: 'appendix', sourceDir: 'appendix', urlPrefix: '/appendix' },
  { id: 'team', sourceDir: 'team', urlPrefix: '/team' },
] as const

export const BOOKS: readonly BookDefinition[] = [
  { id: 'getting-started', title: { zh: '从这里开始', en: 'Getting Started' }, label: { zh: '入门指南', en: 'Getting Started' }, units: ['getting-started'] },
  { id: 'vol1', title: { zh: 'C++ 基础入门', en: 'C++ Fundamentals' }, label: { zh: '卷一', en: 'Volume 1' }, units: ['vol1'] },
  { id: 'vol2', title: { zh: '现代 C++ 核心特性', en: 'Core Modern C++ Features' }, label: { zh: '卷二', en: 'Volume 2' }, units: ['vol2'] },
  { id: 'vol3', title: { zh: '标准库', en: 'The Standard Library' }, label: { zh: '卷三', en: 'Volume 3' }, units: ['vol3'] },
  { id: 'vol4', title: { zh: '高级主题', en: 'Advanced Topics' }, label: { zh: '卷四', en: 'Volume 4' }, units: ['vol4'] },
  { id: 'vol5', title: { zh: '并发编程', en: 'Concurrency' }, label: { zh: '卷五', en: 'Volume 5' }, units: ['vol5'] },
  { id: 'vol6', title: { zh: '性能工程', en: 'Performance Engineering' }, label: { zh: '卷六', en: 'Volume 6' }, units: ['vol6'] },
  { id: 'vol7', title: { zh: '工程实践', en: 'Engineering Practice' }, label: { zh: '卷七', en: 'Volume 7' }, units: ['vol7'] },
  { id: 'vol8', title: { zh: '领域实践', en: 'Domain Practice' }, label: { zh: '卷八', en: 'Volume 8' }, units: ['vol8'] },
  { id: 'vol9', title: { zh: '开源项目研读', en: 'Open-source Project Studies' }, label: { zh: '卷九', en: 'Volume 9' }, units: ['vol9'] },
  { id: 'vol10', title: { zh: '课程与演讲笔记', en: 'Course and Talk Notes' }, label: { zh: '卷十', en: 'Volume 10' }, units: ['vol10'] },
  { id: 'compilation', title: { zh: '编译、链接与构建系统', en: 'Compilation, Linking, and Build Systems' }, label: { zh: '专题册', en: 'Special Edition' }, units: ['compilation'] },
  { id: 'cpp-reference', title: { zh: 'Modern C++ 速查手册', en: 'Modern C++ Quick Reference' }, label: { zh: '参考册', en: 'Reference' }, units: ['cpp-reference'] },
  { id: 'supplement', title: { zh: '项目、社区与附录', en: 'Projects, Community, and Appendices' }, label: { zh: '附录合辑', en: 'Supplement' }, units: ['projects', 'community', 'roadmap', 'appendix', 'team'] },
] as const

const localeStrings = {
  zh: {
    htmlLang: 'zh-CN',
    sourcePrefix: '',
    onlinePrefix: 'https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP',
    strings: {
      contents: '目录', generated: '生成日期', sourceRevision: '源码版本', onlineEdition: '在线版',
      references: '参考资料', lectureResources: '讲座资料', sourceCode: '示例源码', armSourceCode: 'ARM 示例源码',
    },
  },
  en: {
    htmlLang: 'en',
    sourcePrefix: 'en',
    onlinePrefix: 'https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP',
    strings: {
      contents: 'Contents', generated: 'Generated', sourceRevision: 'Source revision', onlineEdition: 'Online edition',
      references: 'References', lectureResources: 'Talk resources', sourceCode: 'Example source', armSourceCode: 'ARM example source',
    },
  },
} as const

export function getBook(bookId: string): BookDefinition {
  const book = BOOKS.find((candidate) => candidate.id === bookId)
  if (!book) {
    throw new Error(`Unknown book "${bookId}". Expected one of: ${BOOKS.map(({ id }) => id).join(', ')}`)
  }
  return book
}

export function getUnit(unitId: string): ContentUnit {
  const unit = CONTENT_UNITS.find((candidate) => candidate.id === unitId)
  if (!unit) throw new Error(`Unknown content unit "${unitId}"`)
  return unit
}

export function getLocale(language: BookLanguage): BookLocale {
  const selected = localeStrings[language]
  return { language, ...selected }
}
