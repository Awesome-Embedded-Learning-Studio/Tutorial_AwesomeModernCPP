# asm-executor/payload@1

本文件及 [JSON Schema](./payload.schema.json) 是消费方与执行器的契约。示例由 `demo/fixtures/b1_add/` 的源码经 `npm run fixture` 生成（产物 `demo/fixtures/b1_add/payload.json` 不入库），不兼容变化升级 format；未知扩展字段允许保留。

运行时由自有 TypeScript 校验器按固定 v1 格式检查结构，再验证地址与执行语义，不动态编译或解释 JSON Schema。修改契约时同步维护 Schema、校验器和测试；校验失败抛出 PayloadError，结构错误包含字段路径，校验成功返回原对象且不填入默认字段。

## 数据与默认值

- `format` 固定为 `asm-executor/payload@1`。
- `meta` 包含 `isa`、`syntax`、`title`、`entry`。Schema 为后续保留 `thumb-2`/`unified`；MVP 语义校验仅启用 `x86-64`，展示语法为 `att` 或 `intel`。
- `code` 每项包含唯一的起始地址 `addr`、连续偶数个十六进制字符 `bytes`、反汇编 `text`。指令长度 1–15 字节、地址区间不重叠，完整包含于某一可执行映射内，并与该处内存逐字节一致。入口必须是已列出的非零指令起点。
- `initial.regs` 为寄存器到值的映射。支持 16 个 64 位通用寄存器、RIP、RFLAGS，及 eax/edi/r8d 等 32 位别名；名称小写。未声明的通用寄存器为零，RFLAGS 默认 `0x202`，RIP 由 entry 确定。同一规范寄存器只能声明一个名称，若显式提供 RIP 则必须等于 entry。RSP 必须位于可写映射中或其顶端边界。
- `initial.memory` 每项为 `addr`、`size`（字节）、`perms`、`bytes`（Base64）。按 4096 字节页对齐，尺寸为正整数页，总量最多 16 MiB，区间不重叠且不溢出 64 位地址空间。数据少于 size 时尾部补零，超过 size 或 Base64 不合法则拒绝。权限按 `rwx` 顺序组合，至少一项。
- `execution.stop_before` 为非空结束地址数组。每个地址必须在可执行映射中，并实际存放 `int3` 字节 `cc`，不能处于其他已列指令的中间。这是 v1 的显式结束守卫契约，守卫本身不会执行。可列多个出口，执行器逐步检查 PC，任意一个命中即正常完成。
- `execution.max_steps` 默认 10000，范围 1–10000，限制每次“继续”，耗尽后暂停，可再次继续；手动单步不消耗上一次继续的预算。
- `ui.watch_regs` 默认 `rip/rsp/rbp/rax/edi/esi/rflags`；RIP 始终展示。`stack_window` 默认 `around: rsp, rows: 8`，rows 为 1–64。窗口从基准寄存器向下按 8 字节对齐的地址开始，沿地址升序显示，每行 8 字节，按小端解释；未映射行显示“未映射”，不改变模拟 CPU 状态。

地址和寄存器值都使用 `0x` 前缀的十六进制字符串；`code.bytes` 使用无前缀 hex，内存 bytes 使用规范 Base64。尺寸、行数与指令计数是 JSON 数字。运行时地址和寄存器统一使用 BigInt，读数按无符号位宽归一化。

## 执行与失败

单步按真实机器码解释执行一条指令。继续在 Worker 内每批执行最多 100 条，批间让出事件循环处理暂停。程序抵达结束地址后停止在该地址之前；若跳到未列出的指令位置，报告该 PC。非法/未支持指令与访问错误进入 error，展示最后一条完整执行后的状态，禁止继续。

重置销毁原 Worker 并从原始负载重新初始化；发生异常的解释器状态禁止复用。运行中两秒未收到响应会终止 Worker，加载初始化单独设 15 秒超时。源码更换、组件卸载时取消请求并销毁旧 Worker，旧响应不能覆盖新场景。

## 构建工具

工具要求 Python 3、GNU nm/objdump，只接受小端 x86-64 ELF64。ELF 的 PT_LOAD 页映射及权限自动提取，与 initial JSON 中额外内存合并；重叠由 TypeScript 校验器拒绝。initial JSON 的结构是 `{ "regs": {...}, "memory": [...] }`，也可附 ui。

```sh
python3 -m tools.make_payload \
  --elf demo/fixtures/b1_add/b1_add.elf \
  --initial demo/fixtures/b1_add/initial.json \
  --entry main --stop-before __stop \
  --title 'call 的完整一生：add(3, 4)' \
  --out /tmp/payload.json
```

入口和结束位置可以填写 ELF 符号名或 `0x` 地址，重复 `--stop-before` 指定多出口。场景作者负责在 ELF 中定义真实 `int3` 结束守卫，并准备纯指令教学片段；没有 OS/syscall/printf 仿真。
