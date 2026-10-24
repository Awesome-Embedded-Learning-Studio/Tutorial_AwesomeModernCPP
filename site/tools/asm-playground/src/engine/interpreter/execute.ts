// 指令执行：解码结果逐条落到寄存器堆与内存。rflags 语义按原生 GDB 轨迹校准，
// 特殊取舍见 core.ts 注释（逻辑运算不动 AF、乘法按低位结果置 SZP 等）。

import {
  Memory, Registers, arithAdd, arithSub, incDecFlags, loadRflags, logicFlags, mask,
  mulFlags, setSzp, signed, signBit,
  FLAG_CF, FLAG_DF, FLAG_OF, FLAG_PF, FLAG_SF, FLAG_ZF,
} from './core';
import { decode, type Decoded, type MemOp, type Operand } from './decode';
import type { Access, AluEvent, StepFlow, StepTrace } from '../types';
import type { Hex } from '../../payload/types';

const M64 = 0xffffffffffffffffn;
// x86 编码序里的栈/串指针与计数寄存器。
const RAX = 0, RCX = 1, RDX = 2, RSP = 4, RBP = 5, RSI = 6, RDI = 7;

const NAMES64 = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15'];
const NAMES32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi', 'r8d', 'r9d', 'r10d', 'r11d', 'r12d', 'r13d', 'r14d', 'r15d'];
const NAMES16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di', 'r8w', 'r9w', 'r10w', 'r11w', 'r12w', 'r13w', 'r14w', 'r15w'];
const NAMES8 = ['al', 'cl', 'dl', 'bl', 'spl', 'bpl', 'sil', 'dil', 'r8b', 'r9b', 'r10b', 'r11b', 'r12b', 'r13b', 'r14b', 'r15b'];
const NAMES8H = ['ah', 'ch', 'dh', 'bh'];

export function registerName(slot: number, size: number, high = false): string {
  if (size === 64) return NAMES64[slot];
  if (size === 32) return NAMES32[slot];
  if (size === 16) return NAMES16[slot];
  return high ? NAMES8H[slot] : NAMES8[slot];
}

function hex(value: bigint, bits: number): Hex {
  return `0x${BigInt.asUintN(bits, value).toString(16).padStart(bits / 4, '0')}`;
}

function hexBytes(value: bigint, size: number): string {
  let out = '';
  for (let i = 0; i < size; i++) out += ((value >> BigInt(i * 8)) & 0xffn).toString(16).padStart(2, '0');
  return out;
}

export class Executor {
  private nextRip = 0n;
  private reads: Access[] = [];
  private writes: Access[] = [];
  private alu: AluEvent | undefined;
  constructor(private regs: Registers, private mem: Memory) { }

  step(): StepTrace {
    const pc = this.regs.rip;
    const ins = decode(this.mem, pc);
    this.nextRip = pc + BigInt(ins.length);
    this.regs.rip = this.nextRip;
    this.reads = [];
    this.writes = [];
    this.alu = undefined;
    this.execute(ins);
    const rip = this.regs.rip;
    return {
      pc, length: ins.length, next: this.nextRip, rip,
      reads: this.reads, writes: this.writes, alu: this.alu,
      flow: this.flowOf(ins, rip),
    };
  }

  private flowOf(ins: Decoded, rip: bigint): StepFlow {
    const next = this.nextRip;
    switch (ins.op) {
      case 'call': case 'callind':
        return { kind: 'call', taken: true, target: hex(rip, 64), returnTo: hex(next, 64) };
      case 'ret':
        return { kind: 'return', taken: true, target: hex(rip, 64) };
      case 'jmp': case 'jmpind':
        return { kind: 'jump', taken: true, target: hex(rip, 64) };
      case 'jcc': case 'loop': case 'loope': case 'loopne': case 'jrcxz': {
        const taken = rip !== next;
        return { kind: 'branch', taken, target: hex(rip, 64), fallthrough: hex(next, 64) };
      }
      default:
        return { kind: 'step', taken: false };
    }
  }

  private noteRead(value: Access): void {
    this.reads.push(value);
  }

  private noteWrite(value: Access): void {
    this.writes.push(value);
  }

  private noteReg(slot: number, size: number, value: bigint, high = false): void {
    this.noteWrite({ kind: 'reg', name: registerName(slot, size, high), canonical: NAMES64[slot], bits: size, value: hex(value, size) });
  }

  /** 只有执行器的数据访问走这里；解码取字节与 UI 观察内存不计入事件。 */
  private readMemory(addr: bigint, size: number): bigint {
    const value = this.mem.readU(addr, size);
    this.noteRead({ kind: 'mem', addr: hex(addr, 64), size, bytes: hexBytes(value, size) });
    return value;
  }

  private writeMemory(addr: bigint, size: number, value: bigint): void {
    this.mem.writeU(addr, size, value);
    this.noteWrite({ kind: 'mem', addr: hex(addr, 64), size, bytes: hexBytes(value, size) });
  }

  private noteAlu(op: string, lhs: bigint, rhs: bigint, result: bigint, size: number): void {
    this.alu = { op, lhs: hex(lhs, size), rhs: hex(rhs, size), result: hex(result, size), bits: size };
  }

  private effectiveAddress(op: MemOp): bigint {
    let addr = op.disp;
    if (op.riprel) addr += this.nextRip;
    if (op.base !== null) addr += this.regs.gpr[op.base];
    if (op.index !== null) addr += this.regs.gpr[op.index] * BigInt(op.scale);
    return op.addr32 ? BigInt.asUintN(32, addr) : BigInt.asUintN(64, addr);
  }

  private readOperand(op: Operand): bigint {
    if (op.type === 'imm') return op.value;
    if (op.type === 'reg') {
      const value = this.regs.read(op.slot, op.size, op.high);
      this.noteRead({ kind: 'reg', name: registerName(op.slot, op.size, op.high), canonical: NAMES64[op.slot], bits: op.size, value: hex(value, op.size) });
      return value;
    }
    return this.readMemory(this.effectiveAddress(op), op.size / 8);
  }

  private writeOperand(op: Operand, value: bigint): void {
    if (op.type === 'reg') {
      this.regs.write(op.slot, op.size, value, op.high);
      this.noteReg(op.slot, op.size, value, op.high);
    } else if (op.type === 'mem') {
      this.writeMemory(this.effectiveAddress(op), op.size / 8, value);
    }
  }

  private imm(op: Operand): bigint {
    if (op.type === 'imm') return op.value;
    throw new Error('内部错误：操作数应为立即数');
  }

  private conditionMet(cc: number): boolean {
    const overflow = this.regs.flag(FLAG_OF);
    const carry = this.regs.flag(FLAG_CF);
    const zero = this.regs.flag(FLAG_ZF);
    const sign = this.regs.flag(FLAG_SF);
    const parity = this.regs.flag(FLAG_PF);
    switch (cc) {
      case 0:
        return overflow;
      case 1:
        return !overflow;
      case 2:
        return carry;
      case 3:
        return !carry;
      case 4:
        return zero;
      case 5:
        return !zero;
      case 6:
        return carry || zero;
      case 7:
        return !carry && !zero;
      case 8:
        return sign;
      case 9:
        return !sign;
      case 10:
        return parity;
      case 11:
        return !parity;
      case 12:
        return sign !== overflow;
      case 13:
        return sign === overflow;
      case 14:
        return zero || sign !== overflow;
      default:
        return !zero && sign === overflow;
    }
  }

  private push(value: bigint, size = 64): void {
    const bytes = size / 8;
    this.regs.gpr[RSP] = (this.regs.gpr[RSP] - BigInt(bytes)) & M64;
    this.noteReg(RSP, 64, this.regs.gpr[RSP]);
    this.writeMemory(this.regs.gpr[RSP], bytes, value);
  }

  private pop(size = 64): bigint {
    const bytes = size / 8;
    const value = this.readMemory(this.regs.gpr[RSP], bytes);
    this.regs.gpr[RSP] = (this.regs.gpr[RSP] + BigInt(bytes)) & M64;
    this.noteReg(RSP, 64, this.regs.gpr[RSP]);
    return value;
  }

  private execute(ins: Decoded): void {
    const { regs } = this;
    const size = ins.size;
    switch (ins.op) {
      case 'add': case 'or': case 'adc': case 'sbb': case 'and': case 'sub': case 'xor': case 'cmp': {
        const a = this.readOperand(ins.ops[0]), b = this.readOperand(ins.ops[1]);
        let result: bigint;
        if (ins.op === 'add') result = arithAdd(regs, a, b, size);
        else if (ins.op === 'adc') result = arithAdd(regs, a, b, size, regs.flag(FLAG_CF) ? 1n : 0n);
        else if (ins.op === 'sub' || ins.op === 'cmp') result = arithSub(regs, a, b, size);
        else if (ins.op === 'sbb') result = arithSub(regs, a, b, size, regs.flag(FLAG_CF) ? 1n : 0n);
        else {
          let value: bigint;
          if (ins.op === 'and') value = a & b;
          else if (ins.op === 'or') value = a | b;
          else value = a ^ b;
          result = logicFlags(regs, value, size);
        }
        this.noteAlu(ins.op, a, b, result, size);
        if (ins.op !== 'cmp') this.writeOperand(ins.ops[0], result);
        return;
      }
      case 'test': {
        const a = this.readOperand(ins.ops[0]), b = this.readOperand(ins.ops[1]);
        logicFlags(regs, a & b, size);
        this.noteAlu('test', a, b, a & b, size);
        return;
      }
      case 'mov':
        this.writeOperand(ins.ops[0], this.readOperand(ins.ops[1]));
        return;
      case 'lea':
        this.writeOperand(ins.ops[0], this.effectiveAddress(ins.ops[1] as MemOp));
        return;
      case 'inc': case 'dec': {
        const v = this.readOperand(ins.ops[0]);
        const result = incDecFlags(regs, v, size, ins.op === 'inc');
        this.noteAlu(ins.op === 'inc' ? '+' : '-', v, 1n, result, size);
        this.writeOperand(ins.ops[0], result);
        return;
      }
      case 'not': {
        const v = this.readOperand(ins.ops[0]);
        this.noteAlu('~', v, 0n, ~v & mask(size), size);
        this.writeOperand(ins.ops[0], ~v & mask(size));
        return;
      }
      case 'neg': {
        const v = this.readOperand(ins.ops[0]);
        this.noteAlu('neg', 0n, v, arithSub(regs, 0n, v, size), size);
        this.writeOperand(ins.ops[0], arithSub(regs, 0n, v, size));
        return;
      }
      case 'imul': {
        const lhs = signed(this.readOperand(ins.ops[1]), size);
        const rhs = signed(this.readOperand(ins.ops[2] ?? ins.ops[0]), size);
        const product = lhs * rhs;
        const low = BigInt.asUintN(size, product);
        mulFlags(regs, low, size, signed(low, size) !== product);
        this.noteAlu('*', ins.ops[1] ? this.readOperand(ins.ops[1]) : lhs, rhs, low, size);
        this.writeOperand(ins.ops[0], low);
        return;
      }
      case 'imul1': case 'mul': {
        const a = regs.read(RAX, size);
        this.noteRead({ kind: 'reg', name: registerName(RAX, size), canonical: NAMES64[RAX], bits: size, value: hex(a, size) });
        const b = this.readOperand(ins.ops[0]);
        const product = ins.op === 'mul' ? a * b : signed(a, size) * signed(b, size);
        const low = BigInt.asUintN(size, product);
        const high = BigInt.asUintN(size, product >> BigInt(size));
        const overflow = ins.op === 'mul' ? product > mask(size) : signed(low, size) !== product;
        // 字节乘法读 AL、写 AX，不接触 DX；其他位宽写入高低两个寄存器。
        if (size === 8) {
          regs.write(RAX, 16, product);
          this.noteReg(RAX, 16, product);
        } else {
          regs.write(RAX, size, low);
          regs.write(RDX, size, high);
          this.noteReg(RAX, size, low);
          this.noteReg(RDX, size, high);
        }
        this.noteAlu(ins.op === 'mul' ? '*' : '*', a, b, low, size);
        mulFlags(regs, low, size, overflow);
        return;
      }
      case 'div': case 'idiv': {
        this.divide(ins, ins.op === 'idiv');
        return;
      }
      case 'rol': case 'ror': case 'rcl': case 'rcr': case 'shl': case 'shr': case 'sar':
        this.shift(ins);
        return;
      case 'push':
        this.push(this.readOperand(ins.ops[0]), size);
        return;
      case 'pop':
        this.writeOperand(ins.ops[0], this.pop(size));
        return;
      case 'pushf':
        this.push(regs.rflags, size);
        return;
      case 'popf':
        loadRflags(regs, this.pop(size));
        return;
      case 'leave':
        regs.gpr[RSP] = regs.gpr[RBP];
        this.noteReg(RSP, 64, regs.gpr[RSP]);
        regs.gpr[RBP] = this.pop();
        return;
      case 'call':
        this.push(this.nextRip);
        regs.rip = this.nextRip + this.imm(ins.ops[0]);
        return;
      case 'callind': {
        const target = this.readOperand(ins.ops[0]);
        this.push(this.nextRip);
        regs.rip = target;
        return;
      }
      case 'ret':
        regs.rip = this.pop();
        if (ins.ops.length) regs.gpr[RSP] = (regs.gpr[RSP] + this.imm(ins.ops[0])) & M64;
        return;
      case 'jmp':
        regs.rip = this.nextRip + this.imm(ins.ops[0]);
        return;
      case 'jmpind':
        regs.rip = this.readOperand(ins.ops[0]);
        return;
      case 'jcc':
        if (this.conditionMet(ins.cond!)) regs.rip = this.nextRip + this.imm(ins.ops[0]);
        return;
      case 'cmov': {
        const v = this.readOperand(ins.ops[1]); // 内存读始终发生
        if (this.conditionMet(ins.cond!)) this.writeOperand(ins.ops[0], v);
        // 即使条件不成立，32 位 CMOV 也清零目的寄存器高 32 位。
        else if (size === 32) this.writeOperand(ins.ops[0], this.readOperand(ins.ops[0]));
        return;
      }
      case 'setcc':
        this.writeOperand(ins.ops[0], this.conditionMet(ins.cond!) ? 1n : 0n);
        return;
      case 'xchg': {
        const a = this.readOperand(ins.ops[0]), b = this.readOperand(ins.ops[1]);
        this.writeOperand(ins.ops[0], b);
        this.writeOperand(ins.ops[1], a);
        return;
      }
      case 'movzx':
        this.writeOperand(ins.ops[0], this.readOperand(ins.ops[1]));
        return;
      case 'movsx': {
        const src = ins.ops[1];
        if (src.type === 'imm') throw new Error('内部错误：MOVSX 源操作数必须有位宽');
        this.writeOperand(ins.ops[0], BigInt.asUintN(size, signed(this.readOperand(src), src.size)));
        return;
      }
      case 'bswap': {
        let r = 0n, x = this.readOperand(ins.ops[0]);
        for (let k = 0; k < size; k += 8) {
          r = (r << 8n) | (x & 0xffn);
          x >>= 8n;
        }
        this.writeOperand(ins.ops[0], r);
        return;
      }
      case 'sxt': {
        const src = size / 2;
        const narrow = regs.read(RAX, src);
        const widened = BigInt.asUintN(size, signed(narrow, src));
        regs.write(RAX, size, widened);
        this.noteRead({ kind: 'reg', name: registerName(RAX, src), canonical: NAMES64[RAX], bits: src, value: hex(narrow, src) });
        this.noteReg(RAX, size, widened);
        return;
      }
      case 'sxt2': {
        const fill = regs.read(RAX, size) & signBit(size) ? -1n : 0n;
        regs.write(RDX, size, fill);
        this.noteReg(RDX, size, fill);
        return;
      }
      case 'clc':
        regs.setFlag(FLAG_CF, false);
        return;
      case 'stc':
        regs.setFlag(FLAG_CF, true);
        return;
      case 'cmc':
        regs.setFlag(FLAG_CF, !regs.flag(FLAG_CF));
        return;
      case 'cld':
        regs.setFlag(FLAG_DF, false);
        return;
      case 'std':
        regs.setFlag(FLAG_DF, true);
        return;
      case 'loop': case 'loope': case 'loopne': {
        const rcx = (regs.gpr[RCX] - 1n) & M64;
        regs.gpr[RCX] = rcx;
        this.noteReg(RCX, 64, rcx);
        const zf = regs.flag(FLAG_ZF);
        const taken = rcx !== 0n && (ins.op === 'loop' || (ins.op === 'loope' ? zf : !zf));
        if (taken) regs.rip = this.nextRip + this.imm(ins.ops[0]);
        return;
      }
      case 'jrcxz':
        if (regs.gpr[RCX] === 0n) regs.rip = this.nextRip + this.imm(ins.ops[0]);
        return;
      case 'movs': case 'stos': case 'lods': case 'scas': case 'cmps': {
        const conditional = ins.op === 'scas' || ins.op === 'cmps';
        let first = true;
        while (true) {
          if (ins.rep) {
            if (regs.gpr[RCX] === 0n) break;
            regs.gpr[RCX] = (regs.gpr[RCX] - 1n) & M64;
          } else if (!first) break;
          this.stringStep(ins.op, size);
          first = false;
          if (ins.rep && conditional) {
            const z = regs.flag(FLAG_ZF);
            if (ins.rep === 'rep' ? !z : z) break;
          }
        }
        return;
      }
      case 'int3':
        throw new Error('int3 断点指令被执行');
      case 'nop':
        return;
      default:
        throw new Error(`不支持的指令：${ins.op}`);
    }
  }

  private shift(ins: Decoded): void {
    const { regs } = this;
    const size = ins.size;
    const raw = ins.countFromCl ? regs.read(RCX, 8) : this.readOperand(ins.ops[1]);
    const count = Number(raw & (size === 64 ? 63n : 31n));
    if (count === 0) return; // 计数为 0：不写回、不动标志
    const operandMask = mask(size);
    const bits = BigInt(size);
    const value = this.readOperand(ins.ops[0]) & operandMask;
    if (ins.op === 'shl' || ins.op === 'shr' || ins.op === 'sar') {
      let result: bigint, carry: boolean;
      if (ins.op === 'shl') {
        carry = count <= size && ((value >> BigInt(size - count)) & 1n) === 1n;
        result = (value << BigInt(count)) & operandMask;
      } else if (ins.op === 'shr') {
        carry = count <= size && ((value >> BigInt(count - 1)) & 1n) === 1n;
        result = value >> BigInt(count);
      } else {
        const signedValue = signed(value, size);
        carry = count < size ? ((value >> BigInt(count - 1)) & 1n) === 1n : signedValue < 0n;
        result = BigInt.asUintN(size, signedValue >> BigInt(count));
      }
      regs.setFlag(FLAG_CF, carry);
      setSzp(regs, result, size);
      // count>1 的 OF 在 ISA 中未定义；这里复现黄金轨迹所记录的本机行为。
      const msb = (value & signBit(size)) !== 0n;
      const nextMsb = (value & (signBit(size) >> 1n)) !== 0n;
      if (ins.op === 'shl') regs.setFlag(FLAG_OF, msb !== nextMsb);
      else if (ins.op === 'shr') regs.setFlag(FLAG_OF, msb);
      else regs.setFlag(FLAG_OF, false);
      this.noteAlu(ins.op, value, BigInt(count), result, size);
      this.writeOperand(ins.ops[0], result);
      return;
    }
    if (ins.op === 'rol' || ins.op === 'ror') {
      const effectiveCount = count % size;
      const shift = BigInt(effectiveCount);
      let result = value;
      if (effectiveCount !== 0) {
        const remaining = BigInt(size - effectiveCount);
        result = ins.op === 'rol'
          ? ((value << shift) | (value >> remaining)) & operandMask
          : ((value >> shift) | (value << remaining)) & operandMask;
      }
      const carry = ins.op === 'rol' ? (result & 1n) === 1n : ((result >> BigInt(size - 1)) & 1n) === 1n;
      regs.setFlag(FLAG_CF, carry);
      if (count === 1) {
        const msb = (result & signBit(size)) !== 0n;
        const next = ((result >> BigInt(size - 2)) & 1n) === 1n;
        regs.setFlag(FLAG_OF, ins.op === 'rol' ? carry !== msb : msb !== next);
      }
      this.noteAlu(ins.op, value, BigInt(count), result, size);
      this.writeOperand(ins.ops[0], result);
      return;
    }
    // rcl/rcr：宽为 size+1 的含进位环，逐步旋转
    let full = value | (BigInt(regs.flag(FLAG_CF) ? 1 : 0) << bits);
    const ring = (1n << (bits + 1n)) - 1n;
    for (let k = 0, n = count % (size + 1); k < n; k++) {
      full = ins.op === 'rcl'
        ? ((full << 1n) & ring) | (full >> bits)
        : (full >> 1n) | ((full & 1n) << bits);
    }
    const result = full & operandMask;
    const carry = ((full >> bits) & 1n) === 1n;
    regs.setFlag(FLAG_CF, carry);
    if (count === 1) {
      const msb = (result & signBit(size)) !== 0n;
      const next = ((result >> BigInt(size - 2)) & 1n) === 1n;
      regs.setFlag(FLAG_OF, ins.op === 'rcl' ? carry !== msb : msb !== next);
    }
    this.noteAlu(ins.op, value, BigInt(count), result, size);
    this.writeOperand(ins.ops[0], result);
    return;
  }

  private divide(ins: Decoded, isSigned: boolean): void {
    const { regs } = this;
    const size = ins.size;
    // 字节除法使用 AX；其他位宽使用 DX:AX、EDX:EAX 或 RDX:RAX。
    const dividendBits = size === 8
      ? regs.read(RAX, 16)
      : (regs.read(RDX, size) << BigInt(size)) | regs.read(RAX, size);
    const divisorBits = this.readOperand(ins.ops[0]);
    const dividend = isSigned ? signed(dividendBits, size * 2) : dividendBits;
    const divisor = isSigned ? signed(divisorBits, size) : divisorBits;
    if (divisor === 0n) throw new Error('除数为零');

    const quotient = dividend / divisor;
    const minimum = isSigned ? -signBit(size) : 0n;
    const maximum = isSigned ? signBit(size) - 1n : mask(size);
    if (quotient < minimum || quotient > maximum) throw new Error('除法溢出');

    const remainder = dividend % divisor;
    if (size === 8) {
      regs.write(RAX, 8, quotient);
      regs.write(RAX, 8, remainder, true);
      this.noteReg(RAX, 8, quotient);
      this.noteReg(RAX, 8, remainder, true);
    } else {
      regs.write(RAX, size, quotient);
      regs.write(RDX, size, remainder);
      this.noteRead({ kind: 'reg', name: registerName(RDX, size), canonical: NAMES64[RDX], bits: size, value: hex(regs.read(RDX, size), size) });
      this.noteReg(RAX, size, quotient);
      this.noteReg(RDX, size, remainder);
    }
  }

  private stringStep(op: 'movs' | 'stos' | 'lods' | 'scas' | 'cmps', size: number): void {
    const { regs, mem } = this;
    const bytes = size / 8;
    const delta = (regs.flag(FLAG_DF) ? -1n : 1n) * BigInt(bytes);
    const si = regs.gpr[RSI], di = regs.gpr[RDI];
    switch (op) {
      case 'movs':
        this.writeMemory(di, bytes, this.readMemory(si, bytes));
        regs.gpr[RSI] = (si + delta) & M64;
        regs.gpr[RDI] = (di + delta) & M64;
        this.noteReg(RSI, 64, regs.gpr[RSI]);
        this.noteReg(RDI, 64, regs.gpr[RDI]);
        break;
      case 'stos': {
        const value = regs.read(RAX, size);
        this.writeMemory(di, bytes, value);
        regs.gpr[RDI] = (di + delta) & M64;
        this.noteReg(RDI, 64, regs.gpr[RDI]);
        break;
      }
      case 'lods': {
        const value = this.readMemory(si, bytes);
        regs.write(RAX, size, value);
        this.noteReg(RAX, size, value);
        regs.gpr[RSI] = (si + delta) & M64;
        this.noteReg(RSI, 64, regs.gpr[RSI]);
        break;
      }
      case 'scas': {
        const a = regs.read(RAX, size), b = this.readMemory(di, bytes);
        this.noteAlu('cmp', a, b, arithSub(regs, a, b, size), size);
        regs.gpr[RDI] = (di + delta) & M64;
        this.noteReg(RDI, 64, regs.gpr[RDI]);
        break;
      }
      case 'cmps': {
        const a = this.readMemory(si, bytes), b = this.readMemory(di, bytes);
        this.noteAlu('cmp', a, b, arithSub(regs, a, b, size), size);
        regs.gpr[RSI] = (si + delta) & M64;
        regs.gpr[RDI] = (di + delta) & M64;
        this.noteReg(RSI, 64, regs.gpr[RSI]);
        this.noteReg(RDI, 64, regs.gpr[RDI]);
        break;
      }
    }
  }
}
