# asm-playground

> **收编注记**：本目录是 [github.com/Charliechen114514/aels-animation](https://github.com/Charliechen114514/aels-animation) `8bf0d41` 的快照收编（不带 git 历史）。收编时剥离了 `tests/`、`demo/` 与 Playwright/vitest 配置：下文涉及测试、GDB 对拍、demo 站的命令与目录均不再适用。组件与 worker 的构建仍走 `npm run build`（`scripts/build.mjs`），站点侧的 vendor 装配与 payload 生成由主仓 `scripts/build_asm_playground.ts` 完成。负载格式见 [documents/payload-format.md](documents/payload-format.md)。

浏览器内的真实机器码执行器。读者在文章中单步执行 x86-64 指令，观察 PC、寄存器和栈；Vue 3 组件可嵌入 VitePress 静态站，无需后端。

当前完成阶段 1 MVP 与自研 MIT TypeScript x86-64 解释器替换：单步、继续、暂停、重置，CPU 剖面场景、指令队列与有界内存窗口。解释器覆盖教学语料使用的整数子集，不提供完整 x86 或操作系统仿真；Thumb-2、CE 改码、断点和反向单步在后续阶段。

画面按“芯片剖面 + 内存墙”布置：刚执行的那条指令和下一条排在芯片顶部的控制区里，寄存器常驻芯片内部，本步的读写以小芯片沿走线移动。每一步的读写列表、控制流去向和条件标志变化都来自解释器采集的 StepEvent，不从反汇编文本推断。内存窗口覆盖本步真实访问与 RSP、RBP 邻域，未映射行保持空白。显示策略分“跟随本步 / 自定义 / 全部展示”，固定关注的寄存器跨步保留。

## 运行

要求支持 BigInt 的现代浏览器、Node.js 22.12+ 与 npm。demo 负载与 GDB 基准由脚本从 demo/fixtures/ 的汇编现场生成（缺失时自动，不入库）；tests/golden/ 中保存批量 GDB 黄金语料，单元测试直接消费。首次生成负载需要 Linux x86-64 上的 Python 3 与 GNU binutils；缺少 gdb 时只跳过 demo 的现场对拍，已入库的黄金对拍仍必须通过。

```sh
npm ci
npm run dev
```

打开终端给出的本地 URL，下滚到执行器。负载与解释器均从站点同源加载，无外部编译服务或远程引擎请求；解释器随 Worker 打包，进入视口后才初始化。

```sh
npm run typecheck
npm test                  # 契约、真实执行/GDB 对拍、SSR
npm run test:diff         # demo 与批量黄金语料 GDB 对拍
npm run test:tools        # Python 负载生成工具
npx playwright install chromium   # 若本机没有 Playwright 浏览器
npm run test:e2e          # /lab/ 子路径，交互/异常/超时/窄屏
npm run fixture          # 真编译 + 原生 GDB，重建负载与基准
npm run build            # 组件、CSS、类型、Worker 与许可证
npm run verify:dist      # 自研引擎随 Worker 打包、无远程/GPL 引擎、生产 ESM SSR、声明
npm run build:demo       # 静态 demo，产物 dist-demo/
npm run test:static      # 用已构建 dist 组件验证 /lab/ 生产静态部署（先 build）
```

浏览器可通过 `CHROMIUM_PATH` 指定。受限容器中 ptrace、本地端口或浏览器进程可能需运行环境授权。所有结果以退出码和仓内报告为准，不能跳过失败门槛。

## VitePress 接入

1. 将整个 dist 拷入 `site/.vitepress/theme/vendor/asm-playground/`，保留其中动态导入的 chunk。
2. 将 `dist/engine/executor-worker.js` 布置到站点同源路径（默认 `/asm-engine/`）。该文件已包含自研 MIT 解释器，无需另行部署引擎或配置 CDN。分发时保留 dist 中的 LICENSE。
3. 将文章配套负载放入 `site/public/payloads/`。在现有主题的 enhanceApp 中注册：

```ts
import { withBase } from 'vitepress'
import { AsmExecutorPlugin } from './vendor/asm-playground/index.js'
import './vendor/asm-playground/asm-playground.css'

// 合并到现有 enhanceApp({ app }) 内：
app.use(AsmExecutorPlugin, { workerUrl: withBase('/asm-engine/executor-worker.js') })
```

文章中一行嵌入，src 使用站点 base 对应的 URL：

```vue
<AsmExecutor src="/你的站点前缀/payloads/b1_add.json" />
```

无站点前缀时用 `/payloads/b1_add.json`。`src` 变更会重新加载；可用组件属性 `worker-url` 单独覆盖插件配置。也可以直接导入具名 `AsmExecutor` 组件。公共类型包括 PayloadV1、Hex、Snapshot、ExecutorStatus 和 AsmExecutorOptions。原 engineUrl / engine-url 选项已移除。

SSR 只渲染占位内容；进入浏览器视口后先校验负载，再动态加载适配层、Worker 与对应引擎。每个组件的 CPU 状态独立。执行期间可操作页面其他部分。

负载校验由项目自有 TypeScript 代码完成，无运行时校验库依赖：先检查 payload v1 的字段结构与数值边界，再验证地址、权限、机器码和寄存器语义。公开的 JSON Schema 保留为格式契约；修改格式时需同步更新 Schema、校验器与契约测试。未知扩展字段保留，可选字段不自动写入默认值。

## 样例与边界

`demo/fixtures/b1_add/` 只保存手写汇编（b1_add.s、boot.s 启动桩、layout.ld 固定地址布局）；ELF、初始状态、负载与 19 步原生 GDB 轨迹是 `tools/build_fixture.py` 的产物，不入库，缺失时自动重建。原生进程的启动片段为 GDB 建立固定栈映射，实际教学从 main 开始，结束在 int3 守卫之前。数字与文章锚点精确一致；这是受控复现实验，不声称重建了文章原始 ELF。

格式与生成命令见 [负载规范](documents/payload-format.md)。v1 契约要求结束地址处为真实 `int3` 守卫；异常后使用新 Worker 恢复，避免复用部分执行状态。

## GDB 黄金对拍与开发规范

tests/golden/ 的每个场景包含 payload.json、gdb-trace.json 与记录 ELF 哈希和工具版本的 manifest.json。源码在 tools/golden/；测试将每步 18 个寄存器与整个 8192 字节栈逐位/逐字节比较，重置后再跑一轮。预期值只来自原生 GDB，解释器不参与采集。

```sh
python3 tools/generate_golden.py                     # 真实 as/ld 编译 + 原生 GDB，重录全部场景
python3 tools/generate_golden.py --only alu_flags    # 只重录指定场景，可列多个名字
npx vitest run tests/unit/golden.test.ts             # 使用已保存参考，不需要本机运行 GDB
```

采集通常使用 stepi；PUSHF 使用下一条指令处的临时断点运行，避免 GDB 硬件单步的 TF 位进入程序栈。REP 在 RIP 前进后记录完整指令结果。部分架构未定义标志位按现有采集主机的行为复现，不能据此推断所有 x86 CPU 都相同。重录只能由真实工具链和 GDB 完成，禁止手改预期来迁就解释器。

TypeScript 以清晰可读为基本要求，具体约定见 [代码规范](documents/typescript-style.md)。

组件与 demo 使用站点/系统字体，不请求远端字体。

自有代码（含解释器和负载校验器）使用 MIT。Vue 作为外部 peer dependency，由宿主站点提供，未打入组件或 Worker。
