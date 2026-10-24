import { fromBase64, registerInfo, REGISTERS, toHex, type Hex, type PayloadV1 } from '../payload/types';
import { validatePayload } from '../payload/validate';
import type { CpuEngine, EngineModule } from './engine';
import type { Access, ExecutorStatus, MemoryRow, Snapshot, StepEvent, StepTrace } from './types';

// 标志位的观察名与位号：只报告这一步真实变化的标志。
const FLAGS: [number, string][] = [[0, 'CF'], [2, 'PF'], [4, 'AF'], [6, 'ZF'], [7, 'SF'], [10, 'DF'], [11, 'OF']];

function changedFlags(before: bigint, after: bigint): string[] {
  return FLAGS.filter(([bit]) => ((before >> BigInt(bit)) & 1n) !== ((after >> BigInt(bit)) & 1n)).map(([, name]) => name);
}

function hex(value: bigint): Hex {
  return toHex(BigInt.asUintN(64, value));
}

function bytesHex(bytes: Uint8Array): string {
  return [...bytes].map(byte => byte.toString(16).padStart(2, '0')).join('');
}

export class Runtime {
  private cpu!: CpuEngine;
  private closed = true;
  private until = 1n;
  private failedSnapshot: Snapshot | undefined;
  private lastEvent: StepEvent | undefined;
  status: ExecutorStatus = 'ready';
  reason: string | undefined;
  steps = 0;
  readonly payload: PayloadV1;
  constructor(private engine: EngineModule, payload: unknown) {
    this.payload = validatePayload(payload);
    this.reset();
  }
  private id(name: string): number {
    return this.engine[`X86_REG_${name.toUpperCase()}`];
  }
  private read(name: string): bigint {
    return BigInt.asUintN(64, this.cpu.reg_read_i64(this.id(name)));
  }
  reset(): void {
    if (this.failedSnapshot) throw new Error('异常后的引擎模块需要重建；请销毁 Worker 后重新加载');
    this.destroy();
    this.cpu = new this.engine.Cpu(this.engine.ARCH_X86, this.engine.MODE_64);
    this.closed = false;
    this.until = BigInt(this.payload.execution.stop_before[0]);
    try {
      for (const memory of this.payload.initial.memory) {
        const addr = BigInt(memory.addr);
        this.cpu.mem_map(addr, memory.size, this.engine.PROT_ALL);
        const bytes = fromBase64(memory.bytes);
        if (bytes.length) this.cpu.mem_write(addr, bytes);
        const perms = (memory.perms.includes('r') ? this.engine.PROT_READ : 0)
          | (memory.perms.includes('w') ? this.engine.PROT_WRITE : 0)
          | (memory.perms.includes('x') ? this.engine.PROT_EXEC : 0);
        this.cpu.mem_protect(addr, memory.size, perms);
      }
      for (const name of REGISTERS) this.cpu.reg_write_i64(this.id(name), name === 'rflags' ? 0x202n : 0n);
      for (const [name, value] of Object.entries(this.payload.initial.regs)) {
        const info = registerInfo(name)!;
        this.cpu.reg_write_i64(this.id(info.canonical), BigInt(value));
      }
      this.cpu.reg_write_i64(this.id('rip'), BigInt(this.payload.meta.entry));
      this.steps = 0;
      this.reason = undefined;
      this.lastEvent = undefined;
      this.status = this.atStop() ? 'finished' : 'ready';
    } catch (error) {
      this.destroy();
      throw error;
    }
  }
  private atStop(): boolean {
    const pc = this.read('rip');
    return this.payload.execution.stop_before.some(stop => BigInt(stop) === pc);
  }
  readMemory(address: bigint, size: number): Uint8Array {
    return this.cpu.mem_read(address, size);
  }
  /** 有界观测窗口：本步真实访问 + RSP/RBP 邻域，8 字节一行，未映射行留空。 */
  private memoryWindow(): MemoryRow[] {
    const rsp = this.read('rsp'), rbp = this.read('rbp');
    const spans: [bigint, bigint][] = [[rsp, rsp + 32n], [rbp, rbp + 16n]];
    for (const access of [...(this.lastEvent?.reads ?? []), ...(this.lastEvent?.writes ?? [])]) {
      if (access.kind === 'mem') spans.push([BigInt(access.addr), BigInt(access.addr) + BigInt(access.size)]);
    }
    let low = spans[0][0], high = spans[0][1];
    for (const [start, end] of spans) {
      if (start < low) low = start;
      if (end > high) high = end;
    }
    // 视窗上下各留一行上下文，行数有界，避免一次渲染整段映射。
    low = (low & ~7n) - 8n;
    high = ((high + 7n) & ~7n) + 8n;
    if (high - low > 8n * 48n) high = low + 8n * 48n;
    const rows: MemoryRow[] = [];
    for (let addr = low; addr < high; addr += 8n) {
      let bytes: string | null = null;
      try {
        bytes = bytesHex(this.readMemory(addr, 8));
      } catch { /* 未映射行保持 null，不填 0。 */ }
      rows.push({ addr: hex(addr), bytes });
    }
    return rows;
  }
  snapshot(): Snapshot {
    if (this.failedSnapshot) return this.failedSnapshot;
    const regs = Object.fromEntries(REGISTERS.map(name => [name, toHex(this.read(name))]));
    const around = this.payload.ui?.stack_window?.around ?? 'rsp';
    const info = registerInfo(around)!;
    const base = BigInt.asUintN(info.bits, BigInt(regs[info.canonical])) & ~7n;
    const stack = Array.from({ length: this.payload.ui?.stack_window?.rows ?? 8 }, (_, i) => {
      const addr = base + BigInt(i * 8);
      let bytes: string | null = null;
      try {
        bytes = bytesHex(this.readMemory(addr, 8));
      } catch { /* Unmapped UI rows are not emulated CPU reads. */ }
      return { addr: toHex(addr), bytes };
    });
    const rsp = this.read('rsp'), rbp = this.read('rbp');
    return {
      regs, stack, steps: this.steps, status: this.status,
      ...(this.reason ? { reason: this.reason } : {}),
      ...(this.lastEvent ? { event: this.lastEvent } : {}),
      memory: this.memoryWindow(),
      pointers: { rsp: hex(rsp), rbp: hex(rbp) },
    };
  }
  private eventFrom(trace: StepTrace, flagsBefore: bigint): StepEvent {
    const line = this.payload.code.find(entry => BigInt(entry.addr) === trace.pc);
    const reads: Access[] = trace.reads.map(access => ({ ...access }));
    return {
      index: this.steps,
      pc: hex(trace.pc),
      next: hex(trace.next),
      rip: hex(trace.rip),
      length: trace.length,
      text: line?.text ?? '',
      reads,
      writes: trace.writes.map(access => ({ ...access })),
      flow: { ...trace.flow },
      ...(trace.alu ? { alu: { ...trace.alu } } : {}),
      flags: { changed: changedFlags(flagsBefore, this.read('rflags')) },
    };
  }
  step(): Snapshot {
    if (this.closed) throw new Error('执行器已销毁');
    if (this.status === 'error' || this.status === 'finished') return this.snapshot();
    const before = this.snapshot(), pc = this.read('rip'), flagsBefore = this.read('rflags');
    try {
      if (!this.payload.code.some(line => BigInt(line.addr) === pc)) throw new Error('PC 未对应负载中的指令');
      this.cpu.emu_start(pc, this.until, 0, 1);
      this.steps++;
      this.status = this.atStop() ? 'finished' : 'paused';
      this.reason = undefined;
      const trace = this.cpu.last_trace?.();
      if (trace && trace.pc === pc) this.lastEvent = this.eventFrom(trace, flagsBefore);
      return this.snapshot();
    } catch (error) {
      this.status = 'error';
      this.reason = `PC ${toHex(pc)}：${String(error)}`;
      this.failedSnapshot = { ...before, status: 'error', reason: this.reason };
      return this.failedSnapshot;
    }
  }
  destroy(): void {
    if (this.closed) return;
    this.closed = true;
    // CPU 故障后的状态不再可信：组件层会丢弃整个 Worker 重建，而不是在原实例上重置。
    if (!this.failedSnapshot) this.cpu.close();
  }
}
