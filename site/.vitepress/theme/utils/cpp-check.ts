import { requestCeCompile } from './ce-api'
import { DEFAULT_JUDGE_COMPILER, DEFAULT_JUDGE_OPTIONS, reserveCompilerRequest } from './judge'

export interface CodeDiagnostic { line: number; column: number; severity: 'error' | 'warning' | 'info'; message: string }
export interface CodeCheck { ok: boolean; diagnostics: CodeDiagnostic[]; text: string }

export function parseCodeCheck(payload: unknown): CodeCheck {
  if (!payload || typeof payload !== 'object' || !('code' in payload) || typeof payload.code !== 'number') {
    throw new Error('编译服务没有返回有效的检查结果，请稍后再试。')
  }
  const result = payload as { code: number; stderr?: unknown; stdout?: unknown }
  const diagnostics: CodeDiagnostic[] = []
  const text: string[] = []
  for (const stream of [result.stderr, result.stdout]) {
    if (!Array.isArray(stream)) continue
    for (const entry of stream) {
      const message = (typeof entry === 'string' ? entry : String(entry?.text ?? '')).replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '')
      if (message) text.push(message)
      const match = message.match(/^(.*?):(\d+):(\d+):\s*(fatal error|error|warning|note):\s*(.*)$/)
      const file = entry?.tag?.file ?? match?.[1]
      // 系统头文件中的诊断保留在列表,不映射到用户代码的同一行。
      if (typeof file !== 'string' || !/^(?:<source>|<stdin>|(?:.*[/\\])?example\.cpp)$/.test(file)) continue
      const line = Number(entry?.tag?.line ?? match?.[2])
      const column = Number(entry?.tag?.column ?? match?.[3] ?? 1)
      if (!Number.isInteger(line) || line < 1 || !Number.isInteger(column) || column < 1) continue
      const severity = /(?:fatal )?error:/.test(message) ? 'error' : /warning:/.test(message) ? 'warning' : 'info'
      diagnostics.push({ line, column, severity, message: match?.[5] ?? message })
    }
  }
  return { ok: result.code === 0, diagnostics, text: text.join('\n') }
}

/** 只编译当前源码,不拼接测试 main、不执行程序、不修改做题进度。 */
export async function checkCppSource(source: string, signal?: AbortSignal): Promise<CodeCheck> {
  reserveCompilerRequest()
  const payload = await requestCeCompile(source, {
    compiler: DEFAULT_JUDGE_COMPILER,
    options: `${DEFAULT_JUDGE_OPTIONS} -fsyntax-only -Wall -Wextra`,
    executorRequest: false,
    signal,
  })
  return parseCodeCheck(payload)
}
