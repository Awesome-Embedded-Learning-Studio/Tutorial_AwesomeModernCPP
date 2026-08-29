/**
 * 首页「学习路径图」种子数据（HomePathGraph.vue 消费）。
 *
 * 语义来源：documents/roadmap/index.md 的三条学习路径（A 零基础 / B 有 C 经验 / C 已会 C++）
 * 与 README 各卷一览的成熟度；节点 = 首页 features 的 16 项（卷级粒度，点击直达对应卷首页）。
 * 种子坐标是模块常量、SSG 直出；客户端拖拽后的布局存 localStorage（key 带 HOME_GRAPH_REVISION，
 * 节点增删后递增版本号，旧布局自动失效回落种子）。
 * EN 链接 = CN href 统一加 /en 前缀，由组件补全（cleanUrls 开启，两套目录同构）。
 */

export type PathNodeKind = 'root' | 'proj' | 'sup'
export type PathEdgeKind = 'solid' | 'dash' | 'dot'
export type PathSide = 'top' | 'right' | 'bottom' | 'left'
export type PathRouteCoord = number | 'from' | 'to'
/** done=已成型 doing=在建/持续更新（镜像 README 各卷一览的两类成熟度） */
export type PathStatus = 'done' | 'doing'
/** 配色三档，镜像 custom.css 首页卡片 rail 的 tier 分层（core=钢蓝 / eng=绿 / domain=紫） */
export type PathTier = 'core' | 'eng' | 'domain'

export interface Bi {
  cn: string
  en: string
}

export interface PathNode {
  /** 节点短 id（边的 from/to 用它） */
  id: string
  name: Bi
  sub: Bi
  /** 种子坐标（中心点）与尺寸 */
  x: number
  y: number
  w: number
  h: number
  kind: PathNodeKind
  status: PathStatus
  tier: PathTier
  /** CN 形态站内路径；EN 由组件补 '/en' 前缀后 withBase */
  href: string
  /** 左上角标 */
  badge: string
}

export interface PathEdge {
  from: string
  to: string
  /** solid = 建议主线路径；dash = 按需选修；dot = 支撑/索引（不进悬停高亮链） */
  kind: PathEdgeKind
  /**
   * 默认布局的布线提示。from/to 选节点锚点，via 把边送入层间或外围的固定轨道；
   * 坐标写 'from'/'to' 时跟随对应锚点，因此节点拖动后首尾段仍保持正交。
   */
  route?: {
    from: PathSide
    to: PathSide
    via?: Array<{ x: PathRouteCoord; y: PathRouteCoord }>
  }
}

export interface PathBand {
  label: Bi
  top: number
  bottom: number
}

/** 画布尺寸（SVG viewBox） */
export const VB_W = 1330
export const VB_H = 856

/** localStorage 布局 key 版本：节点增删/坐标或默认布线大改时 +1 */
export const HOME_GRAPH_REVISION = 2

export const HOME_PATH_BANDS: PathBand[] = [
  { label: { cn: 'B0 · 起点', en: 'B0 · Start' }, top: 16, bottom: 104 },
  { label: { cn: 'B1 · 基础与现代', en: 'B1 · Fundamentals & Modern' }, top: 138, bottom: 248 },
  { label: { cn: 'B2 · 标准库与高级', en: 'B2 · Stdlib & Advanced' }, top: 282, bottom: 392 },
  { label: { cn: 'B3 · 并发 · 性能 · 工程', en: 'B3 · Concurrency · Perf · Eng' }, top: 426, bottom: 546 },
  { label: { cn: 'B4 · 领域实战与深化', en: 'B4 · Domains & Deep Dive' }, top: 580, bottom: 700 },
  { label: { cn: 'B5 · 社区与索引', en: 'B5 · Community & Index' }, top: 734, bottom: 834 },
]

export const HOME_PATH_NODES: PathNode[] = [
  { id: 'gs',    name: { cn: '新手起步',   en: 'Getting Started' }, sub: { cn: '装环境 · 跑通第一课', en: 'Setup · first lesson' },        x: 665,  y: 60,  w: 180, h: 66, kind: 'root', status: 'done',  tier: 'core',   href: '/getting-started/',          badge: 'GS' },
  { id: 'v1',    name: { cn: '卷一 · 基础', en: 'Vol.1 Fundamentals' }, sub: { cn: '类型 · OOP · 模板', en: 'Types · OOP · templates' },  x: 340,  y: 193, w: 160, h: 58, kind: 'proj', status: 'done',  tier: 'core',   href: '/vol1-fundamentals/',        badge: 'V1' },
  { id: 'v2',    name: { cn: '卷二 · 现代', en: 'Vol.2 Modern' },       sub: { cn: '移动 · 智能指针 · lambda', en: 'Move · smart ptr · lambda' }, x: 650, y: 193, w: 160, h: 58, kind: 'proj', status: 'done',  tier: 'core',   href: '/vol2-modern-features/',     badge: 'V2' },
  { id: 'comp',  name: { cn: '编译与链接', en: 'Compile & Link' },      sub: { cn: '编译 · 链接 · 符号', en: 'Build · link · symbols' },      x: 980,  y: 193, w: 140, h: 46, kind: 'sup',  status: 'done',  tier: 'domain', href: '/compilation/',              badge: 'CMP' },
  { id: 'v3',    name: { cn: '卷三 · 标准库', en: 'Vol.3 Stdlib' },     sub: { cn: '容器 · 迭代器 · 算法', en: 'Containers · algorithms' }, x: 440,  y: 337, w: 160, h: 58, kind: 'proj', status: 'done',  tier: 'core',   href: '/vol3-standard-library/',    badge: 'V3' },
  { id: 'v4',    name: { cn: '卷四 · 高级', en: 'Vol.4 Advanced' },     sub: { cn: 'concepts · 协程 · ranges', en: 'Concepts · coroutines' }, x: 820, y: 337, w: 160, h: 58, kind: 'proj', status: 'doing', tier: 'core',   href: '/vol4-advanced/',            badge: 'V4' },
  { id: 'v5',    name: { cn: '卷五 · 并发', en: 'Vol.5 Concurrency' },  sub: { cn: '线程 · 原子 · 协程异步', en: 'Threads · atomics' },      x: 260,  y: 486, w: 160, h: 58, kind: 'proj', status: 'done',  tier: 'eng',    href: '/vol5-concurrency/',         badge: 'V5' },
  { id: 'v6',    name: { cn: '卷六 · 性能', en: 'Vol.6 Performance' },  sub: { cn: '缓存 · SIMD · 基准', en: 'Cache · SIMD · benches' },    x: 540,  y: 486, w: 160, h: 58, kind: 'proj', status: 'done',  tier: 'eng',    href: '/vol6-performance/',         badge: 'V6' },
  { id: 'v7',    name: { cn: '卷七 · 工程', en: 'Vol.7 Engineering' },  sub: { cn: 'CMake · 工具链 · 调试', en: 'CMake · toolchain · debug' }, x: 820, y: 486, w: 160, h: 58, kind: 'proj', status: 'doing', tier: 'eng',   href: '/vol7-engineering/',         badge: 'V7' },
  { id: 'crash', name: { cn: '崩溃实验室', en: 'Crash Lab' },          sub: { cn: '真崩溃排查', en: 'Real crash forensics' },              x: 1090, y: 486, w: 140, h: 46, kind: 'sup',  status: 'doing', tier: 'domain', href: '/crash-lab/',                badge: 'LAB' },
  { id: 'v8',    name: { cn: '卷八 · 领域', en: 'Vol.8 Domains' },      sub: { cn: '嵌入式 · TinyML · 网络', en: 'Embedded · TinyML · net' }, x: 340, y: 640, w: 160, h: 58, kind: 'proj', status: 'doing', tier: 'domain', href: '/vol8-domains/',             badge: 'V8' },
  { id: 'v9',    name: { cn: '卷九 · 开源研读', en: 'Vol.9 Source Study' }, sub: { cn: 'Chromium 源码', en: 'Chromium internals' },         x: 620,  y: 640, w: 160, h: 58, kind: 'proj', status: 'doing', tier: 'domain', href: '/vol9-open-source-project-learn/', badge: 'V9' },
  { id: 'v10',   name: { cn: '卷十 · 演讲笔记', en: 'Vol.10 Talk Notes' }, sub: { cn: 'CppCon 二创', en: 'CppCon notes' },                 x: 890,  y: 640, w: 160, h: 58, kind: 'proj', status: 'doing', tier: 'domain', href: '/vol10-open-lecture-notes/',  badge: 'V10' },
  { id: 'proj',  name: { cn: '贯穿式项目', en: 'Capstone Projects' },   sub: { cn: '协程服务器 · INI 解析器', en: 'Coroutine server · INI' }, x: 1130, y: 640, w: 160, h: 58, kind: 'proj', status: 'doing', tier: 'domain', href: '/projects/',              badge: 'PRJ' },
  { id: 'community', name: { cn: '社区文章', en: 'Community' },         sub: { cn: '来稿与收录', en: 'Submissions' },                       x: 430,  y: 784, w: 140, h: 46, kind: 'sup',  status: 'doing', tier: 'domain', href: '/community/',                badge: 'COM' },
  { id: 'tags',  name: { cn: '标签索引', en: 'Tags' },                  sub: { cn: '按主题检索', en: 'Browse by topic' },                   x: 700,  y: 784, w: 140, h: 46, kind: 'sup',  status: 'doing', tier: 'domain', href: '/tags/',                      badge: 'IDX' },
]

export const HOME_PATH_EDGES: PathEdge[] = [
  // 主线：新手 → 基础 → 现代 → 标准库 → 高级 → 并发 → 性能 → 工程 → 领域 → 开源研读
  { from: 'gs', to: 'v1', kind: 'solid', route: { from: 'bottom', to: 'top', via: [{ x: 'from', y: 121 }, { x: 'to', y: 121 }] } },
  { from: 'v1', to: 'v2', kind: 'solid', route: { from: 'right', to: 'left' } },
  { from: 'v2', to: 'v3', kind: 'solid', route: { from: 'bottom', to: 'top', via: [{ x: 'from', y: 265 }, { x: 'to', y: 265 }] } },
  { from: 'v3', to: 'v4', kind: 'solid', route: { from: 'right', to: 'left' } },
  { from: 'v4', to: 'v5', kind: 'solid', route: { from: 'bottom', to: 'top', via: [{ x: 'from', y: 409 }, { x: 'to', y: 409 }] } },
  { from: 'v5', to: 'v6', kind: 'solid', route: { from: 'right', to: 'left' } },
  { from: 'v6', to: 'v7', kind: 'solid', route: { from: 'right', to: 'left' } },
  { from: 'v7', to: 'v8', kind: 'solid', route: { from: 'bottom', to: 'top', via: [{ x: 'from', y: 563 }, { x: 'to', y: 563 }] } },
  { from: 'v8', to: 'v9', kind: 'solid', route: { from: 'right', to: 'left' } },
  { from: 'comp', to: 'v7', kind: 'solid', route: { from: 'bottom', to: 'top', via: [{ x: 'from', y: 409 }, { x: 'to', y: 409 }] } },
  // 项目线绕左侧进入 B4/B5 间轨道，避免横穿卷七 → 卷八的主线。
  { from: 'v5', to: 'proj', kind: 'solid', route: { from: 'left', to: 'bottom', via: [{ x: 150, y: 'from' }, { x: 150, y: 710 }, { x: 'to', y: 710 }] } },
  // 按需选修（对应 roadmap 页路径 B/C：有 C 底子可跳读、按目标选题）
  { from: 'gs', to: 'v2', kind: 'dash', route: { from: 'bottom', to: 'top', via: [{ x: 'from', y: 129 }, { x: 'to', y: 129 }] } },
  { from: 'v2', to: 'v5', kind: 'dash', route: { from: 'bottom', to: 'top', via: [{ x: 'from', y: 257 }, { x: 230, y: 257 }, { x: 230, y: 409 }, { x: 'to', y: 409 }] } },
  { from: 'v2', to: 'v4', kind: 'dash', route: { from: 'bottom', to: 'top', via: [{ x: 'from', y: 273 }, { x: 'to', y: 273 }] } },
  { from: 'v2', to: 'comp', kind: 'dash', route: { from: 'right', to: 'left' } },
  // 同层隔点连接走节点下方，不能再从卷九节点背后穿过。
  { from: 'v8', to: 'v10', kind: 'dash', route: { from: 'bottom', to: 'bottom', via: [{ x: 'from', y: 692 }, { x: 'to', y: 692 }] } },
  // 支撑/索引：不进悬停高亮链
  { from: 'v2', to: 'crash', kind: 'dot', route: { from: 'right', to: 'top', via: [{ x: 750, y: 'from' }, { x: 750, y: 128 }, { x: 'to', y: 128 }] } },
  { from: 'v7', to: 'crash', kind: 'dot', route: { from: 'right', to: 'left' } },
  // 两条索引线分别使用左右外围轨道，避免贯穿中心节点。
  { from: 'gs', to: 'tags', kind: 'dot', route: { from: 'left', to: 'bottom', via: [{ x: 110, y: 'from' }, { x: 110, y: 820 }, { x: 'to', y: 820 }] } },
  { from: 'proj', to: 'community', kind: 'dot', route: { from: 'right', to: 'top', via: [{ x: 1240, y: 'from' }, { x: 1240, y: 726 }, { x: 'to', y: 726 }] } },
]
