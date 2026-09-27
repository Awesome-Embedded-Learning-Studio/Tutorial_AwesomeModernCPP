// 本地搜索 worker(vitepress theme 覆盖件,配套 VPLocalSearchBox.vue)。
// 全站索引是一块 13MB 级的 JSON(zh),原先 JSON.parse + MiniSearch 重建 +
// 每次击键的查询全在主线程,搜索一开页面就冻住(issue #156)。这里把这些
// 重活全部搬进 worker,主线程只收结果。
//
// 协议(请求按发送顺序串行处理,响应带 id 原路返回):
//   { type: 'init',   id, json, options } -> { type: 'ready' | 'error', id }
//   { type: 'search', id, query }         -> { type: 'results', id, results }
import MiniSearch from 'minisearch'

// 与 VPLocalSearchBox 的展示条数一致;主线程侧不再重复截断
const RESULT_LIMIT = 16

let index: MiniSearch<any> | null = null

self.onmessage = (e: MessageEvent) => {
  const msg = e.data
  if (msg.type === 'init') {
    try {
      index = MiniSearch.loadJSON(msg.json, msg.options)
      self.postMessage({ type: 'ready', id: msg.id })
    } catch (err) {
      index = null
      self.postMessage({ type: 'error', id: msg.id, message: String(err) })
    }
  } else if (msg.type === 'search') {
    // init 尚未完成时收到查询:回空结果,主线程有 loading 态兜底
    const results = index ? index.search(msg.query).slice(0, RESULT_LIMIT) : []
    self.postMessage({ type: 'results', id: msg.id, results })
  }
}
