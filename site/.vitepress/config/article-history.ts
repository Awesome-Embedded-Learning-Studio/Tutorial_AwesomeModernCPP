import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import type { ContributionHistory } from '../theme/utils/article-contributors'

const PROJECT_ROOT = fileURLToPath(new URL('../../../', import.meta.url))

// Verified against /team/, GitHub noreply addresses and repository remotes.
// Names alone are not assumed to be GitHub handles. Keep unknown authors as Git names.
const EMAIL_USERS: Record<string, string> = {
  '725610365@qq.com': 'Charliechen114514',
  '2413704818@qq.com': 'xiaoshuaijie',
  'yukunj.cs@gmail.com': 'YukunJ',
  'yukunj@andrew.cmu.edu': 'YukunJ',
  'tascierol32@gmail.com': 'Voyagerroc-Lab',
}

export function githubFromIdentity(email: string): string | undefined {
  const normalized = email.toLowerCase()
  return EMAIL_USERS[normalized] ?? normalized.match(/^(?:\d+\+)?([^@]+)@users\.noreply\.github\.com$/)?.[1]
}

export function parseArticleHistory(log: string): ContributionHistory[] {
  return log.split('\x1e').filter(record => record.trim()).map(record => {
    const [commit, date, author, email, subject, ...body] = record.trim().split('\x1f')
    const authors = [githubFromIdentity(email) ?? author]
    for (const match of body.join('\x1f').matchAll(/^Co-authored-by:\s*(.+?)\s*<([^>]+)>/gim)) {
      // Only identified GitHub contributors get a human attribution here; tool trailers are omitted.
      const github = githubFromIdentity(match[2])
      if (github && !authors.some(name => name.toLowerCase() === github.toLowerCase())) authors.push(github)
    }
    const pr = subject.match(/\(#(\d+)\)\s*$/)?.[1]
    return { commit, date, authors, note: subject, pr: pr ? Number(pr) : undefined }
  })
}

const cache = new Map<string, ContributionHistory[]>()

/** Real documents paths survive volume-copy builds. Follow renames, exclude merge-only authors. */
export function getArticleHistory(relativePath: string, projectRoot = PROJECT_ROOT): ContributionHistory[] {
  if (!relativePath || relativePath.startsWith('/') || relativePath.includes('\\')
    || relativePath.split('/').some(part => !part || part === '.' || part === '..')) {
    throw new Error(`[article-history] invalid document path: ${relativePath}`)
  }
  const key = `${projectRoot}\0${relativePath}`
  const hit = cache.get(key)
  if (hit) return hit
  let history: ContributionHistory[]
  try {
    const log = execFileSync('git', [
      'log', '--follow', '--no-merges', '--date=short',
      '--format=%x1e%H%x1f%ad%x1f%an%x1f%ae%x1f%s%x1f%b',
      '--', `documents/${relativePath}`,
    ], { cwd: projectRoot, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe'] })
    history = parseArticleHistory(log)
  } catch (error) {
    // An exported source archive has no Git history; do not invent dates or revisions.
    if ((error as { stderr?: Buffer }).stderr?.toString().includes('not a git repository')) history = []
    else throw error
  }
  cache.set(key, history)
  return history
}
