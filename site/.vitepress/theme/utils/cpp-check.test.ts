import assert from 'node:assert/strict'
import test from 'node:test'
import { checkCppSource, parseCodeCheck } from './cpp-check'
import { judge, JudgeCooldownError } from './judge'

test('编译检查解析用户源码诊断,不把系统头文件的位置标到编辑器', () => {
  const result = parseCodeCheck({ code: 1, stderr: [
    { text: '<source>:12:5: error: unknown name', tag: { file: '<source>', line: 12, column: 5 } },
    { text: '/usr/include/vector:9:2: error: header context', tag: { file: '/usr/include/vector', line: 9, column: 2 } },
    { text: 'example.cpp:4:3: warning: unused variable' },
  ] })
  assert.equal(result.ok, false)
  assert.deepEqual(result.diagnostics, [
    { line: 12, column: 5, severity: 'error', message: 'unknown name' },
    { line: 4, column: 3, severity: 'warning', message: 'unused variable' },
  ])
  assert.match(result.text, /header context/)
})

test('编译检查保留成功时的警告,拒绝损坏的协议', () => {
  assert.equal(parseCodeCheck({ code: 0, stderr: [{ text: '<source>:2:1: warning: unused' }] }).diagnostics[0].severity, 'warning')
  assert.deepEqual(parseCodeCheck({ code: 0 }), { ok: true, text: '', diagnostics: [] })
  assert.throws(() => parseCodeCheck({ stdout: [] }), /有效的检查结果/)
  assert.throws(() => parseCodeCheck(null), /有效的检查结果/)
})

test('检查只编译当前源码且不执行,并与判题共享冷却', async t => {
  t.mock.method(Date, 'now', () => 100000)
  let calls = 0
  t.mock.method(globalThis, 'fetch', async (_url: unknown, init: RequestInit) => {
    calls++
    const body = JSON.parse(String(init.body))
    assert.equal(body.source, '// current source')
    assert.equal(body.options.compilerOptions.executorRequest, false)
    assert.equal(body.options.filters.execute, false)
    assert.match(body.options.userArguments, /-fsyntax-only/)
    assert.match(body.options.userArguments, /-Wall -Wextra/)
    return new Response(JSON.stringify({ code: 0 }), { status: 200 })
  })
  assert.equal((await checkCppSource('// current source')).ok, true)
  await assert.rejects(checkCppSource('// current source'), JudgeCooldownError)
  await assert.rejects(judge({ source: '', mode: 'io', tests: [{ stdin: '', expected: '' }] }), JudgeCooldownError)
  assert.equal(calls, 1)
})
