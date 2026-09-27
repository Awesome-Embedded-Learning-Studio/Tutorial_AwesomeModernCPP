// 「每周一些题」题目数据模型:quiz.json 的解析与校验(客户端加载 + 单测共用)。
//
// 统一约定:一题一目录,目录即题目全部——
//   problem.md 题面 | quiz.json 结构化数据 | starter.cpp 模板
//   solution.cpp 参考实现 | code.cpp 找bug题面代码 | answer.md 题解
// 页面只放 <QuizProblem src="…" />。
//
// 测例格式(写在 quiz.json 的 tests 字段):
//   judge-assert:字符串数组,每行「表达式 == 期望值」
//   judge-io:对象数组 [{ "in": "…", "out": "…" }]
//
// 配置错误在加载/解析时抛出,组件渲染为坏卡提示(不再有构建期容器校验,
// P4 的 preflight 会补一道出题目录的静态检查)。

import type { AssertJudgeTest, IoJudgeTest } from './judge'

export type QuizType = 'judge-assert' | 'judge-io' | 'fill' | 'choice' | 'find-bug' | 'reveal'

export const QUIZ_TYPES: QuizType[] = ['judge-assert', 'judge-io', 'fill', 'choice', 'find-bug', 'reveal']

export const QUIZ_TYPE_LABELS: Record<QuizType, string> = {
  'judge-assert': '实现 · 函数式',
  'judge-io': '实现 · 程序式',
  'fill': '口答 · 填写',
  'choice': '口答 · 选择',
  'find-bug': '找 bug · 开放思考',
  'reveal': '思考 · 展开',
}

export interface QuizConfig {
  type: QuizType
  title: string
  stars: number
  /** 多级提示(markdown 行内语法即可) */
  hints: string[]
  mode?: 'assert' | 'io'
  tests?: AssertJudgeTest[] | IoJudgeTest[]
  options?: string[]
  /** choice:正确选项 1-based 行号 */
  answer?: number[]
  /** fill:期望文本 */
  answerText?: string
  /** find-bug:答案侧高亮的 1-based 行号(相对 code.cpp) */
  bugLines?: number[]
}

/** 「表达式 == 期望值」断言,按最后一个 == 切(call 表达式自身可能含 ==) */
export function parseAssertTests(lines: string[]): AssertJudgeTest[] {
  const tests = lines
    .map(line => line.trim())
    .filter(Boolean)
    .map(line => {
      const at = line.lastIndexOf('==')
      if (at <= 0 || at + 2 >= line.length) {
        throw new Error(`tests 断言语法应为「表达式 == 期望值」,这行不合法:${line}`)
      }
      return { call: line.slice(0, at).trim(), expected: line.slice(at + 2).trim() }
    })
  if (tests.length === 0) throw new Error('tests 至少要有一条断言')
  return tests
}

export function parseIoTests(cases: Array<{ in?: unknown; out?: unknown }>): IoJudgeTest[] {
  if (!Array.isArray(cases) || cases.length === 0) {
    throw new Error('tests 至少要有一组 {"in": …, "out": …}')
  }
  return cases.map(({ in: stdin, out: expected }, i) => {
    if (typeof stdin !== 'string' || typeof expected !== 'string') {
      throw new Error(`tests 第 ${i + 1} 组的 in/out 都必须是字符串`)
    }
    return { stdin, expected }
  })
}

/** 解析并校验 quiz.json(题型决定必填字段);错误消息面向出题人 */
export function parseQuizConfig(raw: unknown): QuizConfig {
  if (!raw || typeof raw !== 'object') throw new Error('quiz.json 不是合法的 JSON 对象')
  const cfg = raw as Record<string, unknown>

  const type = cfg.type
  if (typeof type !== 'string' || !QUIZ_TYPES.includes(type as QuizType)) {
    throw new Error(`type 必须是 ${QUIZ_TYPES.join(' / ')} 之一,现在的是:${JSON.stringify(type)}`)
  }
  if (typeof cfg.title !== 'string' || !cfg.title.trim()) throw new Error('title 必须是非空字符串')
  const stars = typeof cfg.stars === 'number' && cfg.stars >= 1 && cfg.stars <= 3 ? Math.floor(cfg.stars) : 3
  const hints = Array.isArray(cfg.hints) ? cfg.hints.filter((h): h is string => typeof h === 'string') : []

  const config: QuizConfig = { type: type as QuizType, title: cfg.title.trim(), stars, hints }

  if (type === 'judge-assert' || type === 'judge-io') {
    if (!Array.isArray(cfg.tests)) throw new Error(`【${cfg.title}】判题类必须有 tests 数组`)
    config.mode = type === 'judge-assert' ? 'assert' : 'io'
    config.tests = type === 'judge-assert'
      ? parseAssertTests(cfg.tests as unknown as string[])
      : parseIoTests(cfg.tests as unknown as Array<{ in?: unknown; out?: unknown }>)
  } else if (type === 'choice') {
    const options = Array.isArray(cfg.options) ? (cfg.options as unknown[]).map(o => String(o)) : []
    if (options.length < 2) throw new Error(`【${cfg.title}】choice 必须有 options(至少两项)`)
    const answer = Array.isArray(cfg.answer) ? (cfg.answer as unknown[]).map(n => Number(n)) : []
    if (!answer.length || answer.some(n => !Number.isInteger(n) || n < 1 || n > options.length)) {
      throw new Error(`【${cfg.title}】answer 应是 options 的 1-based 行号数组,当前 options 共 ${options.length} 项`)
    }
    config.options = options
    config.answer = answer
  } else if (type === 'fill') {
    if (typeof cfg.answerText !== 'string' || !cfg.answerText.trim()) {
      throw new Error(`【${cfg.title}】fill 必须有 answerText(期望文本)`)
    }
    config.answerText = cfg.answerText
  } else if (type === 'find-bug') {
    const bugLines = Array.isArray(cfg.bugLines) ? (cfg.bugLines as unknown[]).map(n => Number(n)) : []
    if (!bugLines.length || bugLines.some(n => !Number.isInteger(n) || n < 1)) {
      throw new Error(`【${cfg.title}】find-bug 必须有 bugLines(1-based 行号数组,行数由 code.cpp 决定)`)
    }
    config.bugLines = bugLines
  } else {
    if (!Array.isArray(cfg.hints) || hints.length === 0) {
      if (typeof cfg.answerText !== 'string') {
        // reveal 不强制 hints,但答案(answer.md)由目录约定保证;这里不做额外校验
      }
    }
  }
  return config
}
