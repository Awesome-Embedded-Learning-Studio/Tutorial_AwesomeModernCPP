import { useRouter } from 'vitepress'

// 仅 dev 生效的人为卡顿开关：模拟弱网/冷缓存下「点链接 → 目标页 chunk 现场
// 拉取」的延迟，肉眼验证 NavProgress 跳转进度条（issue #167）。dev 里模块都
// 在本机、几乎瞬开，不造卡顿根本看不到条。
//
// 用法（浏览器 console，改完刷新生效）：
//   localStorage.setItem('fakeLag', '1500')   // 每次 SPA 跳转前人为睡 1.5s（测角落小卡）
//   localStorage.setItem('fakeLag', '5000')   // 睡 5s（测 3s 后的整站浮层）
//   localStorage.removeItem('fakeLag')        // 关掉
//
// 实现挂在 router.onBeforePageLoad 上：它是 vitepress（1.6.4）loadPage() 里
// chunk 拉取前唯一的 await 点，睡这里等价于睡「跳转卡顿」本体，连后退/前进
// 的 popstate 路径也一起延迟。生产构建 import.meta.env.DEV 为 false，函数体
// 整个被摇掉，零影响。注意它也是单值属性，若将来有正经功能也要用这个钩子，
// 得把它挪进 router-hooks.ts 的订阅器。
export function setupDevFakeLag(): void {
  if (!import.meta.env.DEV || import.meta.env.SSR) return
  const lag = Number(localStorage.getItem('fakeLag') || 0)
  if (!lag || lag <= 0) return
  const router = useRouter()
  router.onBeforePageLoad = async () => {
    await new Promise((resolve) => setTimeout(resolve, lag))
  }
  console.warn(`[dev-fake-lag] SPA 跳转人为延迟 ${lag}ms 已生效（localStorage.removeItem('fakeLag') 关闭）`)
}
