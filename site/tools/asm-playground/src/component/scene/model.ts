// 场景视图模型：把负载、快照和上一步快照整理成组件只负责画的样子。
// 这里不含任何猜测，所有“读了谁、写了谁”都来自引擎发布的 StepEvent。

import { registerInfo, type Hex, type PayloadV1 } from '../../payload/types';
import type {
  Access, AluEvent, ExecutorStatus, MemAccess, MemoryRow, RegAccess, Snapshot, StepEvent, StepFlow,
} from '../../engine/types';

export interface CodeLine {
  addr: Hex;
  bytes: string;
  text: string;
}

export interface NarrationChip {
  label: string;
  value: string;
  tone: 'read' | 'write' | 'plain';
}

export interface Narration {
  text: string;
  chips: NarrationChip[];
}

export interface SceneModel {
  lines: CodeLine[];
  index: number;
  status: ExecutorStatus;
  event?: StepEvent;
  current?: CodeLine;
  next?: CodeLine;
  changed: Set<string>;
  written: Set<string>;
  focus: Set<string>;
  reads: Access[];
  writes: Access[];
  mem: MemoryRow[];
  pointers: { rsp?: Hex; rbp?: Hex };
  narration: Narration;
}

const ALU_SIGN: Record<string, string> = {
  add: '+', adc: '+', sub: '−', sbb: '−', cmp: '−', and: '&', or: '|', xor: '^',
  test: '&', '*': '×', shl: '<<', shr: '>>', sar: '>>', rol: 'rol', ror: 'ror', rcl: 'rcl', rcr: 'rcr',
};

/** 小数字用十进制更好读，大数字与地址一律十六进制。 */
export function number(hex: Hex, bits: number): string {
  const value = BigInt.asUintN(bits, BigInt(hex));
  if (value <= 0xffffn) return value.toString(10);
  return `0x${value.toString(16).padStart(bits / 4, '0')}`;
}

export function byteGroups(bytes: string): string[] {
  return bytes.match(/../g) ?? [];
}

/** 8 字节行按小端解释成 64 位整数，只用于展示这一整行的值。 */
export function rowValue(bytes: string | null): string | null {
  const groups = bytes ? byteGroups(bytes) : [];
  if (groups.length !== 8) return null;
  return `0x${[...groups].reverse().join('')}`;
}

export function accessAddress(access: Access): Hex | undefined {
  return access.kind === 'mem' ? access.addr : undefined;
}

export function accessValue(access: Access): string {
  if (access.kind === 'reg') return number(access.value, access.bits);
  return number(`0x${[...byteGroups(access.bytes)].reverse().join('') || '0'}`, access.size * 8);
}

export function isMem(access: Access): access is MemAccess {
  return access.kind === 'mem';
}

export function isReg(access: Access): access is RegAccess {
  return access.kind === 'reg';
}

/** 同一规范寄存器的别名归并，比如 eax 与 rax。 */
export function canonical(name: string): string {
  return registerInfo(name)?.canonical ?? name;
}

function describeAlu(alu: AluEvent, dest: string | undefined): string {
  const sign = ALU_SIGN[alu.op] ?? alu.op;
  const text = `${number(alu.lhs, alu.bits)} ${sign} ${number(alu.rhs, alu.bits)} = ${number(alu.result, alu.bits)}`;
  if (dest) return `${text}，写回 ${dest.toUpperCase()}`;
  return alu.op === 'cmp' || alu.op === 'test' ? `${text}，只更新条件标志` : text;
}

function describeFlow(flow: StepFlow): string | undefined {
  switch (flow.kind) {
    case 'call':
      return `把返回地址 ${flow.returnTo} 压栈，跳进 ${flow.target}`;
    case 'return':
      return `从栈上取回 ${flow.target}，回到调用点`;
    case 'branch':
      return flow.taken ? `条件成立，跳到 ${flow.target}` : '条件不成立，继续往下走';
    case 'jump':
      return `跳到 ${flow.target}`;
    default:
      return undefined;
  }
}

function describe(event: StepEvent): Narration {
  const writes = event.writes;
  const reads = event.reads;
  const memWrite = writes.filter(isMem).at(-1);
  const memRead = reads.filter(isMem).at(-1);
  const regWrite = writes.filter(isReg).at(-1);
  const regRead = reads.filter(isReg).at(-1);
  const flow = describeFlow(event.flow);
  const chips: NarrationChip[] = [];
  let text = '';

  const push = (label: string, value: string, tone: NarrationChip['tone']) => chips.push({ label, value, tone });

  if (event.alu) {
    text = describeAlu(event.alu, regWrite?.name);
    if (regRead) push(regRead.name.toUpperCase(), number(regRead.value, regRead.bits), 'read');
    if (regWrite) push(regWrite.name.toUpperCase(), number(regWrite.value, regWrite.bits), 'write');
    return { text, chips };
  }

  if (event.text.startsWith('call') || event.text.startsWith('ret') || event.flow.kind !== 'step') {
    text = flow ?? event.text;
    if (memWrite) { push('压栈', memWrite.addr, 'write'); text = `保存返回地址：RSP 减 ${memWrite.size}，把返回地址写进内存单元 ${memWrite.addr}`; }
    if (memRead && event.flow.kind === 'return') { push('返回地址', memRead.addr, 'read'); text = `从 ${memRead.addr} 取回返回地址 ${event.flow.target}，RSP 加 ${memRead.size}`; }
    return { text, chips };
  }

  if (event.text.startsWith('push')) {
    text = `RSP 减 ${memWrite?.size ?? 8}，把 ${regRead ? regRead.name.toUpperCase() : '值'} 复制到 ${memWrite?.addr}`;
    if (regRead) push(regRead.name.toUpperCase(), number(regRead.value, regRead.bits), 'read');
    const rspWrite = writes.filter(isReg).find(access => access.canonical === 'rsp');
    if (rspWrite) push('RSP', rspWrite.value, 'write');
    return { text, chips };
  }

  if (event.text.startsWith('pop')) {
    text = `从栈顶内存单元 ${memRead?.addr ?? ''} 弹出 ${memRead ? accessValue(memRead) : '值'}，RSP 加 ${memRead?.size ?? 8}`;
    if (memRead) push('栈顶', memRead.addr, 'read');
    if (regWrite) push(regWrite.name.toUpperCase(), number(regWrite.value, regWrite.bits), 'write');
    return { text, chips };
  }

  if (event.text.startsWith('leave')) {
    text = '撤掉栈帧：RSP 回到 RBP，再弹出调用者的 RBP';
    if (regWrite) push(regWrite.name.toUpperCase(), number(regWrite.value, regWrite.bits), 'write');
    return { text, chips };
  }

  if (memWrite && regRead) {
    text = `把 ${regRead.name.toUpperCase()} 的 ${number(regRead.value, regRead.bits)} 写进内存单元 ${memWrite.addr}，占 ${memWrite.size} 个字节`;
    push(regRead.name.toUpperCase(), number(regRead.value, regRead.bits), 'read');
    push('内存', memWrite.addr, 'write');
    return { text, chips };
  }

  if (memWrite) {
    text = `把 ${accessValue(memWrite)} 写进内存单元 ${memWrite.addr}，占 ${memWrite.size} 个字节`;
    push('内存', memWrite.addr, 'write');
    return { text, chips };
  }

  if (memRead && regWrite) {
    text = `从内存单元 ${memRead.addr} 读出 ${accessValue(memRead)}，放进 ${regWrite.name.toUpperCase()}`;
    push('内存', memRead.addr, 'read');
    push(regWrite.name.toUpperCase(), number(regWrite.value, regWrite.bits), 'write');
    return { text, chips };
  }

  if (regWrite && regRead) {
    text = `把 ${regRead.name.toUpperCase()} 的 ${number(regRead.value, regRead.bits)} 复制到 ${regWrite.name.toUpperCase()}`;
    push(regRead.name.toUpperCase(), number(regRead.value, regRead.bits), 'read');
    push(regWrite.name.toUpperCase(), number(regWrite.value, regWrite.bits), 'write');
    return { text, chips };
  }

  if (regWrite) {
    text = `把 ${accessValue(regWrite)} 放进 ${regWrite.name.toUpperCase()}（${regWrite.bits} 位）`;
    push(regWrite.name.toUpperCase(), number(regWrite.value, regWrite.bits), 'write');
    return { text, chips };
  }

  text = event.text || '这条指令没有可观察的数据变化';
  return { text, chips };
}

function emptyNarration(status: ExecutorStatus, index: number): Narration {
  if (status === 'error') return { text: '这一步没有执行成功，画面停在最后一条完整状态上', chips: [] };
  if (status === 'finished') return { text: '已抵达场景结束地址', chips: [] };
  if (index === 0) return { text: '按单步开始：每一步都会说清读了哪儿、写到了哪儿', chips: [] };
  return { text: '等待下一步', chips: [] };
}

/** 负载里的地址可能不补零，事件里的地址一律 16 位；先归一化再比。 */
export function normalizeAddress(value: string): Hex {
  return `0x${BigInt(value).toString(16).padStart(16, '0')}`;
}

export function buildScene(payload: PayloadV1, snapshot: Snapshot | undefined, previous?: Snapshot): SceneModel {
  const lines: CodeLine[] = payload.code.map(line => ({ ...line, addr: normalizeAddress(line.addr) }));
  const find = (addr: string | undefined) => (addr ? lines.find(line => line.addr === normalizeAddress(addr)) : undefined);
  const event = snapshot?.steps ? snapshot.event : undefined;
  const status = snapshot?.status ?? 'ready';
  const byCanonical = (name: string) => canonical(name);

  const written = new Set<string>();
  const changed = new Set<string>();
  const focus = new Set<string>(['rip']);
  for (const access of event?.writes ?? []) {
    if (access.kind === 'reg') { written.add(byCanonical(access.name)); focus.add(access.name); }
  }
  for (const access of event?.reads ?? []) {
    if (access.kind === 'reg') focus.add(access.name);
    else focus.add('mem');
  }
  for (const name of written) {
    const before = previous ? BigInt(previous.regs[name] ?? '0x0') : undefined;
    const after = snapshot ? BigInt(snapshot.regs[name] ?? '0x0') : undefined;
    if (before === undefined || after === undefined || before !== after) changed.add(name);
  }

  const nextLine = find(snapshot?.regs.rip);
  return {
    lines,
    index: snapshot?.steps ?? 0,
    status,
    event,
    current: event ? find(event.pc) : undefined,
    next: nextLine,
    changed,
    written,
    focus,
    reads: event?.reads ?? [],
    writes: event?.writes ?? [],
    mem: snapshot?.memory ?? [],
    pointers: snapshot?.pointers ?? {},
    narration: event ? describe(event) : emptyNarration(status, snapshot?.steps ?? 0),
  };
}
