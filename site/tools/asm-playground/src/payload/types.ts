export type Hex = `0x${string}`;
export interface PayloadV1 {
  format: 'asm-executor/payload@1';
  meta: { isa: 'x86-64' | 'thumb-2'; syntax: 'att' | 'intel' | 'unified'; title: string; entry: Hex };
  code: { addr: Hex; bytes: string; text: string }[];
  initial: { regs: Record<string, Hex>; memory: { addr: Hex; size: number; perms: string; bytes: string }[] };
  execution: { stop_before: Hex[]; max_steps?: number };
  ui?: { watch_regs?: string[]; stack_window?: { around: string; rows: number } };
}
export const GPRS = ['rax', 'rbx', 'rcx', 'rdx', 'rsi', 'rdi', 'rbp', 'rsp', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15'] as const;
export const REGISTERS = [...GPRS, 'rip', 'rflags'] as const;
export const DEFAULT_WATCH = ['rip', 'rsp', 'rbp', 'rax', 'edi', 'esi', 'rflags'];
const aliases: Record<string, string> = { eax: 'rax', ebx: 'rbx', ecx: 'rcx', edx: 'rdx', esi: 'rsi', edi: 'rdi', ebp: 'rbp', esp: 'rsp', eip: 'rip', eflags: 'rflags' };
export function registerInfo(name: string): { canonical: string; bits: number } | undefined {
  if ((REGISTERS as readonly string[]).includes(name)) return { canonical: name, bits: 64 };
  if (Object.hasOwn(aliases, name)) return { canonical: aliases[name], bits: 32 };
  if (/^r(?:[89]|1[0-5])d$/.test(name)) return { canonical: name.slice(0, -1), bits: 32 };
}
export function toHex(value: bigint, bits = 64): Hex { return `0x${BigInt.asUintN(bits, value).toString(16).padStart(bits / 4, '0')}`; }
export function fromBase64(value: string): Uint8Array { return Uint8Array.from(atob(value), char => char.charCodeAt(0)); }
export function bytesFromHex(value: string): Uint8Array { return Uint8Array.from(value.match(/../g) ?? [], byte => parseInt(byte, 16)); }
