// x86-64 指令解码：前缀（含 REX）、主操作码、0F 扩展、ModRM/SIB、RIP 相对寻址。
// 只覆盖 -O0 工具链产物用到的整数子集；超出范围一律抛 EngineError。

import { EngineError, Memory } from './core';

export interface RegOp {
  type: 'reg';
  slot: number;
  size: number;
  high: boolean;
}

export interface ImmOp {
  type: 'imm';
  value: bigint;
}

export interface MemOp {
  type: 'mem';
  size: number;
  base: number | null;
  index: number | null;
  scale: 1 | 2 | 4 | 8;
  disp: bigint;
  riprel: boolean;
  addr32: boolean;
}
export type Operand = RegOp | ImmOp | MemOp;

const ALU = ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp'] as const;
const SHIFTS = ['rol', 'ror', 'rcl', 'rcr', 'shl', 'shr', 'shl', 'sar'] as const;
type Instruction = typeof ALU[number] | typeof SHIFTS[number]
  | 'test' | 'mov' | 'lea' | 'inc' | 'dec' | 'not' | 'neg' | 'imul' | 'imul1' | 'mul' | 'div' | 'idiv'
  | 'push' | 'pop' | 'pushf' | 'popf' | 'leave' | 'call' | 'callind' | 'ret' | 'jmp' | 'jmpind'
  | 'jcc' | 'cmov' | 'setcc' | 'xchg' | 'movzx' | 'movsx' | 'bswap' | 'sxt' | 'sxt2'
  | 'clc' | 'stc' | 'cmc' | 'cld' | 'std' | 'loop' | 'loope' | 'loopne' | 'jrcxz'
  | 'movs' | 'stos' | 'lods' | 'scas' | 'cmps' | 'int3' | 'nop';

export interface Decoded {
  op: Instruction;
  size: number;
  ops: Operand[];
  length: number;
  rep?: 'rep' | 'repne';
  cond?: number;            // jcc/cmovcc/setcc 条件码 0-15
  countFromCl?: boolean;    // 移位计数来自 CL
}

export function decode(mem: Memory, pc: bigint): Decoded {
  let offset = 0;
  let rex = 0, operand16 = false, address32 = false;
  let rep: 'rep' | 'repne' | undefined;
  while (true) {
    if (offset >= 15) throw new EngineError('指令超过 15 字节');
    const b = mem.read(pc + BigInt(offset), 1)[0];
    if (b === 0x66) {
      operand16 = true;
      offset++;
    }
    else if (b === 0x67) {
      address32 = true;
      offset++;
    }
    else if (b === 0xf2) {
      rep = 'repne';
      offset++;
    }
    else if (b === 0xf3) {
      rep = 'rep';
      offset++;
    }
    else if (b === 0xf0 || b === 0x26 || b === 0x2e || b === 0x36 || b === 0x3e || b === 0x64 || b === 0x65) offset++;
    else if (b >= 0x40 && b <= 0x4f) {
      rex = b;
      offset++;
    }
    else break;
  }
  const rexW = (rex & 8) !== 0, rexR = (rex & 4) !== 0, rexX = (rex & 2) !== 0, rexB = (rex & 1) !== 0;
  const defaultSize = operand16 ? 16 : 32;
  const size = rexW ? 64 : defaultSize;
  const stackSize = operand16 ? 16 : 64;

  const next = (): number => {
    if (offset >= 15) throw new EngineError('指令超过 15 字节');
    const byte = mem.read(pc + BigInt(offset), 1)[0];
    offset++;
    return byte;
  };
  const imm = (n: number): bigint => {
    if (offset + n > 15) throw new EngineError('指令超过 15 字节');
    const value = mem.readU(pc + BigInt(offset), n);
    offset += n;
    return value;
  };
  const immSigned = (n: number): bigint => BigInt.asIntN(n * 8, imm(n));
  // 大多数 64 位整数操作只编码 imm32，由 CPU 符号扩展；MOVABS 单独读取 imm64。
  const operandImmediate = (): bigint => size === 64 ? immSigned(4) : imm(size / 8);

  let mod = -1, regField = -1, rmField = -1;
  const modrm = (): void => {
    const byte = next();
    mod = byte >> 6;
    regField = (byte >> 3) & 7;
    rmField = byte & 7;
  };
  // 无 REX 时 8 位寄存器字段 4-7 是 ah/ch/dh/bh（槽 0-3 的高字节）。
  const reg = (operandBits: number): RegOp => {
    const high = operandBits === 8 && rex === 0 && regField >= 4;
    return { type: 'reg', slot: (rexR ? 8 : 0) | (high ? regField - 4 : regField), size: operandBits, high };
  };
  const rmReg = (operandBits: number): RegOp => {
    const high = operandBits === 8 && rex === 0 && rmField >= 4;
    return { type: 'reg', slot: (rexB ? 8 : 0) | (high ? rmField - 4 : rmField), size: operandBits, high };
  };

  const memOp = (operandBits: number): MemOp => {
    let base: number | null = null, index: number | null = null, scale: 1 | 2 | 4 | 8 = 1, disp = 0n;
    let riprel = false;
    if (rmField === 4) {
      const sib = next();
      scale = (1 << (sib >> 6)) as 1 | 2 | 4 | 8;
      const idx = (sib >> 3) & 7, bs = sib & 7;
      index = idx === 4 && !rexX ? null : (rexX ? 8 : 0) | idx;
      base = bs === 5 && mod === 0 ? null : (rexB ? 8 : 0) | bs;
    } else if (mod === 0 && rmField === 5) riprel = true;
    else base = (rexB ? 8 : 0) | rmField;
    if (mod === 1) disp = immSigned(1);
    else if (mod === 2) disp = immSigned(4);
    else if (mod === 0 && (riprel || base === null)) disp = immSigned(4);
    return { type: 'mem', size: operandBits, base, index, scale, disp, riprel, addr32: address32 };
  };
  const rm = (operandBits: number): Operand => (mod === 3 ? rmReg(operandBits) : memOp(operandBits));
  const finish = (op: Instruction, operandBits: number, ops: Operand[], extra: Partial<Decoded> = {}): Decoded =>
    ({ op, size: operandBits, ops, length: offset, ...extra });
  const bad = (): never => {
    const bytes = Array.from({ length: Math.min(offset + 2, 8) }, (_, k) => mem.readU(pc + BigInt(k), 1).toString(16).padStart(2, '0')).join(' ');
    throw new EngineError(`无法解码的指令：${bytes} @ 0x${pc.toString(16)}`);
  };

  let opcode = next();
  if (opcode === 0x0f) {
    opcode = next();
    if (opcode >= 0x40 && opcode <= 0x4f) {
      modrm();
      return finish('cmov', size, [reg(size), rm(size)], { cond: opcode & 15 });
    }
    if (opcode >= 0x80 && opcode <= 0x8f) return finish('jcc', 64, [{ type: 'imm', value: immSigned(4) }], { cond: opcode & 15 });
    if (opcode >= 0x90 && opcode <= 0x9f) {
      modrm();
      return finish('setcc', 8, [rm(8)], { cond: opcode & 15 });
    }
    if (opcode >= 0xc8 && opcode <= 0xcf) {
      return finish('bswap', size, [{ type: 'reg', slot: (rexB ? 8 : 0) | (opcode & 7), size, high: false }]);
    }
    switch (opcode) {
      case 0x05:
        throw new Error(`不支持的指令：syscall @ 0x${pc.toString(16)}`);
      case 0x0b:
        throw new Error(`非法指令（ud2）@ 0x${pc.toString(16)}`);
      case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        modrm();
        rm(size); // NOP 不读操作数，但必须消耗 SIB 与位移字节。
        return finish('nop', size, []);
      case 0xaf:
        modrm();
        return finish('imul', size, [reg(size), rm(size)]);
      case 0xb6:
        modrm();
        return finish('movzx', size, [reg(size), rm(8)]);
      case 0xb7:
        modrm();
        return finish('movzx', size, [reg(size), rm(16)]);
      case 0xbe:
        modrm();
        return finish('movsx', size, [reg(size), rm(8)]);
      case 0xbf:
        modrm();
        return finish('movsx', size, [reg(size), rm(16)]);
      default:
        return bad();
    }
  }
  if (opcode < 0x40 && (opcode & 7) < 6) {
    const alu = ALU[opcode >> 3];
    switch (opcode & 7) {
      case 0:
        modrm();
        return finish(alu, 8, [rm(8), reg(8)]);
      case 1:
        modrm();
        return finish(alu, size, [rm(size), reg(size)]);
      case 2:
        modrm();
        return finish(alu, 8, [reg(8), rm(8)]);
      case 3:
        modrm();
        return finish(alu, size, [reg(size), rm(size)]);
      case 4:
        return finish(alu, 8, [{ type: 'reg', slot: 0, size: 8, high: false }, { type: 'imm', value: imm(1) }]);
      default:
        return finish(alu, size, [{ type: 'reg', slot: 0, size, high: false }, { type: 'imm', value: operandImmediate() }]);
    }
  }
  switch (opcode) {
    case 0x63:
      modrm();
      return finish('movsx', size, [reg(size), rm(32)]);
    case 0x68:
      return finish('push', stackSize, [{ type: 'imm', value: immSigned(operand16 ? 2 : 4) }]);
    case 0x80:
      modrm();
      return finish(ALU[regField], 8, [rm(8), { type: 'imm', value: imm(1) }]);
    case 0x81:
      modrm();
      return finish(ALU[regField], size, [rm(size), { type: 'imm', value: operandImmediate() }]);
    case 0x83:
      modrm();
      return finish(ALU[regField], size, [rm(size), { type: 'imm', value: immSigned(1) }]);
    case 0x69:
      modrm();
      return finish('imul', size, [reg(size), rm(size), { type: 'imm', value: immSigned(size === 16 ? 2 : 4) }]);
    case 0x6a:
      return finish('push', stackSize, [{ type: 'imm', value: immSigned(1) }]);
    case 0x6b:
      modrm();
      return finish('imul', size, [reg(size), rm(size), { type: 'imm', value: immSigned(1) }]);
    case 0x8d:
      modrm();
      if (mod === 3) return bad();
      return finish('lea', size, [reg(size), memOp(size)]);
    case 0x8f:
      modrm();
      if (regField !== 0) return bad();
      return finish('pop', stackSize, [rm(stackSize)]);
    case 0x84:
      modrm();
      return finish('test', 8, [rm(8), reg(8)]);
    case 0x85:
      modrm();
      return finish('test', size, [rm(size), reg(size)]);
    case 0x86:
      modrm();
      return finish('xchg', 8, [rm(8), reg(8)]);
    case 0x87:
      modrm();
      return finish('xchg', size, [rm(size), reg(size)]);
    case 0x88:
      modrm();
      return finish('mov', 8, [rm(8), reg(8)]);
    case 0x89:
      modrm();
      return finish('mov', size, [rm(size), reg(size)]);
    case 0x8a:
      modrm();
      return finish('mov', 8, [reg(8), rm(8)]);
    case 0x8b:
      modrm();
      return finish('mov', size, [reg(size), rm(size)]);
    case 0x90:
      if (!rexB) return finish('nop', size, []);
      return finish('xchg', size, [{ type: 'reg', slot: 0, size, high: false }, { type: 'reg', slot: 8, size, high: false }]);
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
      return finish('xchg', size, [{ type: 'reg', slot: 0, size, high: false }, { type: 'reg', slot: (rexB ? 8 : 0) | (opcode & 7), size, high: false }]);
    case 0x98:
      return finish('sxt', size, []);
    case 0x99:
      return finish('sxt2', size, []);
    case 0x9c:
      return finish('pushf', stackSize, []);
    case 0x9d:
      return finish('popf', stackSize, []);
    case 0xa0: case 0xa1: case 0xa2: case 0xa3: {
      const addr: MemOp = { type: 'mem', size: 0, base: null, index: null, scale: 1, disp: imm(address32 ? 4 : 8), riprel: false, addr32: address32 };
      const operandBits = opcode & 1 ? size : 8;
      addr.size = operandBits;
      const acc: RegOp = { type: 'reg', slot: 0, size: operandBits, high: false };
      return finish('mov', operandBits, opcode < 0xa2 ? [acc, addr] : [addr, acc]);
    }
    case 0xa4:
      return finish('movs', 8, [], { rep });
    case 0xa5:
      return finish('movs', size, [], { rep });
    case 0xa6:
      return finish('cmps', 8, [], { rep });
    case 0xa7:
      return finish('cmps', size, [], { rep });
    case 0xa8:
      return finish('test', 8, [{ type: 'reg', slot: 0, size: 8, high: false }, { type: 'imm', value: imm(1) }]);
    case 0xa9:
      return finish('test', size, [{ type: 'reg', slot: 0, size, high: false }, { type: 'imm', value: operandImmediate() }]);
    case 0xaa:
      return finish('stos', 8, [], { rep });
    case 0xab:
      return finish('stos', size, [], { rep });
    case 0xac:
      return finish('lods', 8, [], { rep });
    case 0xad:
      return finish('lods', size, [], { rep });
    case 0xae:
      return finish('scas', 8, [], { rep });
    case 0xaf:
      return finish('scas', size, [], { rep });
    case 0xc0:
      modrm();
      return finish(SHIFTS[regField], 8, [rm(8), { type: 'imm', value: imm(1) }]);
    case 0xc1:
      modrm();
      return finish(SHIFTS[regField], size, [rm(size), { type: 'imm', value: imm(1) }]);
    case 0xc2:
      return finish('ret', size, [{ type: 'imm', value: imm(2) }]);
    case 0xc3:
      return finish('ret', size, []);
    case 0xc6:
      modrm();
      return finish('mov', 8, [rm(8), { type: 'imm', value: imm(1) }]);
    case 0xc7:
      modrm();
      return finish('mov', size, [rm(size), { type: 'imm', value: size === 64 ? immSigned(4) : imm(size / 8) }]);
    case 0xc9:
      return finish('leave', 64, []);
    case 0xcc:
      return finish('int3', 8, []);
    case 0xd0:
      modrm();
      return finish(SHIFTS[regField], 8, [rm(8), { type: 'imm', value: 1n }]);
    case 0xd1:
      modrm();
      return finish(SHIFTS[regField], size, [rm(size), { type: 'imm', value: 1n }]);
    case 0xd2:
      modrm();
      return finish(SHIFTS[regField], 8, [rm(8)], { countFromCl: true });
    case 0xd3:
      modrm();
      return finish(SHIFTS[regField], size, [rm(size)], { countFromCl: true });
    case 0xe0:
      return finish('loopne', 64, [{ type: 'imm', value: immSigned(1) }]);
    case 0xe1:
      return finish('loope', 64, [{ type: 'imm', value: immSigned(1) }]);
    case 0xe2:
      return finish('loop', 64, [{ type: 'imm', value: immSigned(1) }]);
    case 0xe3:
      return finish('jrcxz', 64, [{ type: 'imm', value: immSigned(1) }]);
    case 0xe8:
      return finish('call', 64, [{ type: 'imm', value: immSigned(4) }]);
    case 0xe9:
      return finish('jmp', 64, [{ type: 'imm', value: immSigned(4) }]);
    case 0xeb:
      return finish('jmp', 64, [{ type: 'imm', value: immSigned(1) }]);
    case 0xf5:
      return finish('cmc', 8, []);
    case 0xf6: case 0xf7: {
      modrm();
      const operandBits = opcode === 0xf6 ? 8 : size;
      const width = Math.min(operandBits / 8, 4);
      switch (regField) {
        case 0:
          return finish('test', operandBits, [rm(operandBits), { type: 'imm', value: operandBits === 64 ? immSigned(4) : imm(width) }]);
        case 1:
          return bad();
        case 2:
          return finish('not', operandBits, [rm(operandBits)]);
        case 3:
          return finish('neg', operandBits, [rm(operandBits)]);
        case 4:
          return finish('mul', operandBits, [rm(operandBits)]);
        case 5:
          return finish('imul1', operandBits, [rm(operandBits)]);
        case 6:
          return finish('div', operandBits, [rm(operandBits)]);
        default:
          return finish('idiv', operandBits, [rm(operandBits)]);
      }
    }
    case 0xf8:
      return finish('clc', 8, []);
    case 0xf9:
      return finish('stc', 8, []);
    case 0xfc:
      return finish('cld', 8, []);
    case 0xfd:
      return finish('std', 8, []);
    case 0xfe:
      modrm();
      if (regField > 1) return bad();
      return finish(regField === 0 ? 'inc' : 'dec', 8, [rm(8)]);
    case 0xff: {
      modrm();
      switch (regField) {
        case 0: case 1:
          return finish(regField === 0 ? 'inc' : 'dec', size, [rm(size)]);
        case 2:
          return finish('callind', 64, [rm(64)]);
        case 4:
          return finish('jmpind', 64, [rm(64)]);
        case 6:
          return finish('push', stackSize, [rm(stackSize)]);
        default:
          bad();
      }
      break;
    }
    default:
      if (opcode >= 0x70 && opcode <= 0x7f) return finish('jcc', 64, [{ type: 'imm', value: immSigned(1) }], { cond: opcode & 15 });
      if (opcode >= 0xb0 && opcode <= 0xb7) {
        const idx = opcode & 7, high = rex === 0 && idx >= 4;
        return finish('mov', 8, [{ type: 'reg', slot: (rexB ? 8 : 0) | (high ? idx - 4 : idx), size: 8, high }, { type: 'imm', value: imm(1) }]);
      }
      if (opcode >= 0xb8 && opcode <= 0xbf)
        return finish('mov', size, [{ type: 'reg', slot: (rexB ? 8 : 0) | (opcode & 7), size, high: false }, { type: 'imm', value: size === 64 ? imm(8) : imm(size / 8) }]);
      if (opcode >= 0x50 && opcode <= 0x57)
        return finish('push', stackSize, [{ type: 'reg', slot: (rexB ? 8 : 0) | (opcode & 7), size: stackSize, high: false }]);
      if (opcode >= 0x58 && opcode <= 0x5f)
        return finish('pop', stackSize, [{ type: 'reg', slot: (rexB ? 8 : 0) | (opcode & 7), size: stackSize, high: false }]);
      return bad();
  }
  return bad();
}
