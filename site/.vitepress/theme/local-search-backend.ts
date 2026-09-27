// 搜索后端封装(vitepress theme 覆盖件,配套 VPLocalSearchBox.vue)。
// 优先用 Web Worker 跑索引构建 + 查询;worker 不可用(老浏览器/被 CSP 拦)
// 或 worker 初始化失败时,自动回退到 VitePress 原行为:主线程 MiniSearch。
//
// 实例是模块级单例:搜索盒每次开关不重建、不丢已解析的索引,重新打开
// 搜索时不用再付一次解析成本。
import MiniSearch, { type Options, type SearchResult } from 'minisearch'

// 与 worker 侧、VPLocalSearchBox 的展示条数一致
const RESULT_LIMIT = 16

export interface SearchBackend {
  init(json: string, options: Options<any>): Promise<void>
  search(query: string): Promise<SearchResult[]>
}

let shared: SearchBackend | null = null

export function getSearchBackend(): SearchBackend {
  if (!shared) {
    shared = createWorkerBackend() ?? createMainThreadBackend()
  }
  return shared
}

function createWorkerBackend(): SearchBackend | null {
  // 该模块只会在浏览器挂载的组件里被调用,这里再兜一道 SSR/环境守卫
  if (import.meta.env.SSR || typeof Worker === 'undefined') return null
  try {
    return new WorkerSearchBackend()
  } catch {
    return null
  }
}

class WorkerSearchBackend implements SearchBackend {
  private worker: Worker
  private dead = false
  // worker 初始化失败后改走的主线程索引(即 VitePress 原行为)
  private fallbackIndex: MiniSearch<any> | null = null
  private msgId = 0
  private initWaiters = new Map<
    number,
    { resolve: () => void; reject: (err: unknown) => void }
  >()
  private searchWaiters = new Map<number, (results: SearchResult[]) => void>()

  constructor() {
    this.worker = new Worker(
      new URL('./local-search-worker.ts', import.meta.url),
      { type: 'module' }
    )
    this.worker.onmessage = (e: MessageEvent) => this.onMessage(e.data)
    this.worker.onerror = () => this.drainWaiters()
  }

  private onMessage(msg: {
    type: 'ready' | 'error' | 'results'
    id: number
    results?: SearchResult[]
    message?: string
  }) {
    if (msg.type === 'ready' || msg.type === 'error') {
      const waiter = this.initWaiters.get(msg.id)
      this.initWaiters.delete(msg.id)
      if (msg.type === 'ready') waiter?.resolve()
      else waiter?.reject(new Error(msg.message))
    } else if (msg.type === 'results') {
      const resolve = this.searchWaiters.get(msg.id)
      this.searchWaiters.delete(msg.id)
      resolve?.(msg.results ?? [])
    }
  }

  // worker 崩溃/加载失败:在途请求一律收尾,别让组件悬挂
  private drainWaiters() {
    this.dead = true
    for (const { reject } of this.initWaiters.values()) {
      reject(new Error('search worker crashed'))
    }
    this.initWaiters.clear()
    for (const resolve of this.searchWaiters.values()) resolve([])
    this.searchWaiters.clear()
  }

  async init(json: string, options: Options<any>): Promise<void> {
    // 已经在走主线程回退了,就不再碰 worker
    if (this.dead) {
      this.fallbackIndex = MiniSearch.loadJSON(json, options)
      return
    }
    const id = ++this.msgId
    const ready = new Promise<void>((resolve, reject) => {
      this.initWaiters.set(id, { resolve, reject })
    })
    this.worker.postMessage({ type: 'init', id, json, options })
    try {
      await ready
      this.fallbackIndex = null
    } catch {
      // worker 初始化失败:同一份 json 落回主线程,行为退回 VitePress 原状
      this.dead = true
      this.fallbackIndex = MiniSearch.loadJSON(json, options)
    }
  }

  async search(query: string): Promise<SearchResult[]> {
    if (this.fallbackIndex) {
      return this.fallbackIndex.search(query).slice(0, RESULT_LIMIT)
    }
    if (this.dead) return []
    const id = ++this.msgId
    return new Promise((resolve) => {
      this.searchWaiters.set(id, resolve)
      this.worker.postMessage({ type: 'search', id, query })
    })
  }
}

function createMainThreadBackend(): SearchBackend {
  let index: MiniSearch<any> | null = null
  return {
    async init(json, options) {
      index = MiniSearch.loadJSON(json, options)
    },
    async search(query) {
      return index ? index.search(query).slice(0, RESULT_LIMIT) : []
    },
  }
}
