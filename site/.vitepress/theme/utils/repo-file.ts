// 按仓库路径取文件文本:本地站点资源 → GitHub raw 兜底。
// 与 OnlineCompilerDemo 的源码加载链同思路(dev 中间件/构建拷贝服务本地路径,
// 兜底保证任何环境都能拿到)。题目四件套(quiz.json/problem.md/…)都走这里。

import { withBase } from 'vitepress'

const RAW_BASE = 'https://raw.githubusercontent.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/main'

/** 周列表 manifest 的统一访问路径(dev 中间件现生成 / build 写进 dist) */
export const WEEKLY_MANIFEST_PATH = 'code/volumn_codes/weekly-problems/manifest.json'

export async function fetchRepoText(path: string): Promise<string> {
  const normalized = path.replace(/^\/+/, '')
  const local = await tryFetch(withBase(`/${normalized}`))
  if (local !== null) return local
  const raw = await tryFetch(`${RAW_BASE}/${normalized}`)
  if (raw !== null) return raw
  throw new Error(`无法读取 ${normalized}(本地与 GitHub raw 都失败)`)
}

async function tryFetch(url: string): Promise<string | null> {
  try {
    const response = await fetch(url)
    if (!response.ok) return null
    return await response.text()
  } catch {
    return null
  }
}
