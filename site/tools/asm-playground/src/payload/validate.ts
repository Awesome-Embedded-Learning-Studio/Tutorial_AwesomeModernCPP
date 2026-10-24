import { requireValue } from './errors';
import { assertPayloadShape } from './shape';
import { bytesFromHex, fromBase64, registerInfo, type PayloadV1 } from './types';
export { PayloadError } from './errors';

export function validatePayload(input: unknown): PayloadV1 {
  assertPayloadShape(input);
  const p = input;
  requireValue(p.meta.isa === 'x86-64', 'MVP 尚未支持 Thumb-2 负载');
  requireValue(p.meta.syntax !== 'unified', 'x86-64 syntax 必须为 att 或 intel');
  const bound = 1n << 64n;
  const address = (value: string) => { const n = BigInt(value); requireValue(n >= 0n && n < bound, `地址或寄存器越界：${value}`); return n; };
  const regions = p.initial.memory.map(m => {
    const start = address(m.addr), end = start + BigInt(m.size);
    requireValue(start % 4096n === 0n && end <= bound, `内存页未对齐或越界：${m.addr}`);
    requireValue(/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(m.bytes), '内存 bytes 必须为规范 Base64');
    requireValue(m.bytes.length <= Math.ceil(m.size / 3) * 4, `内存数据超过映射大小：${m.addr}`);
    requireValue(btoa(atob(m.bytes)) === m.bytes, 'Base64 填充位不规范');
    const bytes = fromBase64(m.bytes);
    requireValue(bytes.length <= m.size, `内存数据超过映射大小：${m.addr}`);
    return { start, end, bytes, perms: m.perms };
  }).sort((a, b) => a.start < b.start ? -1 : 1);
  requireValue(p.initial.memory.reduce((n, m) => n + m.size, 0) <= 16777216, '映射总量超过 16 MiB');
  for (let i = 1; i < regions.length; i++) requireValue(regions[i - 1].end <= regions[i].start, '内存地址重叠');
  const find = (start: bigint, size = 1) => regions.find(r => start >= r.start && start + BigInt(size) <= r.end);
  const ranges = p.code.map(instruction => {
    const start = address(instruction.addr), bytes = bytesFromHex(instruction.bytes);
    requireValue(bytes.length <= 15, 'x86-64 指令不能超过 15 字节');
    const region = find(start, bytes.length);
    requireValue(region && region.perms.includes('x'), `指令不在可执行映射：${instruction.addr}`);
    const offset = Number(start - region.start);
    requireValue(bytes.every((b, i) => b === (region.bytes[offset + i] ?? 0)), `code 与内存 bytes 不一致：${instruction.addr}`);
    return { start, end: start + BigInt(bytes.length) };
  }).sort((a, b) => a.start < b.start ? -1 : 1);
  for (let i = 1; i < ranges.length; i++) requireValue(ranges[i - 1].end <= ranges[i].start, '指令地址重叠');
  const entry = address(p.meta.entry);
  requireValue(entry !== 0n && ranges.some(r => r.start === entry), '入口必须是非零有效指令地址');
  for (const stop of p.execution.stop_before) {
    const n = address(stop);
    const region = find(n);
    requireValue(n !== 0n && region && region.perms.includes('x'), '结束地址必须在可执行映射内且非零');
    requireValue(region.bytes[Number(n - region.start)] === 0xcc, '结束地址必须有真实 int3（cc）守卫；它不会被执行');
    requireValue(!ranges.some(r => n > r.start && n < r.end), '结束地址不能位于指令中间');
  }
  const seen = new Set<string>();
  for (const [name, value] of Object.entries(p.initial.regs)) {
    const info = registerInfo(name);
    requireValue(info, `未知寄存器：${name}`);
    requireValue(!seen.has(info.canonical), `初始寄存器别名冲突：${name}`); seen.add(info.canonical);
    requireValue(address(value) < (1n << BigInt(info.bits)), `寄存器位宽越界：${name}`);
    if (info.canonical === 'rip') requireValue(BigInt(value) === entry, '初始 RIP 与 entry 冲突');
  }
  const sp = p.initial.regs.rsp ?? p.initial.regs.esp;
  requireValue(sp && regions.some(r => r.perms.includes('w') && BigInt(sp) > r.start && BigInt(sp) <= r.end), '初始 RSP 必须指向可写栈区域（允许顶端边界）');
  for (const name of p.ui?.watch_regs ?? []) requireValue(registerInfo(name), `未知观察寄存器：${name}`);
  if (p.ui?.stack_window) requireValue(registerInfo(p.ui.stack_window.around), '未知栈窗口基准寄存器');
  return p;
}
