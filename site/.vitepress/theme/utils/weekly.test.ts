import assert from 'node:assert/strict'
import test from 'node:test'
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, sep } from 'node:path'
import { outputDifference } from './output-difference'
import { issueTitle, problemAnchor, resumeProblem, type Week } from './weekly'
import { applyWeeklyPageData } from '../../config/weekly-manifest'
import type { PageData } from 'vitepress'

const week: Week = {
  slug: 'week-01', title: 'Week 1 · 递归三连', problems: ['01-one', '02-two', '03-three'].map(title => ({
    src: `code/weekly-problems/week-01/${title}`, title, type: 'reveal', stars: 1,
  })),
}
const [first, second, third] = week.problems
const record = (status: 'passed' | 'revealed' | 'attempted') => ({ status, hintsUsed: 0, updatedAt: 1 })

test('首次进入从第一题开始,空题包不产生无效链接', () => {
  assert.equal(resumeProblem(week, {}, new Set()), first)
  assert.equal(resumeProblem({ ...week, problems: [] }, {}, new Set()), undefined)
})
test('继续入口优先恢复未通过题目的草稿,不退回已通过题目', () => {
  assert.equal(resumeProblem(week, { [first.src]: record('passed') }, new Set([first.src, third.src])), third)
  assert.equal(resumeProblem(week, { [second.src]: record('attempted') }, new Set()), second)
})
test('已看题解仍可继续,全部通过后回顾第一题', () => {
  assert.equal(resumeProblem(week, { [first.src]: record('revealed') }, new Set()), first)
  assert.equal(resumeProblem(week, Object.fromEntries(week.problems.map(p => [p.src, record('passed')])), new Set()), first)
})
test('深链接不随题目标题或顺序变化', () => {
  assert.equal(problemAnchor(second.src), 'problem-02-two')
  assert.equal(issueTitle(week), '递归三连')
})
test('输出对照只突出不同片段,保留相同首尾', () => {
  assert.deepEqual(outputDifference('hello 123!', 'hello 456!'), [
    { text: 'hello ', changed: false }, { text: '456', changed: true }, { text: '!', changed: false },
  ])
  assert.deepEqual(outputDifference('same', 'same'), [{ text: 'same', changed: false }])
})
test('输出对照支持空输出、缺失内容、换行和 Unicode', () => {
  assert.deepEqual(outputDifference('a', ''), [{ text: '(空输出)', changed: true }])
  assert.equal(outputDifference('abc', 'ac').find(p => p.changed)?.text, '〔缺少内容〕')
  assert.equal(outputDifference('a\nb', 'a\nc').find(p => p.changed)?.text, 'c')
  assert.equal(outputDifference('🙂x', '🙂y').find(p => !p.changed)?.text, '🙂')
})
test('周页面和目录共享构建元数据,普通文档不受影响', () => {
  const root = mkdtempSync(join(tmpdir(), 'weekly-page-data-'))
  try {
    const docs = join(root, 'documents', 'weekly-problems')
    const code = join(root, 'code', 'volumn_codes', 'weekly-problems', 'week-01', '01-test')
    mkdirSync(docs, { recursive: true })
    mkdirSync(code, { recursive: true })
    const weeklyPage = '---\ntitle: "Week 1 · 练习"\ndescription: "测试描述"\ndateRange: "2026-09-14 ~ 09-20"\nweeklyThanks:\n  - github: owollz4\n    role: 题目提供者\n---\n'
    writeFileSync(join(docs, 'week-01.md'), weeklyPage)
    writeFileSync(join(code, 'quiz.json'), JSON.stringify({ type: 'reveal', title: '思考题' }))
    const page = { relativePath: 'weekly-problems/week-01.md', frontmatter: {} } as PageData
    applyWeeklyPageData(page, root)
    assert.equal(page.frontmatter.sidebar, false)
    assert.equal(page.frontmatter.aside, false)
    assert.equal(page.frontmatter.weeklyIssue.description, '测试描述')
    assert.equal(page.frontmatter.weeklyIssue.problems.length, 1)
    const archive = { relativePath: 'weekly-problems/index.md', frontmatter: {} } as PageData
    applyWeeklyPageData(archive, root)
    assert.equal(archive.frontmatter.weeklyArchive.length, 1)
    assert.deepEqual(page.frontmatter.weeklyIssue.weeklyThanks, [{ github: 'owollz4', role: '题目提供者' }])
    assert.deepEqual(archive.frontmatter.weeklyArchive[0].weeklyThanks, page.frontmatter.weeklyIssue.weeklyThanks)
    const ordinary = { relativePath: 'vol1-fundamentals/index.md', frontmatter: {} } as PageData
    applyWeeklyPageData(ordinary, root)
    assert.deepEqual(ordinary.frontmatter, {})
    writeFileSync(join(docs, 'week-01.md'), weeklyPage.replace('github: owollz4', 'github: https://github.com/owollz4'))
    assert.throws(() => applyWeeklyPageData(page, root), /weeklyThanks/)
    writeFileSync(join(docs, 'week-01.md'), weeklyPage)
    writeFileSync(join(code, 'quiz.json'), JSON.stringify({ type: 'not-a-type' }))
    assert.throws(() => applyWeeklyPageData(page, root), /type/)
  } finally {
    assert.ok(resolve(root).startsWith(resolve(tmpdir()) + sep))
    rmSync(root, { recursive: true, force: true })
  }
})
