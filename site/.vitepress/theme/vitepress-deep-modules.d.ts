// VPLocalSearchBox.vue 覆盖件用到的 vitepress 内部模块的手写最小类型。
// 这些文件经 vitepress 的 "./dist/*" exports 深导入可达,但包里没有随附
// .d.ts;类型声明只为编辑器/TS 解析,不参与构建产物。
declare module '@localSearchIndex' {
  const localSearchIndex: Record<
    string,
    () => Promise<{ default: string }> | undefined
  >
  export default localSearchIndex
}

declare module 'vitepress/dist/client/app/utils.js' {
  export function pathToFile(path: string): string | null
}

declare module 'vitepress/dist/client/shared.js' {
  export function escapeRegExp(str: string): string
}

declare module 'vitepress/dist/client/theme-default/support/lru.js' {
  export class LRUCache<K, V> {
    constructor(max?: number)
    get(key: K): V | undefined
    set(key: K, value: V): void
    clear(): void
  }
}

declare module 'vitepress/dist/client/theme-default/support/translation.js' {
  export function createSearchTranslate<T extends object>(
    defaultTranslations: T
  ): (key: string) => string
}

declare module 'mark.js/src/vanilla.js' {
  export default class Mark {
    constructor(root: HTMLElement)
    unmark(options: { done?: () => void }): void
    markRegExp(
      regex: RegExp,
      options: { done?: () => void; acrossElements?: boolean }
    ): void
  }
}
