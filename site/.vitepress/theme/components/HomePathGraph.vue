<script setup lang="ts">
/**
 * 首页「学习路径图」：SVG 交互图（HomePathExplorer 的视图一）。
 *
 * 交互图骨架移植自 Awesome-Embedded 的 RoadmapGraph.vue（PCB 走线 / 悬停链高亮 /
 * 拖拽 + localStorage 记忆 / suppressClick 防拖完误跳），按本项目改造：
 * - 双语：节点/层带文案走 home-path-data.ts 的 Bi；EN 链接 = CN href 补 /en 前缀。
 * - 状态：以右上角状态点（done 绿 / doing 黄）替代母本的 GitHub star。
 * - 配色：节点复用首页卡片的中性描边 + tier rail，主线/选修/支撑只使用站点既有
 *   brand/text/border 色阶；暗色模式零额外工作。
 * - 新增 panzoom 缩放平移（@panzoom/panzoom，按 MermaidLightbox 范式 onMounted 动态
 *   import，不进 SSR bundle）；Ctrl/⌘+滚轮缩放，普通滚轮仍归页面滚动。
 * - 三层手势分工：拖节点（pointerdown 上 stopPropagation 掐断 panzoom）｜
 *   拖画布空白（panzoom）｜翻页（外层 pager，图内横滑归图不翻页，tab 兜底）。
 * - 坐标换算：panzoom 的 CSS transform 在祖先链上，getScreenCTM 在部分浏览器不反映，
 *   改用 bounding-rect 线性映射（panzoom 只有均匀 scale+translate，数学严格成立）。
 */
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { useData, useRouter, withBase } from 'vitepress'
import {
  HOME_GRAPH_REVISION,
  HOME_PATH_BANDS,
  HOME_PATH_EDGES,
  HOME_PATH_NODES,
  VB_H,
  VB_W,
  type Bi,
  type PathEdge,
  type PathNode,
  type PathRouteCoord,
  type PathSide,
} from '../home-path-data'

/* ─────────── 双语 ─────────── */
const { lang } = useData()
const router = useRouter()
const isEn = computed(() => lang.value.startsWith('en'))
const L = (b: Bi) => (isEn.value ? b.en : b.cn)

const t = {
  reset: computed(() => (isEn.value ? 'Reset layout' : '重置布局')),
  fit: computed(() => (isEn.value ? 'Fit view' : '复位缩放')),
  zin: computed(() => (isEn.value ? 'Zoom in' : '放大')),
  zout: computed(() => (isEn.value ? 'Zoom out' : '缩小')),
  lgSolid: computed(() => (isEn.value ? 'Main path' : '建议主线')),
  lgDash: computed(() => (isEn.value ? 'On demand' : '按需选修')),
  lgDot: computed(() => (isEn.value ? 'Support / index' : '支撑 · 索引')),
  lgDone: computed(() => (isEn.value ? 'Complete' : '已成型')),
  lgDoing: computed(() => (isEn.value ? 'In progress' : '在建 · 持续更新')),
}

/* ─────────── 节点状态（SSG 直出种子值，客户端可拖） ─────────── */
interface LiveNode extends PathNode {}
const nodes: LiveNode[] = reactive(HOME_PATH_NODES.map((n) => ({ ...n })))
const byId = new Map(nodes.map((n) => [n.id, n]))

/* 父链（悬停高亮用）：只依 solid + dash 建立；dot 是支撑/索引，不在学习链上 */
const PARENT: Record<string, string> = {}
for (const e of HOME_PATH_EDGES) {
  if (e.kind === 'solid' || e.kind === 'dash') {
    if (!PARENT[e.to]) PARENT[e.to] = e.from
  }
}

/* ─────────── 分轨走线：正交 + 45° 倒角 ─────────── */
interface Pt {
  x: number
  y: number
}
function autoAnchors(f: LiveNode, tn: LiveNode): [Pt, Pt] {
  const below = tn.y > f.y + 34
  const above = tn.y < f.y - 34
  if (below) return [{ x: f.x, y: f.y + f.h / 2 }, { x: tn.x, y: tn.y - tn.h / 2 }]
  if (above) return [{ x: f.x, y: f.y - f.h / 2 }, { x: tn.x, y: tn.y + tn.h / 2 }]
  const right = tn.x > f.x
  return right
    ? [{ x: f.x + f.w / 2, y: f.y }, { x: tn.x - tn.w / 2, y: tn.y }]
    : [{ x: f.x - f.w / 2, y: f.y }, { x: tn.x + tn.w / 2, y: tn.y }]
}

function anchor(n: LiveNode, side: PathSide): Pt {
  if (side === 'top') return { x: n.x, y: n.y - n.h / 2 }
  if (side === 'right') return { x: n.x + n.w / 2, y: n.y }
  if (side === 'bottom') return { x: n.x, y: n.y + n.h / 2 }
  return { x: n.x - n.w / 2, y: n.y }
}

function resolveCoord(value: PathRouteCoord, axis: 'x' | 'y', from: Pt, to: Pt): number {
  if (value === 'from') return from[axis]
  if (value === 'to') return to[axis]
  return value
}

function samePoint(a: Pt, b: Pt): boolean {
  return Math.abs(a.x - b.x) < 0.01 && Math.abs(a.y - b.y) < 0.01
}

/** 删除重复点和共线中间点，避免共享轨道生成零长度倒角。 */
function compactPoints(points: Pt[]): Pt[] {
  const unique = points.filter((p, i) => i === 0 || !samePoint(p, points[i - 1]))
  return unique.filter((p, i) => {
    if (i === 0 || i === unique.length - 1) return true
    const a = unique[i - 1]
    const c = unique[i + 1]
    return !((a.x === p.x && p.x === c.x) || (a.y === p.y && p.y === c.y))
  })
}

/** 把正交折线的直角切成短 45° 倒角；节点拖动后也能安全退化为普通折线。 */
function bevelPath(points: Pt[]): string {
  if (points.length < 2) return ''
  const commands = [`M${points[0].x} ${points[0].y}`]
  for (let i = 1; i < points.length - 1; i++) {
    const prev = points[i - 1]
    const cur = points[i]
    const next = points[i + 1]
    const inLen = Math.hypot(cur.x - prev.x, cur.y - prev.y)
    const outLen = Math.hypot(next.x - cur.x, next.y - cur.y)
    if (inLen < 1 || outLen < 1) continue
    const cut = Math.min(12, inLen / 2, outLen / 2)
    const before = {
      x: cur.x - ((cur.x - prev.x) / inLen) * cut,
      y: cur.y - ((cur.y - prev.y) / inLen) * cut,
    }
    const after = {
      x: cur.x + ((next.x - cur.x) / outLen) * cut,
      y: cur.y + ((next.y - cur.y) / outLen) * cut,
    }
    commands.push(`L${before.x} ${before.y}`, `L${after.x} ${after.y}`)
  }
  const last = points[points.length - 1]
  commands.push(`L${last.x} ${last.y}`)
  return commands.join(' ')
}

function route(edge: PathEdge, f: LiveNode, tn: LiveNode): { d: string } {
  let from: Pt
  let to: Pt
  let points: Pt[]

  if (edge.route) {
    from = anchor(f, edge.route.from)
    to = anchor(tn, edge.route.to)
    const middle = (edge.route.via ?? []).map((p) => ({
      x: resolveCoord(p.x, 'x', from, to),
      y: resolveCoord(p.y, 'y', from, to),
    }))
    points = compactPoints([from, ...middle, to])
  } else {
    ;[from, to] = autoAnchors(f, tn)
    const vertical = Math.abs(to.y - from.y) > 24
    points = vertical
      ? compactPoints([from, { x: from.x, y: (from.y + to.y) / 2 }, { x: to.x, y: (from.y + to.y) / 2 }, to])
      : compactPoints([from, { x: (from.x + to.x) / 2, y: from.y }, { x: (from.x + to.x) / 2, y: to.y }, to])
  }

  return { d: bevelPath(points) }
}

/* 支撑线先画、主线最后画：共享一段轨道时，主线视觉优先，不会被虚线切碎。 */
const EDGE_DRAW_ORDER = { dot: 0, dash: 1, solid: 2 } as const
const routedEdges = computed(() =>
  HOME_PATH_EDGES.map((e) => {
    const f = byId.get(e.from)!
    const tn = byId.get(e.to)!
    return { ...e, ...route(e, f, tn) }
  }).sort((a, b) => EDGE_DRAW_ORDER[a.kind] - EDGE_DRAW_ORDER[b.kind]),
)

/* ─────────── 悬停高亮：起点 → 当前节点整条链点亮，其余压暗 ─────────── */
const hoverId = ref<string | null>(null)
const chain = computed<Set<string> | null>(() => {
  if (!hoverId.value) return null
  const set = new Set<string>([hoverId.value])
  let cur = hoverId.value
  while (PARENT[cur]) {
    cur = PARENT[cur]
    set.add(cur)
  }
  return set
})
function inChain(id: string): boolean {
  return !chain.value || chain.value.has(id)
}
function edgeHot(from: string, to: string): boolean {
  return !!chain.value && chain.value.has(to) && chain.value.has(from)
}

/* ─────────── tier 配色（CSS 变量挂到节点上，描边/引脚/文字共用） ─────────── */
function tierColor(n: LiveNode): string {
  if (n.kind === 'sup') return 'var(--vp-c-text-3)'
  if (n.tier === 'core') return 'var(--vp-c-brand-1)'
  if (n.tier === 'eng') return 'var(--vp-c-green-1)'
  return 'var(--vp-c-purple-1)'
}

function nodeHref(n: LiveNode): string {
  return withBase(isEn.value ? `/en${n.href}` : n.href)
}

/* ─────────── 拖拽 + 布局记忆（localStorage 仅客户端；版本化 key 防脏数据） ─────────── */
const LAYOUT_KEY = `tamcpp-home-graph-v${HOME_GRAPH_REVISION}`
const svgEl = ref<SVGSVGElement | null>(null)

/* panzoom 的 transform 在祖先 div 上，getScreenCTM 不可靠；bounding-rect 线性映射
   （panzoom 只有均匀 scale+translate，无旋转，映射严格成立） */
function svgXY(ev: PointerEvent): { x: number; y: number } {
  const r = svgEl.value!.getBoundingClientRect()
  return {
    x: ((ev.clientX - r.left) / r.width) * VB_W,
    y: ((ev.clientY - r.top) / r.height) * VB_H,
  }
}

/* 画布钳制：节点拖出/存档漂出 viewBox 时拉回，留 16px 边距 */
function clampToCanvas(n: LiveNode) {
  n.x = Math.min(Math.max(n.x, n.w / 2 + 16), VB_W - n.w / 2 - 16)
  n.y = Math.min(Math.max(n.y, n.h / 2 + 16), VB_H - n.h / 2 - 16)
}

let dragId: string | null = null
let dragOff: { x: number; y: number } | null = null
let saveTimer: ReturnType<typeof setTimeout> | null = null

/* 拖拽误触抑制（双保险）：位移超 4px（真拖拽）或按住超过 350ms（长按抓取但
   手指没怎么动）都按拖拽意图处理，click 时吞掉——否则松手时浏览器把长按
   判成轻点，直接跟进节点链接。正常快 tap（<350ms 且几乎没动）照常跳转。 */
let downScreen: { x: number; y: number } | null = null
let downAt = 0
let suppressClick = false
const HOLD_MS = 350

function onPointerDown(ev: PointerEvent, n: LiveNode) {
  /* 掐断冒泡：panzoom 的 pointerdown 挂在外层 .hpg-pan 上，
     不掐断的话拖节点会同时平移画布 */
  ev.stopPropagation()
  dragId = n.id
  const p = svgXY(ev)
  dragOff = { x: p.x - n.x, y: p.y - n.y }
  downScreen = { x: ev.clientX, y: ev.clientY }
  downAt = performance.now()
  suppressClick = false
  ;(ev.currentTarget as Element).setPointerCapture(ev.pointerId)
}
function onPointerMove(ev: PointerEvent, n: LiveNode) {
  if (!dragId || dragId !== n.id || !dragOff) return
  if (
    downScreen &&
    Math.hypot(ev.clientX - downScreen.x, ev.clientY - downScreen.y) > 4
  ) {
    suppressClick = true
  }
  const p = svgXY(ev)
  n.x = p.x - dragOff.x
  n.y = p.y - dragOff.y
  clampToCanvas(n)
}
/* 导航决策完全由本函数接管(见模板 vp-raw 注释):干净轻点 → 走 SPA 路由,
   拖拽 / 长按(≥HOLD_MS)/ 手指没动够 4px 的长按 → 吞掉不跳。 */
function onClickGuard(ev: MouseEvent, n: LiveNode) {
  /* 修饰键 / 非左键:交还浏览器原生行为(新标签页打开等) */
  if (ev.metaKey || ev.ctrlKey || ev.shiftKey || ev.altKey || ev.button !== 0) return

  ev.preventDefault()

  /* 键盘 Enter 触发的 click(detail=0,无 pointerdown 前置):直接放行 */
  if (ev.detail === 0) {
    router.go(nodeHref(n))
    return
  }

  const heldLong = performance.now() - downAt > HOLD_MS
  if (!suppressClick && !heldLong) router.go(nodeHref(n))

  suppressClick = false
}
function onPointerUp() {
  if (!dragId) return
  dragId = null
  dragOff = null
  scheduleSave()
}
function scheduleSave() {
  if (saveTimer) clearTimeout(saveTimer)
  saveTimer = setTimeout(saveLayout, 200)
}
function saveLayout() {
  try {
    const data: Record<string, { x: number; y: number }> = {}
    for (const n of nodes) data[n.id] = { x: Math.round(n.x), y: Math.round(n.y) }
    localStorage.setItem(LAYOUT_KEY, JSON.stringify(data))
  } catch {
    /* storage 不可用时静默放弃记忆 */
  }
}
function loadLayout() {
  try {
    const raw = localStorage.getItem(LAYOUT_KEY)
    if (!raw) return
    const data = JSON.parse(raw) as Record<string, { x: number; y: number }>
    for (const n of nodes) {
      const saved = data[n.id]
      if (saved && Number.isFinite(saved.x) && Number.isFinite(saved.y)) {
        n.x = saved.x
        n.y = saved.y
        clampToCanvas(n)
      }
    }
  } catch {
    /* 记忆损坏则用种子布局 */
  }
}
function resetLayout() {
  HOME_PATH_NODES.forEach((seed, i) => {
    nodes[i].x = seed.x
    nodes[i].y = seed.y
  })
  try {
    localStorage.removeItem(LAYOUT_KEY)
  } catch {
    /* ignore */
  }
}

/* ─────────── panzoom：缩放平移（动态 import，不进 SSR/首屏 chunk） ─────────── */
/* 本地最小类型接口，避免静态 import 类型把库拽进 bundle（MermaidLightbox 同款手法） */
interface PanzoomInstance {
  zoomIn: (opts?: unknown) => void
  zoomOut: (opts?: unknown) => void
  reset: (opts?: unknown) => void
  zoomWithWheel: (event: WheelEvent) => void
  destroy: () => void
  on: (ev: 'panzoomchange', cb: (e: { detail?: { scale?: number } }) => void) => void
}

const viewportEl = ref<HTMLElement | null>(null)
const panEl = ref<HTMLElement | null>(null)
let pz: PanzoomInstance | null = null
let wheelHandler: ((e: WheelEvent) => void) | null = null
const isMobile = ref(false)

function fitView() {
  pz?.reset()
}

onMounted(async () => {
  loadLayout()

  isMobile.value = window.matchMedia('(max-width: 767px)').matches

  if (panEl.value && viewportEl.value) {
    const { default: createPanzoom } = await import('@panzoom/panzoom')
    pz = createPanzoom(panEl.value, {
      maxScale: 8,
      minScale: 0.4,
      step: 0.3,
      cursor: 'grab',
      animate: false,
      /* 移动端默认放大到节点文字可读的档位，桌面 1:1 整图浏览 */
      startScale: isMobile.value ? 2.2 : 1,
      touchAction: 'none',
    })
    /* 合作式触摸：缩放比例回到 ≤1 时把触摸还给页面竖滚（pan-y），
       放大状态下图内自由平移（none）——Google Maps embed 的手势分工 */
    pz.on('panzoomchange', (e) => {
      const s = e.detail?.scale ?? 1
      if (panEl.value) panEl.style.touchAction = s > 1.01 ? 'none' : 'pan-y'
    })
    wheelHandler = (e: WheelEvent) => {
      /* 只在 Ctrl/⌘+滚轮时缩放（浏览器缩放页面的手势语义）；普通滚轮归页面 */
      if (!(e.ctrlKey || e.metaKey)) return
      e.preventDefault()
      pz?.zoomWithWheel(e)
    }
    viewportEl.value.addEventListener('wheel', wheelHandler, { passive: false })
  }
})

onBeforeUnmount(() => {
  if (saveTimer) clearTimeout(saveTimer)
  if (wheelHandler && viewportEl.value) {
    viewportEl.value.removeEventListener('wheel', wheelHandler)
  }
  pz?.destroy()
  pz = null
})
</script>

<template>
  <!-- vp-raw:VitePress 路由器把 click 监听挂在 window 捕获阶段,会先于任何节点级
       处理器抢走 <a> 点击做 SPA 跳转(节点上的 preventDefault 排不上队),拖完/长按
       松手照样跳页。vp-raw 让路由器放过本容器内的链接,导航权交回 onClickGuard。 -->
  <div class="hpg-wrap vp-raw">
    <div class="hpg-ctl">
      <button type="button" :title="t.zout.value" :aria-label="t.zout.value" @click="pz?.zoomOut()">−</button>
      <button type="button" :title="t.zin.value" :aria-label="t.zin.value" @click="pz?.zoomIn()">+</button>
      <button type="button" @click="fitView">⤢ {{ t.fit.value }}</button>
      <button type="button" @click="resetLayout">⟲ {{ t.reset.value }}</button>
    </div>

    <div ref="viewportEl" class="hpg-viewport">
      <div ref="panEl" class="hpg-pan">
        <svg
          ref="svgEl"
          class="hpg-svg"
          :viewBox="`0 0 ${VB_W} ${VB_H}`"
          xmlns="http://www.w3.org/2000/svg"
        >
          <!-- 层带 -->
          <g v-for="(b, i) in HOME_PATH_BANDS" :key="`band-${i}`">
            <rect
              class="hpg-band"
              x="14"
              :y="b.top"
              width="1302"
              :height="b.bottom - b.top"
              rx="10"
            />
            <text class="hpg-band-label" x="26" :y="b.top + 18">{{ L(b.label) }}</text>
          </g>

          <!-- 边：支撑 / 选修 / 主线按视觉优先级依次叠放 -->
          <g v-for="e in routedEdges" :key="`${e.from}-${e.to}`" :data-from="e.from" :data-to="e.to">
            <path
              class="hpg-edge"
              :class="[`hpg-edge--${e.kind}`, { 'hpg-edge--hot': edgeHot(e.from, e.to) }]"
              :d="e.d"
            />
          </g>

          <!-- 节点 -->
          <component
            :is="'a'"
            v-for="n in nodes"
            :key="n.id"
            :href="nodeHref(n)"
            :data-node-id="n.id"
            class="hpg-node"
            :class="{ 'hpg-dim': !inChain(n.id) }"
            :style="{ '--tier-c': tierColor(n) }"
            :transform="`translate(${n.x - n.w / 2},${n.y - n.h / 2})`"
            @pointerdown="onPointerDown($event, n)"
            @pointermove="onPointerMove($event, n)"
            @pointerup="onPointerUp"
            @pointercancel="onPointerUp"
            @click="onClickGuard($event, n)"
            @pointerenter="hoverId = n.id"
            @pointerleave="hoverId = null"
          >
            <rect
              class="hpg-chip"
              :class="[`hpg-chip--${n.kind}`]"
              x="0"
              y="0"
              :width="n.w"
              :height="n.h"
              :rx="n.kind === 'sup' ? 8 : 10"
            />
            <line
              v-if="n.kind !== 'root'"
              class="hpg-rail"
              x1="2"
              :y1="n.kind === 'sup' ? 9 : 11"
              x2="2"
              :y2="n.h - (n.kind === 'sup' ? 9 : 11)"
            />
            <text
              class="hpg-name"
              :class="{ 'hpg-name--onroot': n.kind === 'root' }"
              :x="n.w / 2"
              :y="n.kind === 'sup' ? 28 : 25"
              text-anchor="middle"
            >{{ L(n.name) }}</text>
            <text
              v-if="n.kind !== 'sup'"
              class="hpg-sub"
              :class="{ 'hpg-sub--onroot': n.kind === 'root' }"
              :x="n.w / 2"
              y="43"
              text-anchor="middle"
            >{{ L(n.sub) }}</text>
            <text
              class="hpg-badge"
              :class="{ 'hpg-badge--onroot': n.kind === 'root' }"
              x="8"
              y="15"
            >{{ n.badge }}</text>
            <!-- 状态点：成型=绿 / 在建·持续=黄（替代母本的 star） -->
            <circle
              class="hpg-dot"
              :class="`hpg-dot--${n.status}`"
              :cx="n.w - 10"
              cy="12"
              r="3.5"
            />
            <circle v-if="n.kind === 'root'" class="hpg-here" cx="-16" :cy="n.h / 2" r="5" />
          </component>
        </svg>
      </div>
    </div>

    <div class="hpg-legend">
      <span><svg width="26" height="8" viewBox="0 0 26 8"><line x1="0" y1="4" x2="26" y2="4" class="hpg-edge hpg-edge--solid" /></svg>{{ t.lgSolid.value }}</span>
      <span><svg width="26" height="8" viewBox="0 0 26 8"><line x1="0" y1="4" x2="26" y2="4" class="hpg-edge hpg-edge--dash" /></svg>{{ t.lgDash.value }}</span>
      <span><svg width="26" height="8" viewBox="0 0 26 8"><line x1="0" y1="4" x2="26" y2="4" class="hpg-edge hpg-edge--dot" /></svg>{{ t.lgDot.value }}</span>
      <span><svg width="10" height="10" viewBox="0 0 10 10"><circle cx="5" cy="5" r="3.5" class="hpg-dot--done" /></svg>{{ t.lgDone.value }}</span>
      <span><svg width="10" height="10" viewBox="0 0 10 10"><circle cx="5" cy="5" r="3.5" class="hpg-dot--doing" /></svg>{{ t.lgDoing.value }}</span>
    </div>
  </div>
</template>

<style scoped>
/* ── 图卡（外壳/标题/tab 由 HomePathExplorer 提供） ── */
.hpg-wrap {
  --hpg-main: color-mix(in srgb, var(--vp-c-brand-1) 72%, var(--vp-c-text-3));
  --hpg-optional: color-mix(in srgb, var(--vp-c-brand-1) 48%, var(--vp-c-border));
  --hpg-support: color-mix(in srgb, var(--vp-c-text-3) 68%, var(--vp-c-border));
  position: relative;
  border: 1px solid var(--vp-c-divider);
  border-radius: 14px;
  background: var(--vp-c-bg);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.04), 0 1px 2px rgba(0, 0, 0, 0.06);
  overflow: hidden;
  font-family: var(--vp-font-family);
}

.hpg-ctl {
  position: absolute;
  top: 12px;
  right: 12px;
  display: flex;
  gap: 6px;
  z-index: 5;
}
.hpg-ctl button {
  font-family: var(--vp-font-family);
  font-size: 12.5px;
  font-weight: 500;
  line-height: 1;
  padding: 7px 11px;
  border-radius: 9px;
  border: 1px solid var(--vp-c-border);
  background: var(--vp-c-bg);
  color: var(--vp-c-text-2);
  cursor: pointer;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.04);
  transition: border-color 0.2s ease, color 0.2s ease, background-color 0.2s ease;
}
.hpg-ctl button:hover {
  border-color: var(--vp-c-brand-1);
  color: var(--vp-c-brand-1);
  background: var(--vp-c-brand-soft);
}

/* ── 视口：桌面按 SVG 宽高比整图显示；缩放/平移溢出裁掉 ── */
.hpg-viewport {
  overflow: hidden;
  cursor: grab;
}
.hpg-viewport:active {
  cursor: grabbing;
}
.hpg-pan {
  width: 100%;
}
.hpg-svg {
  display: block;
  width: 100%;
  height: auto;
  user-select: none;
  -webkit-user-select: none;
}

/* ── 图内元素 ── */
.hpg-band {
  fill: var(--vp-c-bg-soft);
  opacity: 0.62;
}
.hpg-band-label {
  font-family: var(--vp-font-family);
  font-size: 10.5px;
  font-weight: 600;
  fill: var(--vp-c-text-3);
  letter-spacing: 0.7px;
}
.hpg-edge {
  fill: none;
  vector-effect: non-scaling-stroke;
  transition: opacity 0.2s, stroke-width 0.2s;
}
.hpg-edge--solid {
  stroke: var(--hpg-main);
  stroke-width: 1.7;
  opacity: 0.82;
}
.hpg-edge--dash {
  stroke: var(--hpg-optional);
  stroke-width: 1.45;
  opacity: 0.8;
  stroke-dasharray: 7 6;
}
.hpg-edge--dot {
  stroke: var(--hpg-support);
  stroke-width: 1.3;
  opacity: 0.72;
  stroke-dasharray: 1 6;
  stroke-linecap: round;
}
.hpg-edge--hot {
  opacity: 1;
  stroke-width: 2.35;
  filter: drop-shadow(0 0 3px var(--vp-c-brand-soft-2));
}
.hpg-node {
  cursor: grab;
  transition: opacity 0.2s;
}
.hpg-node:active {
  cursor: grabbing;
}
.hpg-node.hpg-dim {
  opacity: 0.22;
}
.hpg-chip {
  fill: var(--vp-c-bg-elv);
  stroke: var(--vp-c-divider);
  stroke-width: 1.2;
  vector-effect: non-scaling-stroke;
  transition: stroke 0.25s ease, filter 0.25s ease;
}
.hpg-chip--root {
  fill: var(--vp-c-brand-1);
  stroke: var(--vp-c-brand-2);
  stroke-width: 1.4;
}
.hpg-chip--sup {
  fill: var(--vp-c-bg);
}
.hpg-node:hover .hpg-chip:not(.hpg-chip--root),
.hpg-node:focus-visible .hpg-chip:not(.hpg-chip--root) {
  stroke: var(--tier-c, var(--vp-c-brand-1));
  filter: drop-shadow(0 4px 8px rgba(0, 0, 0, 0.1));
}
.hpg-rail {
  stroke: var(--tier-c, var(--vp-c-brand-1));
  stroke-width: 3;
  stroke-linecap: round;
  opacity: 0.7;
  vector-effect: non-scaling-stroke;
}
.hpg-name {
  font-family: var(--vp-font-family);
  font-size: 14px;
  font-weight: 600;
  fill: var(--vp-c-text-1);
}
.hpg-name--onroot {
  fill: var(--vp-c-bg-elv);
}
.hpg-sub {
  font-family: var(--vp-font-family);
  font-size: 10.75px;
  font-weight: 400;
  fill: var(--vp-c-text-2);
}
.hpg-sub--onroot {
  fill: color-mix(in srgb, var(--vp-c-bg-elv) 85%, transparent);
}
.hpg-badge {
  font-family: var(--vp-font-family);
  font-size: 8.5px;
  font-weight: 600;
  fill: var(--tier-c, var(--vp-c-brand-1));
  letter-spacing: 0.35px;
}
.hpg-badge--onroot {
  fill: color-mix(in srgb, var(--vp-c-bg-elv) 80%, transparent);
}
.hpg-dot--done {
  fill: var(--vp-c-green-1);
}
.hpg-dot--doing {
  fill: var(--vp-c-yellow-1, #f59e0b);
}
.hpg-here {
  fill: var(--vp-c-yellow-1, #f59e0b);
}

/* ── 图例 ── */
.hpg-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 22px;
  padding: 12px 18px;
  border-top: 1px dashed var(--vp-c-divider);
  font-size: 12.5px;
  font-weight: 400;
  color: var(--vp-c-text-3);
  font-family: var(--vp-font-family);
}
.hpg-legend svg {
  vertical-align: -2px;
  margin-right: 6px;
}
.hpg-legend :deep(.hpg-edge) {
  fill: none;
  stroke-width: 2;
}
.hpg-legend :deep(.hpg-edge--solid) {
  stroke: var(--hpg-main);
  stroke-width: 1.7;
  opacity: 0.82;
}
.hpg-legend :deep(.hpg-edge--dash) {
  stroke: var(--hpg-optional);
  stroke-width: 1.45;
  opacity: 0.8;
  stroke-dasharray: 7 6;
}
.hpg-legend :deep(.hpg-edge--dot) {
  stroke: var(--hpg-support);
  stroke-width: 1.3;
  opacity: 0.72;
  stroke-dasharray: 1 6;
  stroke-linecap: round;
}

/* ── 移动端：固定视口高 + 默认放大档，横滑平移/双指缩放（panzoom 管），
     竖滑穿透回页面滚动（touchAction 动态切换见脚本） ── */
@media (max-width: 767px) {
  .hpg-viewport {
    height: clamp(320px, 58vh, 520px);
  }
  .hpg-svg {
    height: 100%;
    width: auto;
    min-width: 100%;
  }
  .hpg-ctl button:nth-child(1),
  .hpg-ctl button:nth-child(2) {
    padding: 6px 8px;
  }
}

@media (prefers-reduced-motion: reduce) {
  .hpg-edge,
  .hpg-node {
    transition: none;
  }
}
</style>
