import { execFileSync } from 'node:child_process'
import { existsSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'

// 本文件恒定位于 site/.vitepress/config/,上推三级 = 项目根。
// 用 import.meta.url 定位,分卷构建时本文件仍从真实位置加载(不被复制),路径稳定。
const HERE = fileURLToPath(new URL('./', import.meta.url))
const DOCUMENTS = join(HERE, '..', '..', '..', 'documents')

const cache = new Map<string, number | undefined>()

/**
 * 返回某 markdown 源文件最后的 git 提交时间(毫秒),供 VitePress 的
 * `transformPageData` 覆盖 `pageData.lastUpdated` 用。
 *
 * 为什么需要它:`scripts/build.ts` 分卷构建会把 `documents/` 下的 md 复制到
 * `site/.vitepress/.build-tmp/`(gitignored) 再交给 VitePress 构建。VitePress 的
 * `lastUpdated` 默认对构建源文件跑 `git log` 取时间,但这些副本不在 git index 里、
 * 拿不到任何历史(实测 `git log` 对副本返回空),导致线上 "Last Updated" 一片空白。
 *
 * 解法:`transformPageData` 拿到的 `pageData.relativePath` 反映的是文件在 documents/
 * 树里的相对位置——分卷副本原样保留了目录结构,EN 副本也带 `en/` 前缀——所以直接用
 * 它在真实的 `documents/` 下定位源文件、对源文件跑 `git log`,把毫秒级时间戳喂回
 * `pageData.lastUpdated`,绕开副本丢历史的问题。dev / 单体构建(srcDir 指向 documents/)
 * 下查的也是同一份真实文件,行为与 VitePress 默认一致。
 *
 * 单位对齐:VitePress 内部 `getGitTimestamp` 用 `+new Date(git log --pretty=%ai)`,
 * 结果是毫秒;这里用 `git log --format=%at`(秒) × 1000 对齐,避免显示成 1970 年。
 */
export function getGitTimestampMs(relativePath: string): number | undefined {
  if (!relativePath) return undefined
  if (cache.has(relativePath)) return cache.get(relativePath)

  const realPath = join(DOCUMENTS, relativePath)
  let result: number | undefined
  if (existsSync(realPath)) {
    try {
      const out = execFileSync(
        'git',
        ['log', '-1', '--format=%at', '--', realPath],
        { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] },
      ).trim()
      const sec = out ? parseInt(out, 10) : NaN
      result = Number.isFinite(sec) ? sec * 1000 : undefined
    } catch {
      result = undefined // 非 git 仓库 / 无历史 → 回退,交由 VitePress 默认行为
    }
  }
  cache.set(relativePath, result)
  return result
}
