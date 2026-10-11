/**
 * 「学习路线图」页数据源（roadmap 页三个组件共用，CN/EN 双语）。
 *
 * 唯一事实源原则：每卷的一句话定位、难度、前置、成熟状态都住这里，组件只管渲染。
 * 以后内容变动（新卷、状态翻绿、主题调整）只改这个文件，页面 prose 在 documents/roadmap/。
 *
 * 状态口径与 home-path-data.ts 对齐：done = 已成型（绿点）、doing = 在建·持续更新（黄点）。
 * 本文件多一个 planned（规划中），只在成熟度板出现——交互图上没有实体的东西不进图。
 * 状态改动必须先逐卷核实（目录盘点 + 各卷 index + AGENTS.md），不照旧数据改。
 */

export type Bi = { cn: string; en: string }

/** done：已成型；doing：在建·持续更新；planned：规划中（仅成熟度板使用） */
export type VolStatus = 'done' | 'doing' | 'planned'

export interface VolumeCard {
  /** 锚点 id，与路线图页详解区的 <a id> 一一对应 */
  id: string
  badge: string
  name: Bi
  /** 一句话定位 */
  tagline: Bi
  difficulty: Bi
  prereq: Bi
  status: Exclude<VolStatus, 'planned'>
  href: string
}

export interface RouteChip {
  label: Bi
  href: string
  /** core = 主线必经（实心胶囊）；opt = 按需选修（虚线胶囊） */
  kind: 'core' | 'opt'
}

export interface LearnRoute {
  id: string
  title: Bi
  /** 适合谁 */
  who: Bi
  chips: RouteChip[]
  note: Bi
}

export interface MaturityItem {
  label: Bi
  /** 一句「现在能读到什么」 */
  note: Bi
}

export interface MaturityGroup {
  key: VolStatus
  title: Bi
  items: MaturityItem[]
}

/* ─────────── 卷卡墙（顺序 = 详解区顺序：起步 → 卷一…卷十 → 编译与链接） ─────────── */

export const ROADMAP_CARDS: VolumeCard[] = [
  {
    id: 'gs',
    badge: 'GS',
    name: { cn: '新手起步', en: 'Getting Started' },
    tagline: { cn: '装好环境、跑通第一个程序，零基础的第一站', en: 'Set up the toolchain and run your first program' },
    difficulty: { cn: '入门', en: 'Beginner' },
    prereq: { cn: '无', en: 'None' },
    status: 'done',
    href: '/getting-started/',
  },
  {
    id: 'v1',
    badge: 'V1',
    name: { cn: '卷一 · 基础入门', en: 'Vol.1 Fundamentals' },
    tagline: { cn: '从零建立 C++ 知识体系，附完整 C 语言速通', en: 'Build C++ from scratch, with a full C refresher' },
    difficulty: { cn: '入门 → 中级', en: 'Beginner → Intermediate' },
    prereq: { cn: '无', en: 'None' },
    status: 'doing',
    href: '/vol1-fundamentals/',
  },
  {
    id: 'v2',
    badge: 'V2',
    name: { cn: '卷二 · 现代特性', en: 'Vol.2 Modern Features' },
    tagline: { cn: '移动语义、智能指针、constexpr，「会写」到「会写好」的分水岭', en: 'Move semantics, smart pointers, constexpr — the modern divide' },
    difficulty: { cn: '中级', en: 'Intermediate' },
    prereq: { cn: '卷一', en: 'Vol.1' },
    status: 'done',
    href: '/vol2-modern-features/',
  },
  {
    id: 'v3',
    badge: 'V3',
    name: { cn: '卷三 · 标准库深入', en: 'Vol.3 Standard Library' },
    tagline: { cn: '容器、字符串、迭代器与算法、IO、错误处理的底层机制', en: 'How containers, strings, iterators and IO really work' },
    difficulty: { cn: '中级', en: 'Intermediate' },
    prereq: { cn: '卷一、卷二', en: 'Vol.1–2' },
    status: 'done',
    href: '/vol3-standard-library/',
  },
  {
    id: 'v4',
    badge: 'V4',
    name: { cn: '卷四 · 高级主题', en: 'Vol.4 Advanced' },
    tagline: { cn: '协程、concepts、元编程、Ranges 与设计模式', en: 'Coroutines, concepts, metaprogramming, patterns' },
    difficulty: { cn: '高级', en: 'Advanced' },
    prereq: { cn: '卷二、卷三', en: 'Vol.2–3' },
    status: 'done',
    href: '/vol4-advanced/',
  },
  {
    id: 'v5',
    badge: 'V5',
    name: { cn: '卷五 · 并发编程', en: 'Vol.5 Concurrency' },
    tagline: { cn: '从线程原语到协程异步，动手 Lab 最重的一卷', en: 'From thread primitives to async coroutines, labs included' },
    difficulty: { cn: '中高', en: 'Upper-intermediate' },
    prereq: { cn: '卷一 ~ 卷四', en: 'Vol.1–4' },
    status: 'done',
    href: '/vol5-concurrency/',
  },
  {
    id: 'v6',
    badge: 'V6',
    name: { cn: '卷六 · 性能优化', en: 'Vol.6 Performance' },
    tagline: { cn: '从测量方法论到 CPU 微架构，按瓶颈调优', en: 'Measurement methodology to microarchitecture' },
    difficulty: { cn: '中高', en: 'Upper-intermediate' },
    prereq: { cn: '建议先读卷五', en: 'Vol.5 recommended' },
    status: 'done',
    href: '/vol6-performance/',
  },
  {
    id: 'v7',
    badge: 'V7',
    name: { cn: '卷七 · 工程实践', en: 'Vol.7 Engineering' },
    tagline: { cn: 'CMake、交叉编译、链接器与调试的工程落地', en: 'CMake, cross-compiling, linkers, debugging' },
    difficulty: { cn: '中级', en: 'Intermediate' },
    prereq: { cn: '建议先读编译与链接', en: 'Compile & Link first' },
    status: 'doing',
    href: '/vol7-engineering/',
  },
  {
    id: 'cmp',
    badge: 'CMP',
    name: { cn: '编译与链接深入', en: 'Compile & Link' },
    tagline: { cn: '编译、链接、符号与库的底层机制，卷七的前置', en: 'How compiling, linking and symbols work' },
    difficulty: { cn: '中级', en: 'Intermediate' },
    prereq: { cn: 'C++ 基础', en: 'C++ basics' },
    status: 'done',
    href: '/compilation/',
  },
  {
    id: 'v8',
    badge: 'V8',
    name: { cn: '卷八 · 领域应用', en: 'Vol.8 Domains' },
    tagline: { cn: '系统编程、嵌入式、TinyML、网络等垂直领域实战', en: 'Systems programming, embedded, TinyML, networking' },
    difficulty: { cn: '中级', en: 'Intermediate' },
    prereq: { cn: '卷一 ~ 卷七', en: 'Vol.1–7' },
    status: 'doing',
    href: '/vol8-domains/',
  },
  {
    id: 'v9',
    badge: 'V9',
    name: { cn: '卷九 · 开源项目学习', en: 'Vol.9 Source Study' },
    tagline: { cn: '拆解 Chromium 工业级源码，学真实世界的 C++ 设计', en: 'Read production-grade Chromium internals' },
    difficulty: { cn: '中高', en: 'Upper-intermediate' },
    prereq: { cn: '建议先读卷四、卷五', en: 'Vol.4–5 recommended' },
    status: 'doing',
    href: '/vol9-open-source-project-learn/',
  },
  {
    id: 'v10',
    badge: 'V10',
    name: { cn: '卷十 · 课程与演讲笔记', en: 'Vol.10 Talk Notes' },
    tagline: { cn: 'CppCon 等顶级演讲的阅读笔记与二次创作', en: 'CppCon talk notes, rewritten and verified' },
    difficulty: { cn: '中级', en: 'Intermediate' },
    prereq: { cn: '卷一 ~ 卷五', en: 'Vol.1–5' },
    status: 'doing',
    href: '/vol10-open-lecture-notes/',
  },
]

/* ─────────── 按背景选起点（三条路径） ─────────── */

export const ROADMAP_ROUTES: LearnRoute[] = [
  {
    id: 'a',
    title: { cn: '路径 A · 零基础 / 仅会 C', en: 'Route A · New to programming / C only' },
    who: { cn: '没写过程序，或者只会 C、想转现代 C++', en: 'Never programmed, or coming from C', },
    chips: [
      { label: { cn: '新手起步', en: 'Getting Started' }, href: '/getting-started/', kind: 'core' },
      { label: { cn: '卷一', en: 'Vol.1' }, href: '/vol1-fundamentals/', kind: 'core' },
      { label: { cn: '卷二', en: 'Vol.2' }, href: '/vol2-modern-features/', kind: 'core' },
      { label: { cn: '卷三', en: 'Vol.3' }, href: '/vol3-standard-library/', kind: 'core' },
      { label: { cn: '卷四', en: 'Vol.4' }, href: '/vol4-advanced/', kind: 'core' },
      { label: { cn: '卷五', en: 'Vol.5' }, href: '/vol5-concurrency/', kind: 'core' },
      { label: { cn: '卷六', en: 'Vol.6' }, href: '/vol6-performance/', kind: 'core' },
      { label: { cn: '卷七', en: 'Vol.7' }, href: '/vol7-engineering/', kind: 'core' },
      { label: { cn: '卷八', en: 'Vol.8' }, href: '/vol8-domains/', kind: 'core' },
    ],
    note: {
      cn: '沿主线一卷一卷走最稳。有编程底子的话，卷一的 C 速通可以快速过，重点啃值类别、面向对象与模板初步。',
      en: 'Walk the main line volume by volume. If you already code, skim the C refresher in Vol.1 and focus on value categories, OOP and templates.',
    },
  },
  {
    id: 'b',
    title: { cn: '路径 B · 有 C 或嵌入式经验', en: 'Route B · C / embedded background' },
    who: { cn: '语法底子够，想补现代 C++ 写法、直奔系统与嵌入式实战', en: 'Solid syntax, want modern C++ and systems practice' },
    chips: [
      { label: { cn: '卷二', en: 'Vol.2' }, href: '/vol2-modern-features/', kind: 'core' },
      { label: { cn: '编译与链接', en: 'Compile & Link' }, href: '/compilation/', kind: 'core' },
      { label: { cn: '卷八', en: 'Vol.8' }, href: '/vol8-domains/', kind: 'core' },
      { label: { cn: '卷五', en: 'Vol.5' }, href: '/vol5-concurrency/', kind: 'opt' },
      { label: { cn: '卷六', en: 'Vol.6' }, href: '/vol6-performance/', kind: 'opt' },
      { label: { cn: '卷七', en: 'Vol.7' }, href: '/vol7-engineering/', kind: 'opt' },
    ],
    note: {
      cn: '卷二补齐现代写法后直进卷八：系统编程线不需要开发板，纯电脑就能跟；嵌入式线建议先把编译与链接过一遍再做交叉编译。并发、性能、工程化按需补。',
      en: 'Modernize in Vol.2, then jump into Vol.8: the systems-programming track needs no dev board. Read Compile & Link before cross-compiling. Concurrency, performance and engineering as needed.',
    },
  },
  {
    id: 'c',
    title: { cn: '路径 C · 已会 C++', en: 'Route C · Already know C++' },
    who: { cn: '语言已经顺手，按目标直取专题', en: 'Fluent in C++, pick topics by goal' },
    chips: [
      { label: { cn: '卷四', en: 'Vol.4' }, href: '/vol4-advanced/', kind: 'opt' },
      { label: { cn: '卷五', en: 'Vol.5' }, href: '/vol5-concurrency/', kind: 'opt' },
      { label: { cn: '卷六', en: 'Vol.6' }, href: '/vol6-performance/', kind: 'opt' },
      { label: { cn: '卷七', en: 'Vol.7' }, href: '/vol7-engineering/', kind: 'opt' },
      { label: { cn: '卷九', en: 'Vol.9' }, href: '/vol9-open-source-project-learn/', kind: 'opt' },
      { label: { cn: '卷十', en: 'Vol.10' }, href: '/vol10-open-lecture-notes/', kind: 'opt' },
    ],
    note: {
      cn: '要并发与异步读卷五，要性能读卷六，要工程化读卷七，要前沿特性、元编程与设计模式读卷四，想读工业级源码读卷九，想跟顶级演讲读卷十。',
      en: 'Concurrency → Vol.5, performance → Vol.6, engineering → Vol.7, cutting-edge features and patterns → Vol.4, source study → Vol.9, talks → Vol.10.',
    },
  },
]

/* ─────────── 成熟度板（三组） ─────────── */

export const ROADMAP_MATURITY: MaturityGroup[] = [
  {
    key: 'done',
    title: { cn: '已成型', en: 'Established' },
    items: [
      { label: { cn: '新手起步', en: 'Getting Started' }, note: { cn: '环境搭建到第一个程序，配图齐全', en: 'Setup to first program, fully illustrated' } },
      { label: { cn: '卷二 · 现代特性', en: 'Vol.2 Modern Features' }, note: { cn: '十二章全就位，正在逐篇打磨', en: 'All 12 chapters in, being polished' } },
      { label: { cn: '卷三 · 标准库深入', en: 'Vol.3 Standard Library' }, note: { cn: '容器、字符串、迭代器与算法、IO、时间数值、错误处理六个子方向完工', en: 'Six sub-tracks complete' } },
      { label: { cn: '卷四 · 高级主题', en: 'Vol.4 Advanced' }, note: { cn: '协程、concepts、元编程、Ranges、设计模式就位', en: 'Coroutines, concepts, metaprogramming, patterns' } },
      { label: { cn: '卷五 · 并发编程', en: 'Vol.5 Concurrency' }, note: { cn: '十章加练习区，Lab 0/1 脚手架可直接上手', en: 'Ten chapters plus exercises, Labs 0–1 ready' } },
      { label: { cn: '卷六 · 性能优化', en: 'Vol.6 Performance' }, note: { cn: '测量方法论八章重构完成', en: 'Rebuilt as an 8-chapter methodology' } },
      { label: { cn: '编译与链接深入', en: 'Compile & Link' }, note: { cn: '全卷完成，卷七的前置', en: 'Complete; prerequisite for Vol.7' } },
    ],
  },
  {
    key: 'doing',
    title: { cn: '在建 · 持续更新', en: 'In progress' },
    items: [
      { label: { cn: '卷一 · 基础入门', en: 'Vol.1 Fundamentals' }, note: { cn: '正在重写为全栈入门，章节可能微调', en: 'Being rewritten; chapters may shift' } },
      { label: { cn: '卷七 · 工程实践', en: 'Vol.7 Engineering' }, note: { cn: '主干章节就位，实战线推进中', en: 'Core chapters in, hands-on track ongoing' } },
      { label: { cn: '卷八 · 领域应用', en: 'Vol.8 Domains' }, note: { cn: '系统编程已完工；嵌入式、TinyML、网络、指针深潜、mini-STL 推进中', en: 'Systems programming done; embedded, TinyML, net, mini-STL ongoing' } },
      { label: { cn: '卷九 · 开源项目学习', en: 'Vol.9 Source Study' }, note: { cn: 'Chromium 四个组件研读完成', en: 'Four Chromium studies done' } },
      { label: { cn: '卷十 · 课程与演讲笔记', en: 'Vol.10 Talk Notes' }, note: { cn: 'CppCon 2025 七场完工，持续加场', en: 'Seven CppCon 2025 talks, more coming' } },
      { label: { cn: '每周一些题', en: 'Weekly Problems' }, note: { cn: '一周一包题，持续出题中', en: 'One problem pack per week' } },
      { label: { cn: '崩溃实验室', en: 'Crash Lab' }, note: { cn: '内存安全篇就位，继续添新案', en: 'Memory-safety cases in, more coming' } },
    ],
  },
  {
    key: 'planned',
    title: { cn: '规划中', en: 'Planned' },
    items: [
      { label: { cn: '卷八更多子域', en: 'More Vol.8 domains' }, note: { cn: '算法、数据存储、GUI、测试', en: 'Algorithms, storage, GUI, testing' } },
      { label: { cn: '卷五 Lab 2–5 脚手架', en: 'Vol.5 Labs 2–5 scaffolds' }, note: { cn: '手册文字已就位，脚手架代码待补', en: 'Handbook ready, scaffolds to come' } },
      { label: { cn: '卷九更多项目', en: 'More Vol.9 projects' }, note: { cn: '在 Chromium 之外挑选新目标', en: 'New targets beyond Chromium' } },
    ],
  },
]

/* ─────────── 组件内微文案（双语） ─────────── */

export const ROADMAP_UI = {
  enter: { cn: '进入', en: 'Open' } as Bi,
  detail: { cn: '本页详解', en: 'Details below' } as Bi,
  difficultyLabel: { cn: '难度', en: 'Level' } as Bi,
  prereqLabel: { cn: '前置', en: 'Prereq' } as Bi,
  doneLabel: { cn: '已成型', en: 'Established' } as Bi,
  doingLabel: { cn: '在建 · 持续更新', en: 'In progress' } as Bi,
}
