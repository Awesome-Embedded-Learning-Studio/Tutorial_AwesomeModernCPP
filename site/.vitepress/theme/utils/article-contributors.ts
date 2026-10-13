// Shared types; UI labels and role names are formatted before reaching the browser.
export const CONTRIBUTION_ROLES = ['author', 'revision', 'translation', 'review', 'maintain'] as const
export type ContributionRole = typeof CONTRIBUTION_ROLES[number]
export type ContributionLocale = 'zh' | 'en'
export type ContributionText = string | Record<ContributionLocale, string>

export interface ContributionPerson {
  github: string
  name?: string
  roles: ContributionRole[]
  note?: ContributionText
  pr?: number
}

export interface ContributionHistory {
  authors: string[]
  note: string
  date: string
  commit: string
  pr?: number
}

export interface ArticleContributions {
  heading: string
  historyHeading: string
  historyHint: string
  historyEmpty: string
  profileLabel: string
  metaLabel: string
  metaCount: string
  signatureLink: string
  contributors: Array<{
    github: string; name: string; roles: string[]; note: string
    avatar: string; profile: string; anchor: string; tooltip: string; pr?: number; prUrl?: string
  }>
  history: Array<{ names: string; note: string; date: string; pr?: number; url?: string; commit: string; commitUrl: string }>
}

export const DEFAULT_CONTRIBUTOR: ContributionPerson = {
  github: 'Charliechen114514', roles: ['author', 'maintain'],
  note: { zh: '文章撰写与长期维护', en: 'Article writing and ongoing maintenance' },
}

const labels = {
  zh: {
    heading: '本文贡献者', historyHeading: '贡献与变更记录',
    historyHint: '来自 Git，按时间由近到远排列；提交说明保留原文。',
    historyEmpty: '本文尚无已提交的 Git 变更记录。', profileLabel: '访问 GitHub 主页',
    metaLabel: '本文贡献者', signatureLink: '查看署名',
    roles: { author: '撰写', revision: '修订', translation: '翻译', review: '审校', maintain: '维护' },
  },
  en: {
    heading: 'Contributors of This Article', historyHeading: 'Contribution history',
    historyHint: 'From Git, newest first. Commit messages keep their original wording.',
    historyEmpty: 'No committed Git history for this article yet.', profileLabel: 'Visit GitHub profile',
    metaLabel: 'Contributors', signatureLink: 'Credits',
    roles: { author: 'Author', revision: 'Revision', translation: 'Translation', review: 'Review', maintain: 'Maintain' },
  },
} as const

export const CONTRIBUTION_REPO = 'https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP'

export function formatArticleContributions(
  people: ContributionPerson[], history: ContributionHistory[], locale: ContributionLocale,
): ArticleContributions {
  const copy = labels[locale]
  const contributors = people.map(person => {
    const roles = CONTRIBUTION_ROLES.filter(role => person.roles.includes(role)).map(role => copy.roles[role])
    const name = person.name ?? person.github
    return {
      github: person.github, name, roles,
      note: typeof person.note === 'string' ? person.note : person.note?.[locale] ?? '',
      avatar: `https://github.com/${person.github}.png?size=96`,
      profile: `https://github.com/${person.github}`,
      anchor: `card-contributor-${person.github.toLowerCase()}`,
      tooltip: `${name} · ${roles.join(' · ')}`,
      pr: person.pr, prUrl: person.pr ? `${CONTRIBUTION_REPO}/pull/${person.pr}` : undefined,
    }
  })
  const names = new Map(contributors.map(person => [person.github.toLowerCase(), person.name]))
  return {
    heading: copy.heading, historyHeading: copy.historyHeading,
    historyHint: copy.historyHint, historyEmpty: copy.historyEmpty, profileLabel: copy.profileLabel,
    metaLabel: copy.metaLabel, signatureLink: copy.signatureLink,
    metaCount: locale === 'zh' ? `${people.length} 人` : String(people.length),
    contributors,
    history: history.map(entry => ({
      names: entry.authors.map(author => names.get(author.toLowerCase()) ?? author).join(' · '),
      note: entry.note, date: entry.date, pr: entry.pr,
      url: entry.pr ? `${CONTRIBUTION_REPO}/pull/${entry.pr}` : undefined,
      commit: entry.commit.slice(0, 7), commitUrl: `${CONTRIBUTION_REPO}/commit/${entry.commit}`,
    })),
  }
}
