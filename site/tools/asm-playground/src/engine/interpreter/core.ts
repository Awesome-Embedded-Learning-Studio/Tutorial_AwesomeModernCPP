// 自研 x86-64 解释器的底座：错误、页式内存、寄存器堆、rflags 组合。
// 地址与寄存器值一律 BigInt；尺寸与计数允许 Number。语义以原生 GDB 轨迹为准。

export class EngineError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'EngineError';
  }
}

export const PROT_READ = 1;
export const PROT_WRITE = 2;
export const PROT_EXEC = 4;
export const PROT_ALL = PROT_READ | PROT_WRITE | PROT_EXEC;

interface Region {
  start: bigint;
  end: bigint;
  perms: number;
  bytes: Uint8Array;
}

export class Memory {
  private regions: Region[] = [];

  private covering(address: bigint, size: number): Region | undefined {
    const end = address + BigInt(size);
    return this.regions.find(region => address >= region.start && end <= region.end);
  }

  map(address: bigint, size: number, perms: number): void {
    if (address % 4096n !== 0n || size <= 0 || size % 4096 !== 0) throw new EngineError(`映射必须页对齐：0x${address.toString(16)}+${size}`);
    const end = address + BigInt(size);
    if (this.regions.some(region => address < region.end && end > region.start)) throw new EngineError(`映射重叠：0x${address.toString(16)}`);
    this.regions.push({ start: address, end, perms, bytes: new Uint8Array(size) });
    this.regions.sort((a, b) => (a.start < b.start ? -1 : 1));
  }

  protect(address: bigint, size: number, perms: number): void {
    const region = this.covering(address, size);
    if (!region) throw new EngineError(`未映射区域不能改权限：0x${address.toString(16)}`);
    region.perms = perms;
  }

  write(address: bigint, bytes: Uint8Array): void {
    const region = this.covering(address, bytes.length);
    if (!region) throw new EngineError(`写入未映射内存：0x${BigInt.asUintN(64, address).toString(16)}`);
    if (!(region.perms & PROT_WRITE)) throw new EngineError(`写入只读内存：0x${BigInt.asUintN(64, address).toString(16)}`);
    // 只有已验证位于映射内的字节偏移才转为数组索引，绝对地址始终保持 BigInt。
    const offset = Number(address - region.start);
    region.bytes.set(bytes, offset);
  }

  read(address: bigint, size: number): Uint8Array {
    const region = this.covering(address, size);
    if (!region || !(region.perms & (PROT_READ | PROT_EXEC))) {
      throw new EngineError(`读取未映射内存：0x${BigInt.asUintN(64, address).toString(16)}`);
    }
    const offset = Number(address - region.start);
    return region.bytes.subarray(offset, offset + size);
  }

  readU(address: bigint, size: number): bigint {
    let value = 0n;
    const bytes = this.read(address, size);
    for (let i = size - 1; i >= 0; i--) value = (value << 8n) | BigInt(bytes[i]);
    return value;
  }

  writeU(address: bigint, size: number, value: bigint): void {
    if (![1, 2, 4, 8].includes(size)) throw new EngineError(`无效整数写入尺寸：${size}`);
    const bytes = new Uint8Array(8);
    // DataView 直接编码 BigInt，寄存器值不经过浮点数转换。
    new DataView(bytes.buffer).setBigUint64(0, BigInt.asUintN(64, value), true);
    this.write(address, bytes.subarray(0, size));
  }
}

// x86 编码序：rax rcx rdx rbx rsp rbp rsi rdi r8..r15；rip 与 rflags 单独存放。
export const REG_COUNT = 16;
export const RIP_SLOT = 16;
export const RFLAGS_SLOT = 17;

export class Registers {
  readonly gpr: bigint[] = Array(REG_COUNT).fill(0n);
  rip = 0n;
  rflags = 0x202n;

  read(slot: number, size: number, high = false): bigint {
    if (size === 64) return this.gpr[slot];
    const value = this.gpr[slot];
    if (size === 32) return value & 0xffffffffn;
    if (size === 16) return value & 0xffffn;
    return high ? (value >> 8n) & 0xffn : value & 0xffn;
  }

  write(slot: number, size: number, value: bigint, high = false): void {
    if (size === 64 || size === 32) {
      // 32 位写清零高位；更窄的写入只替换指定部分。
      this.gpr[slot] = BigInt.asUintN(size, value);
      return;
    }
    const current = this.gpr[slot];
    if (size === 16) {
      this.gpr[slot] = (current & ~0xffffn) | (value & 0xffffn);
      return;
    }
    const mask = high ? 0xff00n : 0xffn;
    this.gpr[slot] = (current & ~mask) | ((value << (high ? 8n : 0n)) & mask);
  }

  flag(bit: number): boolean {
    return ((this.rflags >> BigInt(bit)) & 1n) === 1n;
  }

  setFlag(bit: number, value: boolean): void {
    const mask = 1n << BigInt(bit);
    this.rflags = value ? this.rflags | mask : this.rflags & ~mask;
  }
}

export const FLAG_CF = 0;
export const FLAG_PF = 2;
export const FLAG_AF = 4;
export const FLAG_ZF = 6;
export const FLAG_SF = 7;
export const FLAG_IF = 9;
export const FLAG_DF = 10;
export const FLAG_OF = 11;
// POPF 在用户态只允许改算术标志与 DF；IF 与保留位维持 0x202。
const bit = (n: number) => 1n << BigInt(n);
const USER_FLAGS_MASK = bit(FLAG_CF) | bit(FLAG_PF) | bit(FLAG_AF) | bit(FLAG_ZF) | bit(FLAG_SF) | bit(FLAG_DF) | bit(FLAG_OF);

export function loadRflags(regs: Registers, value: bigint): void {
  regs.rflags = (value & USER_FLAGS_MASK) | 0x202n;
}

export function mask(size: number): bigint {
  return (1n << BigInt(size)) - 1n;
}

export function signBit(size: number): bigint {
  return 1n << BigInt(size - 1);
}

export function signed(value: bigint, size: number): bigint {
  return BigInt.asIntN(size, value);
}

export function parityByte(value: bigint): boolean {
  let byte = value & 0xffn;
  let even = true;
  while (byte) {
    even = !even;
    byte &= byte - 1n;
  }
  return even;
}

export function setSzp(regs: Registers, result: bigint, size: number): void {
  const m = mask(size);
  const value = result & m;
  regs.setFlag(FLAG_ZF, value === 0n);
  regs.setFlag(FLAG_SF, (value & signBit(size)) !== 0n);
  regs.setFlag(FLAG_PF, parityByte(value));
}

export function arithAdd(regs: Registers, a: bigint, b: bigint, size: number, carryIn = 0n): bigint {
  const m = mask(size);
  // 符号扩展立即数也必须按操作数位宽比较，才能正确计算进位。
  a &= m;
  b &= m;
  const result = a + b + carryIn;
  const value = result & m;
  regs.setFlag(FLAG_CF, result > m);
  regs.setFlag(FLAG_AF, ((a ^ b ^ result) & 0x10n) !== 0n);
  regs.setFlag(FLAG_OF, (~(a ^ b) & (a ^ result) & signBit(size)) !== 0n);
  setSzp(regs, value, size);
  return value;
}

export function arithSub(regs: Registers, a: bigint, b: bigint, size: number, borrowIn = 0n): bigint {
  const m = mask(size);
  a &= m;
  b &= m;
  const result = a - b - borrowIn;
  const value = result & m;
  regs.setFlag(FLAG_CF, a < b + borrowIn);
  regs.setFlag(FLAG_AF, ((a ^ b ^ result) & 0x10n) !== 0n);
  regs.setFlag(FLAG_OF, ((a ^ b) & (a ^ result) & signBit(size)) != 0n);
  setSzp(regs, value, size);
  return value;
}

export function logicFlags(regs: Registers, result: bigint, size: number): bigint {
  const value = result & mask(size);
  regs.setFlag(FLAG_CF, false);
  regs.setFlag(FLAG_OF, false);
  // AF 在本机逻辑运算后保持原值，实测轨迹如此。
  setSzp(regs, value, size);
  return value;
}

export function incDecFlags(regs: Registers, before: bigint, size: number, up: boolean): bigint {
  const carry = regs.flag(FLAG_CF);
  const result = up ? arithAdd(regs, before, 1n, size) : arithSub(regs, before, 1n, size);
  regs.setFlag(FLAG_CF, carry); // inc/dec 不动 CF
  return result;
}

// 乘法：CF/OF 报告是否溢出；ZF/SF/PF 按低位结果（本机实测），AF 不动。
export function mulFlags(regs: Registers, low: bigint, size: number, overflow: boolean): void {
  regs.setFlag(FLAG_CF, overflow);
  regs.setFlag(FLAG_OF, overflow);
  setSzp(regs, low & mask(size), size);
}
