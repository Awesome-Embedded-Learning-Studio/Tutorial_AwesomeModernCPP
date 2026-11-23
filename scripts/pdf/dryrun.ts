/**
 * 渲染层干跑:对全部书册只跑 Markdown 渲染与组件转换(与正式构建同一
 * 代码路径),不启动浏览器、不分页、不写 PDF。失败不中断,收尾汇总,
 * 用于在改完管线或文档后一次性暴露所有渲染层问题。
 *
 *   tsx scripts/pdf/dryrun.ts [--language zh|en] [--book id[,id...]]
 */
import { discoverRepositoryDocuments } from './catalog'
import { BOOKS, getLocale, getUnit, type BookLanguage } from './books'
import type { BookDefinition } from './model'
import { createBookMarkdownRenderer } from './markdown'
import { transformDocument, type TransformContext } from './transform'

interface DryRunOptions {
  language: BookLanguage
  books: readonly BookDefinition[]
}

function parseOptions(argv: string[]): DryRunOptions {
  const options: DryRunOptions = { language: 'zh', books: BOOKS }
  for (let index = 2; index < argv.length; index += 1) {
    const argument = argv[index]
    if (argument === '--language') {
      const value = argv[index + 1]
      if (value !== 'zh' && value !== 'en') throw new Error('--language expects zh or en')
      options.language = value
      index += 1
    } else if (argument === '--book') {
      const value = argv[index + 1]
      if (!value) throw new Error('--book expects a comma-separated list of book ids')
      const ids = value.split(',').map((entry) => entry.trim()).filter(Boolean)
      const unknown = ids.filter((id) => !BOOKS.some((book) => book.id === id))
      if (unknown.length) throw new Error(`Unknown book id(s): ${unknown.join(', ')}`)
      options.books = BOOKS.filter((book) => ids.includes(book.id))
      index += 1
    } else {
      throw new Error(`Unknown argument: ${argument}`)
    }
  }
  return options
}

async function main(): Promise<void> {
  const options = parseOptions(process.argv)
  const locale = getLocale(options.language)
  const repositoryRoot = process.cwd()
  const markdown = await createBookMarkdownRenderer(repositoryRoot)
  const context: TransformContext = {
    repositoryRoot,
    markdown,
    locale,
    // Dry run copies nothing: asset paths are still resolved (so missing
    // local images surface), but no bytes move and nothing is staged.
    assets: {
      resolveSourceAsset: () => '',
      copyLocalAsset: async () => '',
    } as TransformContext['assets'],
    resolveLink: (href) => ({ kind: 'external', href }),
  }

  const documents = discoverRepositoryDocuments({
    repositoryRoot,
    locales: [locale],
    units: options.books.flatMap((book) => book.units.map(getUnit)),
  })
  const failures: Array<{ repositoryPath: string; message: string }> = []
  let checked = 0

  for (const document of documents) {
    checked += 1
    try {
      await transformDocument(document, context)
    } catch (error) {
      // One document can report several unknown tags at once; keep them all.
      failures.push({ repositoryPath: document.repositoryPath, message: String(error) })
    }
  }

  console.log(`[dryrun] language=${options.language} books=${options.books.map(({ id }) => id).join(',')}`)
  console.log(`[dryrun] documents=${checked} failures=${failures.length}`)
  for (const failure of failures) {
    console.log(`FAIL ${failure.repositoryPath}:\n${failure.message}`)
  }
  if (failures.length) process.exitCode = 1
}

await main()
