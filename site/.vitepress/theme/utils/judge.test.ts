// judge 判题内核单元测试(纯逻辑,不走网络)。
// payload 形状按 2026-09 实测 CE executor 响应构造:顶层 code = 程序退出码,
// 编译成败看 buildResult.code,超时标记是顶层 timedOut(无 execResult)。
// 真实 CE 链路属开发期人工验证,见 P2 Issue 验收方式。运行:pnpm test:judge

import test from 'node:test'
import assert from 'node:assert/strict'

import { parseExecutionResult } from './ce-api'
import {
  assembleAssertProgram,
  assertVerdictFrom,
  JUDGE_MARKER,
  normalizeIoOutput,
  parseJudgeLines,
  type AssertJudgeTest,
} from './judge'

// ── assembleAssertProgram ─────────────────────────────────────

test('assert 拼装:包含用户源码与生成的 main/断言', () => {
  const source = 'int count_coins(int total) { return 6; }'
  const tests: AssertJudgeTest[] = [
    { call: 'count_coins(15)', expected: '6' },
    { call: 'count_coins(0)', expected: '1' },
  ]
  const program = assembleAssertProgram(source, tests)

  assert.ok(program.includes('int count_coins(int total)'))
  assert.ok(program.includes('judge_detail::report(1, (count_coins(15)), (6));'))
  assert.ok(program.includes('judge_detail::report(2, (count_coins(0)), (1));'))
  assert.ok(program.includes('int main()'))
  assert.ok(program.indexOf('int count_coins') < program.indexOf('int main()'), '用户代码应在生成的 main 之前')
})

test('assert 拼装:JSON 转义用双反斜杠,生成的 C++ 字符串合法', () => {
  const program = assembleAssertProgram('int f() { return 1; }', [{ call: 'f()', expected: '1' }])
  // C++ 源码里应看到 "\\n"(两个字符:反斜杠+n)而不是裸换行转义
  assert.ok(program.includes('out += "\\\\n"'), '换行应转成字面 \\n 两个字符')
  assert.ok(program.includes('out += "\\\\\\\\"'), '反斜杠应转成字面 \\\\ 两个字符')
  assert.ok(program.includes('"\\\\u%04x"'), '控制字符用 \\uXXXX 格式')
})

// ── parseJudgeLines ───────────────────────────────────────────

test('判定行解析:只认前缀 JSON,噪音行与坏行跳过', () => {
  const stdout = [
    'hello from user',
    `${JUDGE_MARKER}${JSON.stringify({ case: 1, ok: true, got: '6', want: '6' })}`,
    `${JUDGE_MARKER}not-json`,
    'garbage',
    `${JUDGE_MARKER}${JSON.stringify({ case: 2, ok: false, got: '5', want: '6' })}`,
  ].join('\n')

  const lines = parseJudgeLines(stdout)
  assert.equal(lines.length, 2)
  assert.deepEqual(lines[0], { caseIndex: 1, ok: true, got: '6', want: '6' })
  assert.equal(lines[1].caseIndex, 2)
  assert.equal(lines[1].ok, false)
  assert.equal(lines[1].got, '5')
})

test('判定行解析:缺字段的对象不构成判定', () => {
  const lines = parseJudgeLines(`${JUDGE_MARKER}${JSON.stringify({ case: 1 })}\n${JUDGE_MARKER}${JSON.stringify({ ok: true })}`)
  assert.equal(lines.length, 0)
})

// ── normalizeIoOutput ─────────────────────────────────────────

test('io 规范化:去行尾空白与末尾空行,保留行内前导空白', () => {
  assert.equal(normalizeIoOutput('6\n'), '6')
  assert.equal(normalizeIoOutput('6  \n'), '6')
  assert.equal(normalizeIoOutput('6\r\n'), '6')
  assert.equal(normalizeIoOutput('a\nb\n\n\n'), 'a\nb')
  assert.equal(normalizeIoOutput('  a\nb'), '  a\nb')
  assert.equal(normalizeIoOutput(''), '')
})

// ── parseExecutionResult(ce-api)─────────────────────────────

test('CE 结果解析:正常执行的程序', () => {
  const payload = {
    code: 0,
    timedOut: false,
    stdout: [{ text: 'out' }],
    stderr: [] as { text: string }[],
    buildResult: { code: 0 },
  }
  const exec = parseExecutionResult(payload)
  assert.equal(exec.compileFailed, false)
  assert.equal(exec.stdout, 'out')
  assert.equal(exec.exitCode, 0)
  assert.equal(exec.timedOut, false)
})

test('CE 结果解析:编译失败给诊断', () => {
  const payload = {
    buildResult: { code: 1, stderr: [{ text: 'error: expected ;' }] },
    stdout: [],
    stderr: [] as { text: string }[],
  }
  const exec = parseExecutionResult(payload)
  assert.equal(exec.compileFailed, true)
  assert.ok(exec.diagnostics.includes('error: expected ;'))
})

test('回归(2026-09 实测):顶层 code 非零是运行期崩溃,不是编译失败', () => {
  // 程序 SIGABRT:顶层 code=134,但 buildResult.code=0(编译明明成功了)
  const payload = {
    code: 134,
    timedOut: false,
    stdout: [],
    stderr: [{ text: 'terminate called after throwing' }],
    buildResult: { code: 0 },
  }
  const exec = parseExecutionResult(payload)
  assert.equal(exec.compileFailed, false)
  assert.equal(exec.exitCode, 134)
  assert.ok(exec.stderr.includes('terminate'))
})

test('回归(2026-09 实测):编译诊断不能被顶层 "Build failed" 桩截胡', () => {
  // executor 编译失败:顶层 stderr 只有桩文本,真实 gcc 诊断在 buildResult.stderr
  const payload = {
    code: 1,
    timedOut: false,
    stdout: [],
    stderr: [{ text: 'Build failed' }],
    buildResult: {
      code: 1,
      stdout: [],
      stderr: [
        { text: 'example.cpp: In function \'int main()\':' },
        { text: "example.cpp:3:12: error: expected ';' before 'return'" },
      ],
    },
  }
  const exec = parseExecutionResult(payload)
  assert.equal(exec.compileFailed, true)
  assert.ok(exec.diagnostics.includes("error: expected ';'"), '诊断应包含真实 gcc 错误')
})

test('回归(2026-09 实测):SIGKILL 超时标记在顶层 timedOut', () => {
  const payload = {
    code: 9,
    timedOut: true,
    stdout: [],
    stderr: [{ text: 'Killed - processing time exceeded' }],
    buildResult: { code: 0 },
  }
  const exec = parseExecutionResult(payload)
  assert.equal(exec.timedOut, true)
  assert.equal(exec.compileFailed, false)
})

// ── assertVerdictFrom(状态机)────────────────────────────────

const judgeLine = (c: number, ok: boolean, got: string, want: string) => ({
  text: `${JUDGE_MARKER}${JSON.stringify({ case: c, ok, got, want })}`,
})

test('assert 判定:全过 → AC', () => {
  const payload = {
    code: 0,
    timedOut: false,
    stdout: [judgeLine(1, true, '6', '6'), judgeLine(2, true, '1', '1')],
    stderr: [],
    buildResult: { code: 0 },
  }
  const verdict = assertVerdictFrom(payload, [
    { call: 'count_coins(15)', expected: '6' },
    { call: 'count_coins(0)', expected: '1' },
  ])
  assert.equal(verdict.status, 'AC')
  assert.equal(verdict.cases.every(c => c.status === 'pass'), true)
  assert.equal(verdict.firstFailed, undefined)
})

test('assert 判定:有失败 → WA 且 firstFailed 指向首个错例', () => {
  const payload = {
    code: 0,
    timedOut: false,
    stdout: [judgeLine(1, true, '6', '6'), judgeLine(2, false, '5', '1')],
    stderr: [],
    buildResult: { code: 0 },
  }
  const verdict = assertVerdictFrom(payload, [
    { call: 'count_coins(15)', expected: '6' },
    { call: 'count_coins(0)', expected: '1' },
  ])
  assert.equal(verdict.status, 'WA')
  assert.equal(verdict.firstFailed, 2)
  assert.equal(verdict.cases[1].got, '5')
  assert.equal(verdict.cases[1].expected, '1')
})

test('assert 判定:编译失败 → CE', () => {
  const payload = {
    buildResult: { code: 1, stderr: [{ text: 'error: boom' }] },
    stdout: [],
    stderr: [],
  }
  const verdict = assertVerdictFrom(payload, [{ call: 'f()', expected: '1' }])
  assert.equal(verdict.status, 'CE')
  assert.ok(verdict.diagnostics!.includes('error: boom'))
  assert.equal(verdict.cases[0].status, 'pending')
})

test('assert 判定:运行崩溃 → RE 且保留已完成的用例(实测回归)', () => {
  const payload = {
    code: 134,
    timedOut: false,
    stdout: [judgeLine(1, true, '6', '6')],
    stderr: [{ text: 'terminate called after throwing' }],
    buildResult: { code: 0 },
  }
  const verdict = assertVerdictFrom(payload, [
    { call: 'count_coins(15)', expected: '6' },
    { call: 'count_coins(0)', expected: '1' },
  ])
  assert.equal(verdict.status, 'RE')
  assert.equal(verdict.cases[0].status, 'pass')
  assert.equal(verdict.cases[1].status, 'pending')
  assert.ok(verdict.diagnostics!.includes('terminate'))
})

test('assert 判定:超时 → TLE(实测回归)', () => {
  const payload = {
    code: 9,
    timedOut: true,
    stdout: [],
    stderr: [{ text: 'Killed - processing time exceeded' }],
    buildResult: { code: 0 },
  }
  const verdict = assertVerdictFrom(payload, [{ call: 'f()', expected: '1' }])
  assert.equal(verdict.status, 'TLE')
})

test('assert 判定:正常退出但零判定行 → ERROR', () => {
  const payload = {
    code: 0,
    timedOut: false,
    stdout: [{ text: 'nothing' }],
    stderr: [],
    buildResult: { code: 0 },
  }
  const verdict = assertVerdictFrom(payload, [{ call: 'f()', expected: '1' }])
  assert.equal(verdict.status, 'ERROR')
})
