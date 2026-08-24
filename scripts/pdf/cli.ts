#!/usr/bin/env node
import { resolve } from 'node:path'
import { BOOKS, getBook } from './books'
import { buildBook } from './build-book'
import { createBookMarkdownRenderer } from './markdown'
import type { BookLanguage } from './model'

interface CliOptions {
  books: string[]
  language: BookLanguage
  output: string
  list: boolean
  json: boolean
  htmlOnly: boolean
  keepStaging: boolean
  timeoutMs?: number
  executablePath?: string
}

function usage(): string {
  return `Usage:
  pnpm pdf -- --book <id|all> [--language zh|en] [--output dir]
  pnpm pdf:list -- --json

Options:
  --book <id|all>          Book to build (repeatable; default: getting-started)
  --language <zh|en>       Source language (default: zh)
  --output <directory>     Final PDF/report directory (default: dist/pdf)
  --html-only              Assemble and validate HTML without launching Chromium
  --keep-staging           Preserve .pdf-build/<book>-<language>
  --timeout <seconds>      Browser/pagination timeout (default: 900)
  --executable-path <path> Use a specific Chromium executable
  --list [--json]          List publication books
  --help                   Show this help
`
}

function valueAfter(args: string[], index: number, option: string): string {
  const value = args[index + 1]
  if (!value || value.startsWith('--')) throw new Error(`${option} requires a value`)
  return value
}

function parseArgs(args: string[]): CliOptions {
  const options: CliOptions = {
    books: [],
    language: 'zh',
    output: 'dist/pdf',
    list: false,
    json: false,
    htmlOnly: false,
    keepStaging: false,
  }
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index]
    if (argument === '--') {
      continue
    } else if (argument === '--help' || argument === '-h') {
      process.stdout.write(usage())
      process.exit(0)
    } else if (argument === '--book') {
      options.books.push(valueAfter(args, index, argument))
      index += 1
    } else if (argument === '--language') {
      const language = valueAfter(args, index, argument)
      if (language !== 'zh' && language !== 'en') throw new Error(`Unsupported language "${language}"`)
      options.language = language
      index += 1
    } else if (argument === '--output') {
      options.output = valueAfter(args, index, argument)
      index += 1
    } else if (argument === '--timeout') {
      const seconds = Number(valueAfter(args, index, argument))
      if (!Number.isFinite(seconds) || seconds <= 0) throw new Error(`Invalid --timeout value ${seconds}`)
      options.timeoutMs = seconds * 1000
      index += 1
    } else if (argument === '--executable-path') {
      options.executablePath = valueAfter(args, index, argument)
      index += 1
    } else if (argument === '--list') {
      options.list = true
    } else if (argument === '--json') {
      options.json = true
    } else if (argument === '--html-only') {
      options.htmlOnly = true
    } else if (argument === '--keep-staging') {
      options.keepStaging = true
    } else {
      throw new Error(`Unknown option "${argument}"`)
    }
  }
  return options
}

async function main(): Promise<void> {
  const options = parseArgs(process.argv.slice(2))
  if (options.list) {
    if (options.json) {
      process.stdout.write(`${JSON.stringify(BOOKS.map(({ id }) => ({ id })))}\n`)
    } else {
      for (const book of BOOKS) process.stdout.write(`${book.id}\t${book.label.zh} · ${book.title.zh}\n`)
    }
    return
  }

  const repositoryRoot = resolve(import.meta.dirname, '..', '..')
  const selectedIds = options.books.length ? options.books : ['getting-started']
  const definitions = selectedIds.includes('all')
    ? [...BOOKS]
    : selectedIds.map(getBook)
  const markdown = await createBookMarkdownRenderer(repositoryRoot)

  for (const definition of definitions) {
    const { build, report } = await buildBook({
      repositoryRoot,
      definition,
      language: options.language,
      outputDir: resolve(repositoryRoot, options.output),
      markdown,
      htmlOnly: options.htmlOnly,
      keepStaging: options.keepStaging,
      timeoutMs: options.timeoutMs,
      executablePath: options.executablePath,
      log: (message) => process.stderr.write(`${message}\n`),
    })
    const output = options.htmlOnly ? build.htmlPath : build.pdfPath
    process.stdout.write(`${output}\n`)
    if (report.warnings.length) {
      for (const warning of report.warnings) process.stderr.write(`[warning] ${warning}\n`)
    }
  }
}

main().catch((error) => {
  process.stderr.write(`${error instanceof Error ? error.stack ?? error.message : String(error)}\n`)
  process.exitCode = 1
})
