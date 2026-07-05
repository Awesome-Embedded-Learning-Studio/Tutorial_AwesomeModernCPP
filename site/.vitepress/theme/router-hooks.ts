import { useRouter } from 'vitepress'

// VitePress 的 router.onAfterRouteChange 是单值属性，不是事件订阅：
// 任意组件再赋值就会覆盖前一个。曾经 ReadingProgress 和 mermaid 各自赋值，
// 导致 SPA 跳转后 mermaid 不渲染（首屏 onMounted 路径正常，跳转路径被覆盖）。
// 这里包一层订阅器：第一次订阅时安装一个 dispatcher，之后所有订阅者共享。
type RouteHandler = (href: string) => void

const subscribers = new Set<RouteHandler>()
let installed = false

export function subscribeAfterRouteChange(fn: RouteHandler): void {
  subscribers.add(fn)
  if (installed) return
  // 必须在 setup 上下文调用（mermaid 的 setupMermaid / 各组件 setup 均满足）。
  const router = useRouter()
  router.onAfterRouteChange = (href: string) => {
    for (const fn of subscribers) {
      try {
        fn(href)
      } catch (e) {
        // 单个订阅者抛错不能连累其它订阅者（否则 mermaid 渲染失败会让进度条也不更新）。
        console.error('[router-hook] onAfterRouteChange 订阅者抛错', e)
      }
    }
  }
  installed = true
}
