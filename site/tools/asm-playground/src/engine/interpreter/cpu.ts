// CpuEngine 门面：实现 runtime 依赖的八个方法；寄存器号与 x86 编码序一致，
// rip=16、rflags=17。emu_start 每调用至多执行 count 条，count=0 表示运行至出口。

import type { CpuEngine, EngineModule } from '../engine';
import type { StepTrace } from '../types';
import { EngineError, Memory, Registers, RIP_SLOT, RFLAGS_SLOT } from './core';
import { Executor } from './execute';

export const X86_REG_RIP_ID = RIP_SLOT;
export const X86_REG_RFLAGS_ID = RFLAGS_SLOT;

export class X86Cpu implements CpuEngine {
  private regs = new Registers();
  private mem = new Memory();
  private executor = new Executor(this.regs, this.mem);
  private trace: StepTrace | undefined;

  constructor(arch: number, mode: number) {
    if (arch !== x86Engine.ARCH_X86 || mode !== x86Engine.MODE_64) throw new EngineError('仅支持 x86-64');
  }

  reg_write_i64(reg: number, value: bigint): void {
    const v = BigInt.asUintN(64, value);
    if (reg === X86_REG_RIP_ID) this.regs.rip = v;
    else if (reg === X86_REG_RFLAGS_ID) this.regs.rflags = v;
    else if (reg >= 0 && reg < 16) this.regs.gpr[reg] = v;
    else throw new EngineError(`未知寄存器号：${reg}`);
  }

  reg_read_i64(reg: number): bigint {
    if (reg === X86_REG_RIP_ID) return this.regs.rip;
    if (reg === X86_REG_RFLAGS_ID) return this.regs.rflags;
    if (reg >= 0 && reg < 16) return this.regs.gpr[reg];
    throw new EngineError(`未知寄存器号：${reg}`);
  }

  mem_map(address: bigint, size: number, perms: number): void {
    this.mem.map(address, size, perms);
  }
  mem_protect(address: bigint, size: number, perms: number): void {
    this.mem.protect(address, size, perms);
  }
  mem_write(address: bigint, bytes: Uint8Array): void {
    this.mem.write(address, bytes);
  }
  mem_read(address: bigint, size: number): Uint8Array {
    return this.mem.read(address, size).slice();
  }

  emu_start(begin: bigint, until: bigint, _timeout: number, count: number): void {
    this.regs.rip = BigInt.asUintN(64, begin);
    let executed = 0;
    while (count === 0 || executed < count) {
      if (this.regs.rip === until) break;
      this.trace = this.executor.step();
      executed++;
    }
  }

  last_trace(): StepTrace | undefined {
    const trace = this.trace;
    this.trace = undefined;
    return trace;
  }

  close(): void { /* 纯 TS 实现，无需清理 */ }
}

// 寄存器号按 REGISTERS（payload/types.ts）命名导出，runtime 通过 X86_REG_* 查表。
export const x86Engine: EngineModule = {
  Cpu: X86Cpu,
  ARCH_X86: 4, MODE_64: 8,
  PROT_ALL: 7, PROT_READ: 1, PROT_WRITE: 2, PROT_EXEC: 4,
  X86_REG_RAX: 0, X86_REG_RCX: 1, X86_REG_RDX: 2, X86_REG_RBX: 3,
  X86_REG_RSP: 4, X86_REG_RBP: 5, X86_REG_RSI: 6, X86_REG_RDI: 7,
  X86_REG_R8: 8, X86_REG_R9: 9, X86_REG_R10: 10, X86_REG_R11: 11,
  X86_REG_R12: 12, X86_REG_R13: 13, X86_REG_R14: 14, X86_REG_R15: 15,
  X86_REG_RIP: X86_REG_RIP_ID, X86_REG_RFLAGS: X86_REG_RFLAGS_ID,
};
