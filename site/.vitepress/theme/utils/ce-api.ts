// Compiler Explorer(CE)API 客户端 —— 从 OnlineCompilerDemo 抽出的共享层。
// 负责:发起编译/执行请求、解析执行输出与汇编、收集编译诊断。
// 「每周一些题」的判题内核(judge)也踩在这层上,改动请保持行为兼容。

export interface CeCompileAction {
  compiler: string
  options: string
  /** true = 执行请求(executor),拿程序 stdout/stderr/exit code;false = 汇编请求 */
  executorRequest: boolean
  /** true 时 filters.intel(x86-64 汇编用 Intel 语法) */
  intel?: boolean
  /** 执行请求的 stdin(默认 '')——判题 io 模式逐用例喂输入 */
  stdin?: string
  /** 执行请求的命令行参数(默认 '') */
  args?: string
  /** 中止信号,透传给 fetch */
  signal?: AbortSignal
}

/** 请求 CE 编译(可选执行)。HTTP 层失败抛 Error;业务结果由调用方用下方提取函数解析 */
export async function requestCeCompile(source: string, action: CeCompileAction): Promise<any> {
  const response = await fetch(`https://godbolt.org/api/compiler/${action.compiler}/compile`, {
    method: 'POST',
    headers: {
      Accept: 'application/json',
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({
      source,
      options: {
        userArguments: action.options,
        compilerOptions: {
          executorRequest: action.executorRequest,
        },
        filters: {
          binary: false,
          commentOnly: true,
          demangle: true,
          directives: true,
          execute: action.executorRequest,
          intel: action.intel === true,
          labels: true,
          libraryCode: false,
          trim: false,
        },
        executeParameters: {
          args: action.args ?? '',
          stdin: action.stdin ?? '',
        },
      },
    }),
    signal: action.signal,
  })

  if (!response.ok) {
    throw new Error(`Compiler Explorer 请求失败 (${response.status} ${response.statusText})`)
  }

  return response.json()
}

function linesToText(value: unknown): string {
  if (!value) return ''
  if (typeof value === 'string') return stripAnsi(value)
  if (Array.isArray(value)) {
    return value.map((line) => {
      if (typeof line === 'string') return stripAnsi(line)
      if (line && typeof line === 'object' && 'text' in line) {
        return stripAnsi(String((line as { text: unknown }).text ?? ''))
      }
      return stripAnsi(String(line ?? ''))
    }).join('\n')
  }
  return stripAnsi(String(value))
}

function stripAnsi(value: string): string {
  return value.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '')
}

export function extractExecutionText(payload: any): string {
  const exec = payload.execResult ?? payload.executionResult ?? payload
  // godbolt executor 响应常把程序输出同时放在 execResult 和顶层（此时 exec===payload），
  // 每路只取第一份非空的，避免把同一份输出拼两遍。（对齐 C-Journey f85300b 修复）
  if (isCompilationFailure(payload, linesToText(payload.asm))) {
    const diag = gatherDiagnostics(payload)
    return diag
      ? `❌ 编译失败：\n${diag}`
      : '❌ 编译失败，但 Compiler Explorer 没有返回诊断信息。检查源码语法、编译器 id 与参数，或点「打开 Godbolt」看完整输出。'
  }
  const out = linesToText(exec.stdout) || linesToText(payload.stdout) || linesToText(payload.buildResult?.stdout)
  const err = linesToText(exec.stderr) || linesToText(payload.stderr) || linesToText(payload.buildResult?.stderr)
  const chunks = [out, err].filter(Boolean)

  if (exec.code !== undefined && exec.code !== 0) chunks.push(`exit code: ${exec.code}`)
  else if (payload.code !== undefined && payload.code !== 0) chunks.push(`exit code: ${payload.code}`)
  return chunks.join('\n').trim() || '(程序无输出)'
}

// 收集编译/运行诊断(execResult → 顶层 → buildResult，stderr 优先再 stdout)，每路第一份非空避免重复
function gatherDiagnostics(payload: any): string {
  const exec = payload.execResult ?? payload.executionResult ?? payload
  const err = linesToText(exec.stderr) || linesToText(payload.stderr) || linesToText(payload.buildResult?.stderr)
  const out = linesToText(exec.stdout) || linesToText(payload.stdout) || linesToText(payload.buildResult?.stdout)
  return [err, out].filter(Boolean).join('\n')
}

export function extractAsmText(payload: any): string {
  const asm = linesToText(payload.asm)
  if (isCompilationFailure(payload, asm)) {
    // 顶层与 buildResult 可能同源，每路只取第一份非空。
    const err = linesToText(payload.stderr) || linesToText(payload.buildResult?.stderr)
    const out = linesToText(payload.stdout) || linesToText(payload.buildResult?.stdout)
    const diag = [err, out].filter(Boolean).join('\n')
    return diag
      ? `❌ 编译失败：\n${diag}`
      : '❌ 编译失败，但 Compiler Explorer 没有返回诊断信息。检查源码语法、编译器 id 与参数，或点「打开 Godbolt」看完整输出。'
  }
  return (asm || 'Compiler Explorer 没有返回可显示的汇编输出。').trim()
}

function isCompilationFailure(payload: any, asm: string): boolean {
  return payload.code !== undefined && payload.code !== 0
    || payload.buildResult?.code !== undefined && payload.buildResult.code !== 0
    || asm.includes('<Compilation failed>')
}

/** CE 执行请求的结构化结果(判题内核用):区分编译失败 / 运行输出 / 超时 */
export interface CeExecutionResult {
  compileFailed: boolean
  stdout: string
  stderr: string
  /** 程序退出码(executor 响应中非零 = 运行期错误;编译失败时无意义) */
  exitCode?: number
  /** 被 CE 执行时限终止 */
  timedOut: boolean
  /** 编译失败时的诊断文本 */
  diagnostics: string
}

// 编译失败诊断:executor 响应的真实 gcc 诊断在 buildResult.stderr(实测 2026-09),
// 顶层 stderr 常只有一句 "Build failed" 桩文本——不能"第一个非空就停",
// 多路合并去重,保证用户能看到真正的报错。
function collectCompileDiagnostics(payload: any): string {
  const parts = [
    linesToText(payload.buildResult?.stderr),
    linesToText(payload.stderr),
    linesToText(payload.buildResult?.stdout),
    linesToText(payload.stdout),
  ]
  const seen = new Set<string>()
  return parts
    .filter(Boolean)
    .filter(part => (seen.has(part) ? false : (seen.add(part), true)))
    .join('\n')
}

// 把 CE 执行请求的原始 payload 摊平成结构化结果。
// 实测(2026-09,CE executor 现行响应)形态:顶层 code 是「程序退出码」,
// 编译成败看 buildResult.code,超时标记是顶层 timedOut——所以这里不能复用
// isCompilationFailure 的"顶层 code 非零 = 编译失败"(那会把 SIGABRT/SIGKILL
// 误判成编译失败)。旧形态(无 buildResult)退回 asm 标记判断。
export function parseExecutionResult(payload: any): CeExecutionResult {
  const exec = payload.execResult ?? payload.executionResult ?? payload
  const compileFailed = payload.buildResult
    ? payload.buildResult.code !== undefined && payload.buildResult.code !== 0
    : isCompilationFailure(payload, linesToText(payload.asm))
  const stdout = linesToText(exec.stdout) || linesToText(payload.stdout) || linesToText(payload.buildResult?.stdout)
  const stderr = linesToText(exec.stderr) || linesToText(payload.stderr) || linesToText(payload.buildResult?.stderr)
  return {
    compileFailed,
    stdout,
    stderr,
    exitCode: exec.code ?? payload.code,
    timedOut: payload.timedOut === true || payload.didTimeout === true || exec.didTimeout === true,
    diagnostics: compileFailed ? collectCompileDiagnostics(payload) : '',
  }
}
