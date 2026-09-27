// 判题内核(judge)——「每周一些题」P2。
// 纯函数库,无 UI:把「用户代码 + 测例」拼成完整程序,经 CE(Compiler Explorer)
// 编译执行,把输出解析成逐测例判定与最终 verdict。答题卡组件(P3)只管调 judge() 画界面。
//
// 只服务两类题型:函数式(assert,系统生成 main + 断言)/ 程序式(io,stdin→stdout 比对)。
// 口答/选择/找bug/展开式四类题型是纯前端交互,不经过这里。

import { parseExecutionResult, requestCeCompile } from './ce-api'

/** 生成 main 打印的判定行前缀——前端只认这个前缀,用户程序打印什么都不会干扰判定 */
export const JUDGE_MARKER = '##JUDGE##'
/** 判题冷却:两次 judge() 之间至少间隔(模块级共享),防止连点打爆 CE 免费额度 */
export const JUDGE_COOLDOWN_MS = 3000
/** 判题默认环境:做题环境不比教程旧(站子教到 C++23),题目可用 frontmatter 覆写 */
export const DEFAULT_JUDGE_COMPILER = 'g153'
export const DEFAULT_JUDGE_OPTIONS = '-O2 -std=c++23'

// ── 测例协议 ──────────────────────────────────────────────────

/** assert 模式测例:用户代码只写函数,系统生成 main 断言 */
export interface AssertJudgeTest {
  /** C++ 表达式,求值结果与 expected 用 == 比较,如 "count_coins(15)" */
  call: string
  /** C++ 表达式(通常是字面量),期望值,如 "6";失败时也用于展示 */
  expected: string
  label?: string
}

/** io 模式测例:用户写完整程序,逐用例喂 stdin 比对 stdout */
export interface IoJudgeTest {
  stdin: string
  expected: string
  label?: string
}

export type JudgeMode = 'assert' | 'io'

export type JudgeStatus = 'AC' | 'WA' | 'CE' | 'RE' | 'TLE' | 'ERROR'

export interface JudgeCaseResult {
  /** 1-based */
  index: number
  label?: string
  status: 'pending' | 'running' | 'pass' | 'fail'
  /** io 模式:程序实际输出;assert 模式:断言实际值 */
  got?: string
  /** 期望值(展示用) */
  expected?: string
}

export interface JudgeVerdict {
  status: JudgeStatus
  cases: JudgeCaseResult[]
  /** 首个失败用例(1-based),无失败则 undefined */
  firstFailed?: number
  /** CE 诊断 / RE 的 stderr / 其他说明 */
  diagnostics?: string
}

export interface JudgeRequest {
  /** 用户代码(assert:声明+函数实现,不能自带 main;io:完整程序) */
  source: string
  mode: JudgeMode
  tests: AssertJudgeTest[] | IoJudgeTest[]
  compiler?: string
  options?: string
}

export interface JudgeProgress {
  /** 正在/刚完成的用例序号(1-based) */
  running: number
  total: number
  result: JudgeCaseResult
}

export interface JudgeOptions {
  onProgress?: (progress: JudgeProgress) => void
  signal?: AbortSignal
}

export class JudgeCooldownError extends Error {
  readonly cooldownRemaining: number
  constructor(remainingMs: number) {
    super(`判题冷却中,请约 ${Math.ceil(remainingMs / 1000)} 秒后再试`)
    this.name = 'JudgeCooldownError'
    this.cooldownRemaining = remainingMs
  }
}

// ── 判题冷却(模块级:整页多张答题卡共用一个节奏)──────────────

let lastJudgeAt = 0

export function judgeCooldownRemaining(now: number = Date.now()): number {
  return Math.max(0, lastJudgeAt + JUDGE_COOLDOWN_MS - now)
}

/** 判题与手动编译检查共享请求节奏。 */
export function reserveCompilerRequest(): void {
  const remaining = judgeCooldownRemaining()
  if (remaining > 0) throw new JudgeCooldownError(remaining)
  lastJudgeAt = Date.now()
}

// ── 拼装:assert 模式 ──────────────────────────────────────────

// 用户源码在上、生成的 judge_detail + main 在下。判定行是 ${JUDGE_MARKER} 前缀的
// JSON(值经 JSON 转义),用户输出无法伪造结构;String.raw 保证 C++ 转义序列原样落盘。
const ASSERT_HARNESS = String.raw`

// ─── 以下由判题系统生成 ───
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

namespace judge_detail {

std::string escape_json(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char raw : s) {
    const auto c = static_cast<unsigned char>(raw);
    switch (raw) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
          out += buf;
        } else {
          out += raw;
        }
    }
  }
  return out;
}

template <class T>
std::string to_str(const T& value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

template <class T, class U>
void report(int index, const T& got, const U& want) {
  const bool ok = (got == want);
  std::cout << "` + JUDGE_MARKER + String.raw`{\"case\":" << index
            << ",\"ok\":" << (ok ? "true" : "false")
            << ",\"got\":\"" << judge_detail::escape_json(judge_detail::to_str(got))
            << "\",\"want\":\"" << judge_detail::escape_json(judge_detail::to_str(want))
            << "\"}\n";
}

}  // namespace judge_detail
`

export function assembleAssertProgram(source: string, tests: AssertJudgeTest[]): string {
  const checks = tests
    .map((test, i) => `  judge_detail::report(${i + 1}, (${test.call}), (${test.expected}));`)
    .join('\n')

  return `${source}${ASSERT_HARNESS}
int main() {
${checks}
  return 0;
}
`
}

// ── 判定行解析(纯函数)───────────────────────────────────────

export interface JudgeLineResult {
  caseIndex: number
  ok: boolean
  got?: string
  want?: string
}

// 只认 ${JUDGE_MARKER} 前缀的 JSON 行;坏行/噪音行一律跳过
export function parseJudgeLines(stdout: string): JudgeLineResult[] {
  const results: JudgeLineResult[] = []
  for (const line of stdout.split('\n')) {
    const at = line.indexOf(JUDGE_MARKER)
    if (at === -1) continue
    const jsonText = line.slice(at + JUDGE_MARKER.length).trim()
    try {
      const data = JSON.parse(jsonText)
      if (typeof data.case === 'number' && typeof data.ok === 'boolean') {
        results.push({ caseIndex: data.case, ok: data.ok, got: data.got, want: data.want })
      }
    } catch { /* 坏行跳过:用户输出/程序噪音不构成判定 */ }
  }
  return results
}

// ── io 比对规范化(纯函数)────────────────────────────────────

// 行尾空白(含 \r)去掉、末尾空行去掉——剩下的逐字符一致才算过;
// 行内前导空白保留(输出格式本身可能是考点)
export function normalizeIoOutput(text: string): string {
  const lines = text.split('\n').map(line => line.replace(/[ \t\r]+$/, ''))
  while (lines.length > 0 && lines[lines.length - 1] === '') lines.pop()
  return lines.join('\n')
}

// ── assert 判定(纯函数,便于单测)────────────────────────────

function pendingCases(tests: { label?: string }[]): JudgeCaseResult[] {
  return tests.map((test, i) => ({ index: i + 1, label: test.label, status: 'pending' as const }))
}

/** 从 CE 执行 payload 推出 assert 模式 verdict(不走网络,可直接喂测试样例) */
export function assertVerdictFrom(payload: any, tests: AssertJudgeTest[]): JudgeVerdict {
  const exec = parseExecutionResult(payload)
  if (exec.compileFailed) {
    return { status: 'CE', cases: pendingCases(tests), diagnostics: exec.diagnostics }
  }

  const lines = parseJudgeLines(exec.stdout)
  const cases: JudgeCaseResult[] = tests.map((test, i) => {
    const line = lines.find(l => l.caseIndex === i + 1)
    if (!line) return { index: i + 1, label: test.label, status: 'pending' as const }
    return {
      index: i + 1,
      label: test.label,
      status: line.ok ? 'pass' as const : 'fail' as const,
      got: line.got,
      expected: line.want,
    }
  })

  if (exec.timedOut) {
    return { status: 'TLE', cases, diagnostics: '执行超时,被 Compiler Explorer 终止(粗判定,非毫秒级计时)' }
  }
  if (exec.exitCode !== undefined && exec.exitCode !== 0) {
    return { status: 'RE', cases, diagnostics: exec.stderr.trim() || `程序异常退出,exit code: ${exec.exitCode}` }
  }
  if (lines.length === 0) {
    return { status: 'ERROR', cases, diagnostics: '程序正常结束,但未收到任何判题结果行' }
  }
  const firstFailedIndex = cases.findIndex(c => c.status === 'fail')
  return {
    status: firstFailedIndex === -1 ? 'AC' : 'WA',
    cases,
    firstFailed: firstFailedIndex === -1 ? undefined : firstFailedIndex + 1,
  }
}

// ── 判题入口 ──────────────────────────────────────────────────

export async function judge(req: JudgeRequest, opts: JudgeOptions = {}): Promise<JudgeVerdict> {
  reserveCompilerRequest()

  const compiler = req.compiler ?? DEFAULT_JUDGE_COMPILER
  const options = req.options ?? DEFAULT_JUDGE_OPTIONS

  // assert:拼装 → 一次请求 → 纯函数判定
  if (req.mode === 'assert') {
    const tests = req.tests as AssertJudgeTest[]
    const program = assembleAssertProgram(req.source, tests)
    const payload = await requestCeCompile(program, { compiler, options, executorRequest: true, signal: opts.signal })
    return assertVerdictFrom(payload, tests)
  }

  // io:用户程序原样,逐用例喂 stdin(一次判题 = 顺序编译执行 N 次)
  const tests = req.tests as IoJudgeTest[]
  const cases: JudgeCaseResult[] = tests.map((test, i) => ({ index: i + 1, label: test.label, status: 'pending' as const }))

  for (let i = 0; i < tests.length; i++) {
    if (opts.signal?.aborted) throw new Error('判题已中止')
    const test = tests[i]
    cases[i].status = 'running'
    opts.onProgress?.({ running: i + 1, total: tests.length, result: { ...cases[i] } })

    const payload = await requestCeCompile(req.source, {
      compiler,
      options,
      executorRequest: true,
      stdin: test.stdin,
      signal: opts.signal,
    })
    const exec = parseExecutionResult(payload)

    // 早退时把当前用例从 running 摆回 pending,别让结果里挂着"进行中"
    if (exec.compileFailed) {
      cases[i].status = 'pending'
      return { status: 'CE', cases, diagnostics: exec.diagnostics }
    }
    if (exec.timedOut) {
      cases[i].status = 'pending'
      return { status: 'TLE', cases, diagnostics: '执行超时,被 Compiler Explorer 终止(粗判定,非毫秒级计时)' }
    }
    if (exec.exitCode !== undefined && exec.exitCode !== 0) {
      cases[i].status = 'pending'
      return { status: 'RE', cases, diagnostics: exec.stderr.trim() || `程序异常退出,exit code: ${exec.exitCode}` }
    }

    const pass = normalizeIoOutput(exec.stdout) === normalizeIoOutput(test.expected)
    cases[i] = {
      index: i + 1,
      label: test.label,
      status: pass ? 'pass' as const : 'fail' as const,
      got: exec.stdout,
      expected: test.expected,
    }
    opts.onProgress?.({ running: i + 1, total: tests.length, result: { ...cases[i] } })
  }

  const firstFailedIndex = cases.findIndex(c => c.status === 'fail')
  return {
    status: firstFailedIndex === -1 ? 'AC' : 'WA',
    cases,
    firstFailed: firstFailedIndex === -1 ? undefined : firstFailedIndex + 1,
  }
}
