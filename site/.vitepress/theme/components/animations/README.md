# animation_maker Web 产物目录(单来源分发)

**播放器一份, 数据每动画一份。** 生成日期: 2026-09-04。

| 路径 | 说明 |
|---|---|
| `AnimPlayer.vue` | 通用播放器, 全部动画共用, 零运行时依赖, 勿手改; 已存在时编译默认跳过, 升级加 `--force-player` |
| `data/<id>.json` | 动画数据(id = DSL 文件名), 播放器唯一输入 |
| `posters/<id>.svg` | 首帧静态图(打印/PDF 等无 JS 场景的可选兜底) |

## 接入 VitePress(实战验证过的姿势)

1. 拷 `AnimPlayer.vue` 到 `site/.vitepress/theme/components/animations/`
   (仅需一份), 数据放同级 `data/` 子目录。
2. 注册(懒加载, `theme/index.ts`):

   ```ts
   import { defineAsyncComponent } from 'vue'
   // enhanceApp({ app }) 内:
   app.component('AnimPlayer', defineAsyncComponent(() =>
     import('./components/animations/AnimPlayer.vue')))
   ```

   **必须 PascalCase 使用(`<AnimPlayer />`)**。不要写 `<anim-player>`:
   VitePress 站点常把含连字符的标签交给 `isCustomElement` 当 Web Component,
   结果是静默变原生标签 —— 不报错、不渲染。

3. 页面级 import 数据后传入(md 内 `<script setup>`):

   ```md
   <script setup>
   import vecGrowth from './animations/data/opp1-vector-growth.json'
   </script>

   <AnimPlayer :data="vecGrowth" />
   ```

   注意:
   - **变量名随意, 不必含 id**; id 带连字符(如 `opp1-vector-growth`)不是
     合法 JS 标识符, 别把文件名拼进变量名。
   - md 模板表达式在页面作用域求值, `provide/inject` 在其中拿不到 ——
     页面级 import 是唯一通路。
   - 跨深目录建议 vite alias: `{ '@anim': <animations 目录> }`,
     之后 `import vecGrowth from '@anim/data/<id>.json'`。

## 播放控制

- **单步**: 以 DSL 语义 Step 为边界步进(开场是独立一步), 进度条刻度即边界。
- **倍速**: 0.5x-2x; DSL 中 push 的 `pace` 已折算为该 Step 的速率系数。
- **键盘**: 进度条可聚焦, 左右方向键微调; `aria-valuenow` 同步。
- **懒播放**: 进视口才首次自动播放(仅首次); `prefers-reduced-motion` 生效。

## SSG/SSR 安全

setup 阶段零 `window`/`document`; 渲染是纯函数 `buildFrame(t)`, SSR 输出即
首帧; rAF/Observer 全在 `onMounted` 且暂停即停转。`pnpm build` 直接过。

## 更新播放器

编译时播放器已存在则默认跳过(保护消费端); 升级用:

    python -m animation_maker compile <scene>.yaml --backend web --force-player
