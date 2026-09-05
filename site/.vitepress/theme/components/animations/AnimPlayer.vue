<!--
  AnimPlayer.vue — animation_maker Web 后端通用播放器
  源文件: src/animation_maker/web_player/AnimPlayer.vue(编译时原样拷贝分发, 产物副本勿手改)
  用法: <AnimPlayer :data="xxData" />   xxData = 同名 .json 的 import
  播放控制: 播放/暂停/单步(语义 Step 边界)/倍速(0.5x-2x)/进度条(Step 刻度)

  坐标契约(2026-09-03 消费方 DOM 实测反馈修复):
  - json 坐标一律画框系(x∈[-w/2, w/2] 中心原点, y 向上), fs 为画框单位(非像素);
  - 模板负责 y 翻转 + 原点平移(左上) + ×100 缩放, x/y 两侧都必须做原点平移;
  - 纯函数核心在普通 <script> 块导出, vitest 直测本文件(测试即分发)。

  架构(2026-09-03 注册表化重构, 行为保持, e2e golden 全绿佐证):
  - buildFrame = 时间轴重放引擎 + 家族模块注册表 FAMILIES;
  - 每个家族 = { reduce: 事件归约器表, render: 原语展开 }, "语义→原语"映射
    全部聚在家族内, 与引擎解耦;
  - FAMILIES 顺序即渲染 z-order(后渲染者叠上层), 勿随意调整;
  - 帧状态(c.*)是家族间共享黑板 —— DSL 语义本身跨家族(fill 联动 counter,
    row_destroy 联动文本淡出), 强行按家族隔离状态反而扭曲语义;
  - 阶段 C 前瞻: 将来"语义→原语"映射搬入 Python 编译器(emitter 插件)时,
    按家族整体摘除即可, 引擎与渲染顺序表不动。
-->
<script>
/* ---------- 纯函数核心(无组件状态): vitest 直接 import 本文件断言 ---------- */

export function ease(p) {
  return p <= 0 ? 0 : p >= 1 ? 1 : p * p * (3 - 2 * p)
}

const MONO = 'Consolas, Menlo, monospace'
const SANS = '"Microsoft YaHei", "PingFang SC", sans-serif'

/* tabular 家族辅助: 取/建格行状态 */
function rowSt(c, key) {
  if (!c.rows[key]) {
    c.rows[key] = { g: c.D.actors[key], filled: new Set(), op: 1, dx: 0, dy: 0,
                    gone: false, ghosts: [], animCell: null }
  }
  return c.rows[key]
}

/* ================ 家族模块: 每族 = 事件归约器表 + 原语展开 ================ */

/* --- text: 静态文本淡入淡出 --- */
const textFam = {
  reduce: {
    write:   (c, ev, at, p) => { c.texts[ev.target] = p },
    fadein:  (c, ev, at, p) => { c.texts[ev.target] = p },
    fadeout: (c, ev, at, p) => {
      c.texts[ev.target] =
        (c.texts[ev.target] === undefined ? 1 : c.texts[ev.target]) * (1 - p)
    },
  },
  render(c) {
    /* counter 除外 —— 它由 counter 家族渲染动态文本+脉冲框,
       否则同屏叠两份(2026-09-03 反馈 Bug 4) */
    for (const [id, op] of Object.entries(c.texts)) {
      if (id === 'counter') continue
      if (op <= 0.01) continue
      const a = c.D.actors[id]
      if (!a || a.kind !== 'text') continue
      c.prim.push({ tag: 'text', x: a.x, y: a.y, fs: a.fs, fill: a.color,
                    ff: a.mono ? MONO : SANS, op, text: a.text })
    }
  },
}

/* --- counter: 计数器文本 + 增长脉冲框 --- */
const counterFam = {
  reduce: {
    counter: (c, ev, at, p) => { if (p > 0.5) c.counterTxt = ev.text },
    pulse:   (c, ev, at, p) => { c.pulse = 1 - p },
  },
  render(c) {
    /* 无 counter actor 的自定义 json 优雅降级, 不白屏 */
    const a = c.D.actors.counter
    if (!a) return
    const op = c.texts.counter === undefined ? 1 : c.texts.counter
    if (op <= 0.01) return
    c.prim.push({ tag: 'text', x: a.x, y: a.y, fs: a.fs * (1 + 0.04 * c.pulse),
                  fill: a.color, ff: MONO, op, text: c.counterTxt || a.text })
    if (c.pulse > 0.02) {
      c.prim.push({ tag: 'rect', x: a.x - a.w * 0.55, y: a.y - a.h * 0.8,
                    w: a.w * 1.1, h: a.h * 1.6, stroke: c.P.grow, sw: 0.02,
                    op: c.pulse })
    }
  },
}

/* --- tabular: 格行(创建/填充/搬迁/交换/销毁) --- */
const tabularFam = {
  reduce: {
    row_create: (c, ev, at, p) => {
      /* 修复: 重置出生位移 —— 同 key 跨多次扩容复用时, 上一次 row_swap 的
         定居位移残留会让新行直接出生在目标位(2026-09-03 反馈 Bug 1b) */
      const rs = rowSt(c, ev.target)
      /* 2026-09-04 语义修复: 事件自带当拍几何(ev.g) —— actors 是终态快照,
         多阶段容量此前被压扁成最终容量; 有 ev.g 即按新缓冲出生语义重置 */
      if (ev.g) {
        rs.g = ev.g
        rs.filled = new Set()
        rs.gone = false
      }
      rs.op = p
      rs.dx = 0
      rs.dy = 0
    },
    fill: (c, ev, at, p, time) => {
      const rs = rowSt(c, ev.target)
      /* settle 容差 1e-12: 事件边界采样上 (t-at)/dur 可为 0.99999999999998,
         单槽位 animCell 会被下一格抢走而把本格闪回 0 —— 与 v2 逐 prim
         关键帧在该 ulp 窗口内语义分叉(2026-09-04 等价性对拍锁定) */
      const settled = p >= 1 - 1e-12
      rs.animCell = settled ? null : { i: ev.i, p }
      if (settled) rs.filled.add(ev.i)
      if (ev.counter) {
        /* size 在半程翻面: 阈值时刻与 v2 转译器同式构造(at + dur/2)并同用
           >= 比较, 位级对齐 —— 此前 (t-at)/dur>=0.5 与 at+dur/2 两条算术
           路径在 ulp 级分歧, 特定时间轴会在中点采样上差 1(2026-09-04) */
        const flipAt = at + ev.dur / 2
        c.counterTxt = 'size = ' + (time >= flipAt ? ev.s : ev.s - 1) +
                       '    capacity = ' + ev.cap
      }
    },
    relocate: (c, ev, at, p, time) => {
      const dst = rowSt(c, ev.dst)
      const n = ev.count
      /* 2026-09-04 修复: 并发飞行(镜像 Manim LaggedStart 参考语义) —— 此前
         单 ghost 槽被下一个启动的元素抢占, 前一个在 31%(n=4)~4%(n=32) 处
         凭空消失, 观感即"搬移被打断"。现在窗口重叠期间所有在飞元素同屏,
         各自飞完全程(settle 容差与 fill 一致) */
      dst.ghosts = []
      for (let k = 0; k < n; k++) {
        const seg = n > 1 ? 0.55 * (k / (n - 1)) : 0
        const dur = 0.45 + 0.55 / n
        const p2 = Math.min(1, Math.max(0, (time - at - seg) / dur))
        if (p2 >= 1 - 1e-12) dst.filled.add(k)
        else if (p2 > 0) dst.ghosts.push({ i: k, p: p2, src: ev.src })
      }
    },
    row_destroy: (c, ev, at, p) => {
      const rs = rowSt(c, ev.target)
      rs.op = 1 - p
      if (p >= 1) rs.gone = true
      c.texts[ev.write] = p
    },
    row_swap: (c, ev, at, p) => {
      /* 修复: 位移作用于新行(tmp)而非旧行 —— 旧行已被 row_destroy 标记 gone
         永不渲染, 写它的位移等于作废; DSL 语义是 tmp 升起接替 target 的位置
         (2026-09-03 反馈 Bug 1a)。完成后做身份交接: 正式键(后续 fill/hl/move
         都指向它)复活到定居位, tmp 退场。 */
      const tmp = rowSt(c, ev.tmp)
      const tx = ev.to ? ev.to[0] : c.D.actors[ev.target].cx
      const ty = ev.to ? ev.to[1] : c.D.actors[ev.target].cy
      tmp.dx = (tx - tmp.g.cx) * ease(p)
      tmp.dy = (ty - tmp.g.cy) * ease(p)
      tmp.op = 1
      if (p >= 1) {
        const fin = rowSt(c, ev.target)
        fin.gone = false
        fin.op = 1
        /* 2026-09-04 语义修复: 交接连几何与定居位移一起收养 —— 新几何以
           出生位(tmp.g)为坐标基准, 定居靠 tmp 的 dx/dy 偏移表达;
           这正是 buffer_ = std::move(new_buf_) 的语义 */
        fin.g = tmp.g
        fin.filled = new Set(tmp.filled)
        fin.animCell = null
        fin.dx = tmp.dx
        fin.dy = tmp.dy
        tmp.gone = true
      }
      for (const f of (ev.fade || [])) {
        c.texts[f] = (c.texts[f] === undefined ? 1 : c.texts[f]) * (1 - p)
      }
    },
  },
  render(c) {
    for (const rs of Object.values(c.rows)) {
      if (rs.gone || rs.op <= 0.01) continue
      for (const cl of rs.g.cells) {
        const done = rs.filled.has(cl.i)
        const fo = done ? 1 : (rs.animCell && rs.animCell.i === cl.i ? rs.animCell.p : 0)
        const x = cl.x + rs.dx, y = cl.y + rs.dy, s = cl.s
        c.prim.push({ tag: 'rect', x: x - s / 2, y: y - s / 2, w: s, h: s,
                      fill: c.P.fill, fillOp: 0.95 * fo,
                      stroke: fo > 0.5 ? c.P.fillEdge : c.P.emptyEdge, sw: 0.014,
                      op: rs.op })
        if (rs.g.idx && rs.g.idx.includes(cl.i + 1)) {
          c.prim.push({ tag: 'text', x, y, fs: s * 0.42,
                        fill: fo > 0.5 ? c.P.numOnFill : c.P.muted, ff: MONO,
                        op: rs.op, text: String(cl.i + 1) })
        }
      }
      /* 2026-09-04 修复: 逐个绘制所有在飞元素(并发搬迁), 详见 relocate 归约器 */
      for (const gh of rs.ghosts) {
        const srcRow = rowSt(c, gh.src).g
        const dstCell = rs.g.cells[gh.i]
        if (srcRow && dstCell) {
          const srcCell = srcRow.cells[gh.i]
          const q = ease(gh.p)
          c.prim.push({ tag: 'ghost',
                 x: srcCell.x + (dstCell.x - srcCell.x) * q + rs.dx,
                 y: srcCell.y + (dstCell.y - srcCell.y) * q + rs.dy,
                 s: srcCell.s + (dstCell.s - srcCell.s) * q,
                 op: Math.min(1, 4 * q * (1 - q) + 0.5) })
        }
      }
    }
  },
}

/* --- pointer: 指针箭头 --- */
const ptrFam = {
  reduce: {
    ptr_show: (c, ev, at, p) => { c.ptrs[ev.target] = { op: p, x: null, y: null } },
    ptr_move: (c, ev, at, p) => {
      const st = c.ptrs[ev.target] || (c.ptrs[ev.target] = { op: 1 })
      st.to = ev.to
      st.p = p
    },
  },
  render(c) {
    for (const [pid, st] of Object.entries(c.ptrs)) {
      if (st.op <= 0.01) continue
      const g = c.D.actors['ptr:' + pid]
      if (!g) continue
      let x = g.x, tipY = g.tipY, baseY = g.baseY, ty = g.txt.y
      if (st.to) {
        const q = ease(st.p === undefined ? 1 : st.p)
        const dTip = st.to[1] - g.tipY
        x = g.x + (st.to[0] - g.x) * q
        tipY += dTip * q
        baseY += dTip * q
        ty += dTip * q
      }
      c.prim.push({ tag: 'ptr', x, tipY, baseY, ty, name: g.name,
                    fs: g.txt.fs, op: st.op, color: c.P.text })
    }
  },
}

/* --- highlight: 瞬态高亮框 --- */
const hlFam = {
  reduce: {
    hl_show: (c, ev, at, p) => { c.hls.push({ frame: ev.frame, op: p }) },
    hl_hide: (c, ev, at, p) => { c.hls.push({ frame: ev.frame, op: 1 - p }) },
  },
  render(c) {
    for (const h of c.hls) {
      if (h.op <= 0.01) continue
      const f = h.frame
      c.prim.push({ tag: 'hl', x: f.cx - f.w / 2, y: f.cy - f.h / 2, w: f.w,
                    h: f.h, color: f.color, op: h.op })
    }
  },
}

/* --- annot: 大件(code/bars/rowlist)与自由标注的显隐/移动 --- */
const annotFam = {
  reduce: {
    label_move: (c, ev, at, p) => {
      const el = c.D.actors['show:' + ev.target]
      const m = c.moves['show:' + ev.target] || (c.moves['show:' + ev.target] = {})
      const fr = m.from || [el.x, el.y]
      m.from = [fr[0] + (ev.to[0] - fr[0]) * ease(p),
                fr[1] + (ev.to[1] - fr[1]) * ease(p)]
    },
    code_show:    (c, ev, at, p) => { c.moves[ev.target] = { op: p } },
    bars_show:    (c, ev, at, p) => { c.moves[ev.target] = { op: p } },
    rowlist_show: (c, ev, at, p) => { c.moves[ev.target] = { op: p } },
    move: (c, ev, at, p) => {
      /* 修复: CellRow 的 move 写入行状态而非 moves —— 渲染循环的 moves
         只展开 text/code/bars/rowlist 四种, 行移动此前是瞬移无动画 */
      if (ev.target.startsWith('row:')) {
        const rs = rowSt(c, ev.target)
        rs.dx = (ev.frm[0] - ev.to[0]) * (1 - ease(p))
        rs.dy = (ev.frm[1] - ev.to[1]) * (1 - ease(p))
      } else {
        const m = c.moves[ev.target] || (c.moves[ev.target] = { op: 1 })
        m.dx = (ev.frm[0] - ev.to[0]) * (1 - ease(p))
        m.dy = (ev.frm[1] - ev.to[1]) * (1 - ease(p))
      }
    },
  },
  render(c) {
    /* 大件与自由标注: 平铺展开为 text/rect 原语 */
    const emitCard = (a, cx, cy, op) => {
      c.prim.push({ tag: 'rect', x: cx - a.w / 2, y: cy + a.h / 2, w: a.w,
                    h: a.h, stroke: c.P.emptyEdge, sw: 0.02, op, rx: true })
      const top = cy + a.h / 2 - 0.26
      for (let li = 0; li < a.rows.length; li++) {
        const ln = a.rows[li]
        const yy = top - a.lh / 2 - li * a.pitch
        c.prim.push({ tag: 'text',
               x: cx - a.w / 2 + 0.26 + a.gutW + 0.30 + ln.w / 2,
               y: yy, fs: a.fs, fill: c.P.text, ff: MONO, op, text: ln.text })
        c.prim.push({ tag: 'text', x: cx - a.w / 2 + 0.26 + a.gutW / 2, y: yy,
               fs: a.fs * 0.8, fill: c.P.muted, ff: MONO, op, text: String(li + 1) })
      }
    }
    const emitBars = (a, cx, cy, op) => {
      const base = cy + a.h / 2
      for (let ci = 0; ci < a.cols.length; ci++) {
        const col = a.cols[ci]
        const x = cx - a.w / 2 + a.colW / 2 + ci * a.pitch
        c.prim.push({ tag: 'rect', x: x - a.colW / 2, y: base - col.h, w: a.colW,
               h: col.h, fill: c.P.fill, fillOp: 0.92, stroke: c.P.fillEdge,
               sw: 0.012, op })
        c.prim.push({ tag: 'text', x, y: base - col.h - 0.10, fs: 0.24,
               fill: c.P.muted, ff: MONO, op, text: String(col.value) })
        c.prim.push({ tag: 'text', x, y: base + 0.28, fs: 0.26, fill: c.P.text,
               ff: SANS, op, text: col.label })
      }
    }
    const emitRowList = (a, cx, cy, op) => {
      let yTop = cy + a.h / 2
      for (const ent of a.entries) {
        const cyE = yTop - Math.max(ent.card.h, ent.note.h) / 2
        c.prim.push({ tag: 'rect', x: cx - a.w / 2, y: yTop - ent.card.h,
               w: ent.card.w, h: ent.card.h, stroke: c.P.emptyEdge, sw: 0.02,
               op, rx: true })
        c.prim.push({ tag: 'text', x: cx - a.w / 2 + ent.card.w / 2, y: cyE,
               fs: ent.card.fs, fill: c.P.text, ff: MONO, op,
               text: ent.card.rows[0].text })
        c.prim.push({ tag: 'text', x: cx - a.w / 2 + a.noteX, y: cyE, fs: 0.28,
               fill: ent.tone === 'ok' ? c.P.ok : ent.tone === 'bad' ? c.P.bad : c.P.text,
               ff: SANS, op, text: ent.note.text })
        yTop -= Math.max(ent.card.h, ent.note.h) + 0.35
      }
    }
    for (const [id, m] of Object.entries(c.moves)) {
      const op = m.op === undefined ? 1 : m.op
      if (op <= 0.01) continue
      const a = c.D.actors[id]
      if (!a) continue
      const dx = m.dx || 0, dy = m.dy || 0
      const cx = (m.from ? m.from[0] : (a.cx !== undefined ? a.cx : a.x)) + dx
      const cy = (m.from ? m.from[1] : (a.cy !== undefined ? a.cy : a.y)) + dy
      if (a.kind === 'text') {
        c.prim.push({ tag: 'text', x: cx, y: cy, fs: a.fs, fill: a.color,
                      ff: SANS, op, text: a.text })
      } else if (a.kind === 'code') {
        emitCard(a, cx, cy, op)
      } else if (a.kind === 'bars') {
        emitBars(a, cx, cy, op)
      } else if (a.kind === 'rowlist') {
        emitRowList(a, cx, cy, op)
      }
    }
  },
}

/* =============== 引擎: 家族表 + 时间轴重放 + 展开调度 =============== */

/* render 顺序 = SVG z-order(后渲染者叠上层)。调整顺序 = 改变遮挡语义, 慎动。 */
const FAMILIES = [textFam, counterFam, tabularFam, ptrFam, hlFam, annotFam]

/* 事件类型 -> 归约器: 模块加载时合成一次, 重放循环内查表分发 */
const REDUCE = {}
for (const f of FAMILIES) Object.assign(REDUCE, f.reduce)

/* buildFrame(D, time): 事件时间轴重放 -> 扁平 SVG 原语列表(画框系, y 向上)。
   纯函数: 同一 (D, time) 恒等输出, 不触碰 props/window/document。 */
export function buildFrame(D, time) {
  /* 帧状态黑板: 各家族 reduce 写入, render 按注册序消费 */
  const c = { D, P: D.palette, prim: [],
              rows: {}, texts: {}, moves: {}, ptrs: {},
              counterTxt: null, pulse: 0, hls: [] }

  /* 时间轴重放: 事件绝对时刻 = step 起点 + ev.t, p 为该事件的进行度 */
  const ss = [0]
  for (const st of D.steps) ss.push(ss[ss.length - 1] + st.duration)
  for (let si = 0; si < D.steps.length; si++) {
    const st = D.steps[si]
    for (const ev of st.events) {
      const at = ss[si] + ev.t
      if (at > time) continue
      const p = ev.dur > 0 ? Math.min(1, (time - at) / ev.dur) : 1
      const h = REDUCE[ev.type]
      if (h) h(c, ev, at, p, time)
    }
  }

  for (const f of FAMILIES) f.render(c)
  return c.prim
}

/* =============== v2 内核: prim + tl 关键帧求值(C 阶段) =============== */

/* v2 ease 闭集(与 Python 转译器约定一一对应):
   s=smoothstep(同 v1 ease) / l=线性
   j=阶跃(p>0.5 取 to, 严格大于 —— 镜像 v1 fill stroke/counter 的 fo>0.5;
   窗口型 j-kf 与 v1 事件窗口同源, p 位级一致故边界行为一致。
   v1 的 p>=0.5 类语义(fill counter/relocate settle/swap 交接)由转译器
   发 t0==t1 的时刻阶跃帧表达, p=1 恒取 to, 不依赖此阈值)
   p=ghost 抛物线 —— 镜像 v1 的 min(1, 4·ease(p)·(1-ease(p))+0.5),
   注意先 smoothstep 再抛物线, 不是裸 4p(1-p)+0.5;
   端点 p=0/1 归 0: v1 ghost 可见条件是 p2∈(0,1) 开区间(严格大于),
   窗口边界时刻 ghost 不存在。 */
export const EASEV2 = {
  s: ease,
  l: (p) => p,
  j: (p) => (p > 0.5 ? 1 : 0),
  p: (p) => { if (p <= 0 || p >= 1) return 0
              const q = ease(p); return Math.min(1, 4 * q * (1 - q) + 0.5) },
}

/* buildFrameV2(D, time): v2 协议求值, 输出与 v1 buildFrame 同构的扁平 prim
   (同一模板消费, e2e 双路径共用 golden)。
   - tl 按发射序线性扫描, t0<=time 即应用, 同 prop 后发覆盖(链式);
   - t1==t0 的阶跃帧防 0 除; 字符串 prop / ease j 一律 p>=0.5 阶跃;
   - dx/dy/fsm 为烘焙通道: 并入 x/y/fs 后剥离, 不进模板;
   - op<=0.01 跳过 = v1 各家族渲染门槛(golden 行数敏感, 勿改阈值);
   - 每帧展开为浅拷贝, 不改 D.prim(跨帧共享)。 */
export function buildFrameV2(D, time) {
  const snap = new Map()
  for (const kf of D.tl) {
    if (kf.t0 > time) continue
    /* 除数用 kf.d(事件 dur 原值): t1-t0 浮点相减会失真, 破坏与 v1
       (time-at)/ev.dur 的位级一致(中点采样即分歧) */
    const p = kf.d < 1e-9 ? 1
      : Math.min(1, Math.max(0, (time - kf.t0) / kf.d))
    const step = kf.ease === 'j' || typeof kf.from === 'string'
    const v = step ? (p > 0.5 ? kf.to : kf.from)
      : kf.from + (kf.to - kf.from) * EASEV2[kf.ease](p)
    for (const id of kf.ids) {
      let s = snap.get(id)
      if (!s) snap.set(id, s = {})
      s[kf.prop] = v
    }
  }
  const out = []
  for (const pr of D.prim) {
    if (time < pr.born) continue   // 出生前不渲染(镜像 v1 惰性帧状态)
    const it = { ...pr, ...(snap.get(pr.id) || {}) }
    if (it.dx !== undefined) it.x += it.dx
    if (it.dy !== undefined) it.y += it.dy
    if (it.fsm !== undefined) it.fs *= it.fsm
    delete it.id; delete it.born
    delete it.dx; delete it.dy; delete it.fsm
    /* gone = per-prim 存在性阈值(v1 counter 脉冲框是 pulse>0.02 严格,
       其余家族一律 0.01); 阈值本身不进模板输出 */
    if (it.op === undefined || it.op <= (it.gone ?? 0.01)) continue
    delete it.gone
    out.push(it)
  }
  return out
}

/* 指针箭头三顶点(viewBox 系)。修复: 补 fw 半宽平移 —— 此前 x 裸乘导致
   指针整体贴左缘(2026-09-03 反馈 Bug 2)。 */
export function ptrPts(it, fw, fh) {
  const bx = (fw / 2 + it.x) * 100, by = (fh / 2 - it.tipY) * 100
  const dir = it.tipY > it.baseY ? 10 : -10
  return bx + ',' + by + ' ' + (bx - 4) + ',' + (by + dir) + ' ' +
         (bx + 4) + ',' + (by + dir)
}
</script>

<script setup>
import { ref, computed, watch, onMounted, onBeforeUnmount, nextTick } from 'vue'

const props = defineProps({
  data: { type: Object, required: true },
  autoplay: { type: Boolean, default: true }
})

/* 数据校验: json 损坏时显式报错, 不静默白屏 */
const dataErr = computed(() => {
  const d = props.data
  if (!d || typeof d !== 'object') return 'AnimPlayer: data 缺失或不是对象(检查 JSON import)'
  if (!Array.isArray(d.steps) || d.steps.length === 0) return 'AnimPlayer: data.steps 缺失或为空'
  if (!d.actors || !d.palette || !d.frame) return 'AnimPlayer: data 结构不完整(缺 actors/palette/frame)'
  return ''
})

/* step 绝对边界; total 总时长(steps 求和, 权威值) */
const stepStarts = computed(() => {
  const out = [0]
  if (!dataErr.value) {
    for (const st of props.data.steps) out.push(out[out.length - 1] + st.duration)
  }
  return out
})
const total = computed(() => stepStarts.value[stepStarts.value.length - 1])
const t = ref(0)
const playing = ref(false)
const rate = ref(1)
const playedOnce = ref(false)
const rootEl = ref(null)
let rafId = 0, lastTs = 0

function stepIndexOf(time) {
  const ss = stepStarts.value
  for (let i = ss.length - 2; i >= 0; i--) {
    if (time >= ss[i] - 1e-6) return i
  }
  return 0
}
const curStepIdx = computed(() => stepIndexOf(t.value))
const curStep = computed(() => props.data.steps[curStepIdx.value])
const progress = computed(() => (t.value / total.value) * 100)

function play() {
  if (t.value >= total.value - 1e-3) t.value = 0
  playing.value = true
}
function pause() { playing.value = false }
function toggle() { playing.value ? pause() : play() }
function stepBy(n) {
  pause()
  const ss = stepStarts.value
  const i = stepIndexOf(t.value)
  if (n > 0) t.value = ss[Math.min(i + 1, ss.length - 1)]
  else if (t.value - ss[i] > 0.3) t.value = ss[i]
  else t.value = ss[Math.max(i - 1, 0)]
}
function seek(ev) {
  pause()
  const box = ev.currentTarget.getBoundingClientRect()
  t.value = Math.max(0, Math.min(1, (ev.clientX - box.left) / box.width)) * total.value
}
function seekBy(dt) {
  pause()
  t.value = Math.max(0, Math.min(total.value, t.value + dt))
}

/* rAF 仅播放时运转: 暂停即 cancel(一页多动画不空转省电), 恢复重启 */
function loop(ts) {
  if (lastTs) {
    const dt = Math.min((ts - lastTs) / 1000, 0.1)
    t.value += dt * rate.value * (curStep.value.speed || 1)
    if (t.value >= total.value) { t.value = total.value; playing.value = false }
  }
  lastTs = ts
  rafId = playing.value ? requestAnimationFrame(loop) : 0
  if (!rafId) lastTs = 0
}
watch(playing, (on) => {
  if (on) { if (!rafId) rafId = requestAnimationFrame(loop) }
  else if (rafId) { cancelAnimationFrame(rafId); rafId = 0; lastTs = 0 }
})

/* ---------- E2E 几何快照钩子(挂根元素, 多实例互不覆盖; 生产零副作用) ----------
   用法(Playwright): document.querySelector('.amp').__amp
     await __amp.seek(t)  -> 暂停并跳到 t 秒(nextTick 后渲染落定)
     __amp.snapshot()     -> 行字符串数组(viewBox 系 DOM 实测, 数值 round2 消浮点尾数)
   钩子随 AnimPlayer.vue 分发, e2e 与消费端行为同源。 */
function _snapNum(v) { return Math.round(parseFloat(v) * 100) / 100 }
function _snapshot() {
  const svg = rootEl.value && rootEl.value.querySelector('svg.amp-svg')
  if (!svg) return ['<no-svg>']
  const vb = (svg.getAttribute('viewBox') || '0 0 0 0').trim().split(/\s+/).map(Number)
  const W = vb[2] || 1, H = vb[3] || 1
  const out = []
  for (const el of svg.querySelectorAll('text,rect,polygon,line')) {
    const tag = el.tagName.toLowerCase()
    if (tag === 'text') {
      out.push(`text ${_snapNum(el.getAttribute('x'))} ${_snapNum(el.getAttribute('y'))}` +
        ` fs=${_snapNum(el.getAttribute('font-size'))}` +
        ` op=${_snapNum(el.getAttribute('opacity') ?? 1)}` +
        ` "${(el.textContent || '').trim().slice(0, 24)}"`)
    } else if (tag === 'rect') {
      const w = parseFloat(el.getAttribute('width')), h = parseFloat(el.getAttribute('height'))
      if (w >= W - 1 && h >= H - 1) continue /* 全幅背景板 */
      out.push(`rect ${_snapNum(el.getAttribute('x'))} ${_snapNum(el.getAttribute('y'))}` +
        ` ${_snapNum(w)} ${_snapNum(h)} op=${_snapNum(el.getAttribute('opacity') ?? 1)}` +
        (el.getAttribute('rx') !== null ? ' rx' : ''))
    } else if (tag === 'polygon') {
      const pts = (el.getAttribute('points') || '').trim().split(/\s+/)
        .map((p) => p.split(',').map(_snapNum).join(',')).join(' ')
      out.push(`poly ${pts} op=${_snapNum(el.getAttribute('opacity') ?? 1)}`)
    } else if (tag === 'line') {
      out.push(`line ${_snapNum(el.getAttribute('x1'))} ${_snapNum(el.getAttribute('y1'))}` +
        ` ${_snapNum(el.getAttribute('x2'))} ${_snapNum(el.getAttribute('y2'))}`)
    }
  }
  return out
}

onMounted(() => {
  if (dataErr.value) { console.warn(dataErr.value, props.data); return }
  if (rootEl.value) {
    rootEl.value.__amp = {
      seek: async (sec) => {
        pause()
        t.value = Math.max(0, Math.min(total.value, sec))
        await nextTick()
      },
      snapshot: _snapshot,
    }
  }
  const reduced = typeof matchMedia !== 'undefined' &&
    matchMedia('(prefers-reduced-motion: reduce)').matches
  if (props.autoplay && !reduced && typeof IntersectionObserver !== 'undefined') {
    const io = new IntersectionObserver((es) => {
      for (const e of es) {
        if (e.isIntersecting && !playedOnce.value) { playedOnce.value = true; play() }
      }
    }, { threshold: 0.35 })
    io.observe(rootEl.value)
    onBeforeUnmount(() => io.disconnect())
  }
})
onBeforeUnmount(() => { if (rafId) cancelAnimationFrame(rafId) })

const frame = computed(() => dataErr.value ? [] :
  (props.data.version || 1) >= 2 ? buildFrameV2(props.data, t.value)
                                 : buildFrame(props.data, t.value))
const fmtTime = (s) => s.toFixed(1) + 's'
const RATES = [0.5, 1, 1.5, 2]
</script>

<template>
  <figure ref="rootEl" class="amp">
    <div v-if="dataErr" class="amp-err">{{ dataErr }}</div>
    <template v-else>
    <svg class="amp-svg" :viewBox="'0 0 ' + data.frame.w * 100 + ' ' + data.frame.h * 100"
         preserveAspectRatio="xMidYMid meet" role="img" :aria-label="data.title">
      <rect x="0" y="0" :width="data.frame.w * 100" :height="data.frame.h * 100"
            :fill="data.palette.bg" />
      <!-- 修复(Bug 2): x 一律 + frame.w/2 原点平移(y 侧原本就有 frame.h/2 翻转) -->
      <template v-for="(it, k) in frame" :key="k">
        <text v-if="it.tag === 'text'" :x="(data.frame.w / 2 + it.x) * 100"
              :y="(data.frame.h / 2 - it.y) * 100"
              :font-size="it.fs * 100" :fill="it.fill" :font-family="it.ff"
              :opacity="it.op" text-anchor="middle" dominant-baseline="central">{{ it.text }}</text>
        <rect v-else-if="it.tag === 'rect'" :x="(data.frame.w / 2 + it.x) * 100"
              :y="(data.frame.h / 2 - it.y - it.h) * 100" :width="it.w * 100"
              :height="it.h * 100" :fill="it.fill || 'none'" :fill-opacity="it.fillOp || 0"
              :stroke="it.stroke" :stroke-width="it.sw * 100" :opacity="it.op" />
        <rect v-else-if="it.tag === 'ghost'" :x="(data.frame.w / 2 + it.x - it.s / 2) * 100"
              :y="(data.frame.h / 2 - it.y - it.s / 2) * 100" :width="it.s * 100"
              :height="it.s * 100" :opacity="it.op" :fill="data.palette.grow"
              fill-opacity="0.85" :stroke="data.palette.grow" stroke-width="2" />
        <g v-else-if="it.tag === 'ptr'" :opacity="it.op">
          <line :x1="(data.frame.w / 2 + it.x) * 100" :y1="(data.frame.h / 2 - it.baseY) * 100"
                :x2="(data.frame.w / 2 + it.x) * 100" :y2="(data.frame.h / 2 - it.tipY) * 100"
                :stroke="it.color" :stroke-width="3" />
          <polygon :fill="it.color" :points="ptrPts(it, data.frame.w, data.frame.h)" />
          <text :x="(data.frame.w / 2 + it.x) * 100" :y="(data.frame.h / 2 - it.ty) * 100"
                :font-size="it.fs * 100" :fill="it.color" text-anchor="middle"
                font-family="Consolas, Menlo, monospace">{{ it.name }}</text>
        </g>
        <rect v-else-if="it.tag === 'hl'" :x="(data.frame.w / 2 + it.x) * 100"
              :y="(data.frame.h / 2 - it.y - it.h) * 100" :width="it.w * 100"
              :height="it.h * 100" :stroke="it.color" stroke-width="2.4"
              :fill="it.color" fill-opacity="0.12" :opacity="it.op" rx="4" />
      </template>
    </svg>
    <noscript><span class="amp-noscript">动画需要 JavaScript 支持。</span></noscript>
    <div class="amp-bar">
      <button class="amp-btn" :title="playing ? '暂停' : '播放'" @click="toggle">{{ playing ? '❚❚' : '▶' }}</button>
      <button class="amp-btn" title="上一步(语义 Step)" @click="stepBy(-1)">⏮</button>
      <button class="amp-btn" title="下一步(语义 Step)" @click="stepBy(1)">⏭</button>
      <div class="amp-progress" role="slider" tabindex="0"
           :aria-valuemin="0" :aria-valuemax="Math.round(total)"
           :aria-valuenow="Math.round(t)" aria-label="动画进度(左右方向键微调)"
           @click="seek" @keydown.left.prevent="seekBy(-total * 0.05)"
           @keydown.right.prevent="seekBy(total * 0.05)">
        <div class="amp-fill" :style="{ width: progress + '%' }" />
        <span v-for="(st, i) in data.steps.slice(1)" :key="i" class="amp-tick"
              :style="{ left: (stepStarts[i + 1] / total) * 100 + '%' }" />
      </div>
      <span class="amp-label">{{ curStep.label }} · {{ fmtTime(t) }}/{{ fmtTime(total) }}</span>
      <div class="amp-rates">
        <button v-for="r in RATES" :key="r" class="amp-btn amp-rate"
                :class="{ on: rate === r }" @click="rate = r">{{ r }}x</button>
      </div>
    </div>
    </template>
  </figure>
</template>
<style scoped>
.amp { max-width: 860px; margin: 1em auto; font-family: system-ui, sans-serif; }
.amp-svg { width: 100%; height: auto; display: block; border-radius: 8px 8px 0 0; }
.amp-bar { display: flex; align-items: center; gap: 6px; padding: 6px 8px;
           background: #1e1e2a; border-radius: 0 0 8px 8px; color: #ECECE4; }
.amp-btn { background: #2a2a3a; color: #ECECE4; border: none; border-radius: 6px;
           padding: 4px 10px; cursor: pointer; font-size: 13px; }
.amp-btn:hover { background: #3a3a4e; }
.amp-rate.on { background: #5EC8E0; color: #0e2430; }
.amp-progress { position: relative; flex: 1; height: 8px; background: #2a2a3a;
                border-radius: 4px; cursor: pointer; }
.amp-fill { position: absolute; left: 0; top: 0; bottom: 0; border-radius: 4px;
            background: #5EC8E0; }
.amp-tick { position: absolute; top: -2px; width: 1px; height: 12px; background: #9a9aad; }
.amp-label { font-size: 12px; white-space: nowrap; color: #9a9aad; }
.amp-rates { display: flex; gap: 2px; }
.amp-noscript { color: #9a9aad; font-size: 13px; padding: 6px; }
.amp-err { color: #e06c75; font-size: 13px; padding: 8px; background: #1e1e2a;
           border-radius: 8px; font-family: Consolas, Menlo, monospace; }
.amp-progress:focus { outline: 2px solid #5EC8E0; outline-offset: 2px; }
</style>
