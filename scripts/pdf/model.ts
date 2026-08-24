export type BookLanguage = 'zh' | 'en'

export interface LocalizedText {
  zh: string
  en: string
}

export interface ContentUnit {
  id: string
  sourceDir: string
  urlPrefix: string
}

export interface BookDefinition {
  id: string
  title: LocalizedText
  label: LocalizedText
  units: string[]
}

export interface BookLocale {
  language: BookLanguage
  htmlLang: string
  sourcePrefix: string
  onlinePrefix: string
  strings: {
    contents: string
    generated: string
    sourceRevision: string
    onlineEdition: string
    references: string
    lectureResources: string
    sourceCode: string
    armSourceCode: string
  }
}

export interface Frontmatter {
  title?: string
  description?: string
  chapter?: string | number
  order?: string | number
  sidebar_order?: string | number
  [key: string]: unknown
}

export type DocumentKind = 'book-index' | 'chapter-index' | 'article'

export interface SourceDocument {
  sourcePath: string
  relativePath: string
  repositoryPath: string
  unit: ContentUnit
  docId: string
  canonicalPath: string
  title: string
  description: string
  chapter: string
  order: number
  kind: DocumentKind
  markdown: string
  frontmatter: Frontmatter
}

export interface HeadingInfo {
  originalId: string
  id: string
  level: number
  text: string
}

export interface RenderedDocument extends SourceDocument {
  html: string
  headings: HeadingInfo[]
  rootAnchor: string
  endnotes: string[]
  stats: TransformStats
}

export interface TransformStats {
  chapterNav: number
  chapterLink: number
  onlineCompilerDemo: number
  refLink: number
  referenceCard: number
  referenceItem: number
  talkInfoCard: number
  remoteImages: number
  internalLinks: number
  crossBookLinks: number
  paperContext: number
}

export interface BuildMetadata {
  version: string
  revision: string
  generatedAt: string
}

export interface BookBuild {
  definition: BookDefinition
  locale: BookLocale
  documents: RenderedDocument[]
  metadata: BuildMetadata
  outputDir: string
  assetDir: string
  htmlPath: string
  pdfPath: string
  reportPath: string
}

export interface BuildReport {
  schemaVersion: 1
  book: string
  language: BookLanguage
  revision: string
  generatedAt: string
  sourceDocuments: number
  renderedDocuments: number
  pageCount?: number
  pdfBytes?: number
  elapsedMs: Record<string, number>
  transforms: TransformStats
  warnings: string[]
  versions: Record<string, string>
}
