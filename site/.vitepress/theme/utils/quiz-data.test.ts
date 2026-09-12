// quiz-data(quiz.json 解析与校验)单元测试。运行:pnpm test:judge

import test from 'node:test'
import assert from 'node:assert/strict'

import { parseAssertTests, parseIoTests, parseQuizConfig, QUIZ_TYPES } from './quiz-data'

// ── 断言测例解析 ─────────────────────────────────────────────

test('parseAssertTests:按最后一个 == 切分', () => {
  assert.deepEqual(parseAssertTests(['count_coins(15) == 6']), [{ call: 'count_coins(15)', expected: '6' }])
  assert.deepEqual(parseAssertTests(['f(a == b) == c']), [{ call: 'f(a == b)', expected: 'c' }])
  assert.throws(() => parseAssertTests(['没有等号的断言']), /断言语法/)
  assert.throws(() => parseAssertTests(['  ', '']), /至少要有一条断言/)
})

test('parseIoTests:in/out 对象数组', () => {
  assert.deepEqual(
    parseIoTests([{ in: '2 3', out: '5' }, { in: '-7 4', out: '-3' }]),
    [
      { stdin: '2 3', expected: '5' },
      { stdin: '-7 4', expected: '-3' },
    ],
  )
  assert.throws(() => parseIoTests([]), /至少要有一组/)
  assert.throws(() => parseIoTests([{ in: 'x' } as never]), /in\/out 都必须是字符串/)
})

// ── quiz.json 校验 ───────────────────────────────────────────

test('合法的 judge-assert 配置', () => {
  const cfg = parseQuizConfig({
    type: 'judge-assert',
    title: '数硬币',
    stars: 3,
    hints: ['提示一', '提示二'],
    tests: ['count_coins(15) == 6', 'count_coins(0) == 1'],
  })
  assert.equal(cfg.type, 'judge-assert')
  assert.equal(cfg.mode, 'assert')
  assert.equal(cfg.stars, 3)
  assert.equal(cfg.hints.length, 2)
  assert.deepEqual(cfg.tests, [
    { call: 'count_coins(15)', expected: '6' },
    { call: 'count_coins(0)', expected: '1' },
  ])
})

test('stars 越界回退为 3;hints 缺省为空数组', () => {
  const cfg = parseQuizConfig({ type: 'reveal', title: 'T' })
  assert.equal(cfg.stars, 3)
  assert.deepEqual(cfg.hints, [])
})

test('未知题型 → 报错并列出可选项', () => {
  assert.throws(() => parseQuizConfig({ type: 'nope', title: 'T' }), new RegExp(QUIZ_TYPES.join(' / ')))
})

test('判题类缺 tests → 报错', () => {
  assert.throws(() => parseQuizConfig({ type: 'judge-io', title: 'T' }), /必须有 tests 数组/)
})

test('choice:answer 行号越界 → 报错', () => {
  assert.throws(
    () => parseQuizConfig({ type: 'choice', title: 'T', options: ['甲', '乙'], answer: [3] }),
    /1-based 行号数组/,
  )
})

test('fill:缺 answerText → 报错', () => {
  assert.throws(() => parseQuizConfig({ type: 'fill', title: 'T' }), /answerText/)
})

test('find-bug:bugLines 必填', () => {
  assert.throws(() => parseQuizConfig({ type: 'find-bug', title: 'T' }), /bugLines/)
  const cfg = parseQuizConfig({ type: 'find-bug', title: 'T', bugLines: [10, 11] })
  assert.deepEqual(cfg.bugLines, [10, 11])
})

test('非法 JSON 形状 → 报错', () => {
  assert.throws(() => parseQuizConfig(null), /不是合法的 JSON 对象/)
  assert.throws(() => parseQuizConfig({ type: 'fill' }), /title/)
})
