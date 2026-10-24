import type { StepTrace } from './types';
export interface CpuEngine {
  reg_write_i64(reg: number, value: bigint): void;
  reg_read_i64(reg: number): bigint;
  mem_map(address: bigint, size: number, perms: number): void;
  mem_protect(address: bigint, size: number, perms: number): void;
  mem_write(address: bigint, bytes: Uint8Array): void;
  mem_read(address: bigint, size: number): Uint8Array;
  emu_start(begin: bigint, until: bigint, timeout: number, count: number): void;
  /** 取走最近一次 step 的原始轨迹；未实现该方法的引擎返回 undefined。 */
  last_trace?(): StepTrace | undefined;
  close(): void;
}
export interface EngineModule {
  Cpu: new (arch: number, mode: number) => CpuEngine;
  ARCH_X86: number;
  MODE_64: number;
  PROT_ALL: number;
  PROT_READ: number;
  PROT_WRITE: number;
  PROT_EXEC: number;
  [register: `X86_REG_${string}`]: number;
}
