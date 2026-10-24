import type { Hex } from '../payload/types';

export type ExecutorStatus = 'ready' | 'running' | 'paused' | 'finished' | 'error';

/** 寄存器访问：保留别名与位宽，UI 据此区分 eax 与 rax 的视图关系。 */
export interface RegAccess {
  kind: 'reg';
  name: string;
  canonical: string;
  bits: number;
  value: Hex;
}

/** 内存访问：地址、字节数与真实字节（小端存储序，hex 原样）。 */
export interface MemAccess {
  kind: 'mem';
  addr: Hex;
  size: number;
  bytes: string;
}

export type Access = RegAccess | MemAccess;

export type FlowKind = 'step' | 'jump' | 'branch' | 'call' | 'return';

export interface StepFlow {
  kind: FlowKind;
  taken: boolean;
  /** 实际进入的地址（jump/call/return 必然存在）。 */
  target?: Hex;
  /** call 保存的返回地址。 */
  returnTo?: Hex;
  /** 条件跳转未采用时的顺序地址。 */
  fallthrough?: Hex;
}

export interface AluEvent {
  op: string;
  lhs: Hex;
  rhs: Hex;
  result: Hex;
  bits: number;
}

/** 一条真实指令的单步事实。由解释器采集，UI 不靠反汇编文本猜测。 */
export interface StepEvent {
  index: number;
  pc: Hex;
  next: Hex;
  rip: Hex;
  length: number;
  text: string;
  reads: Access[];
  writes: Access[];
  flow: StepFlow;
  alu?: AluEvent;
  flags: { changed: string[] };
}

/** 解释器交出的一次单步原始轨迹（地址保持 BigInt）。 */
export interface StepTrace {
  pc: bigint;
  length: number;
  next: bigint;
  rip: bigint;
  reads: Access[];
  writes: Access[];
  alu?: AluEvent;
  flow: StepFlow;
}

/** 有界内存观测行：bytes 为 null 表示该地址未映射，不填 0。 */
export interface MemoryRow {
  addr: Hex;
  bytes: string | null;
}

export interface Snapshot {
  regs: Record<string, Hex>;
  stack: { addr: Hex; bytes: string | null }[];
  steps: number;
  status: ExecutorStatus;
  reason?: string;
  /** 最近一次单步的真实事件；就绪态与失败步不发布。 */
  event?: StepEvent;
  /** 覆盖本步真实访问与 RSP/RBP 邻域的有界窗口。 */
  memory?: MemoryRow[];
  pointers?: { rsp: Hex; rbp: Hex };
}
