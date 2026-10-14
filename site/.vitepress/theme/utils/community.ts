export const COMMUNITY_CATEGORIES = {
  'learning-notes': { zh: '学习笔记', en: 'Learning notes' },
  engineering: { zh: '工程经验', en: 'Engineering' },
  'source-reading': { zh: '源码阅读', en: 'Source reading' },
  'qa': { zh: '问答整理', en: 'Q&A' },
} as const

export type CommunityCategory = keyof typeof COMMUNITY_CATEGORIES
export type CommunityStatus = 'incoming' | 'articles'

export interface CommunityArticle {
  url: string
  title: string
  description: string
  category: CommunityCategory
  status: CommunityStatus
  authors: Array<{ github: string; name: string; avatar: string }>
  publishedAt?: string
  readingTime?: number
}

export function communityArticleStatus(path: string): CommunityStatus | undefined {
  const match = path.match(/^(?:en\/)?community\/(incoming|articles)\/(.+)\.md$/)
  if (!match || match[2].split('/').at(-1) === 'index') return undefined
  return match[1] as CommunityStatus
}
