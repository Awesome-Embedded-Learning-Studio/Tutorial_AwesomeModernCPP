import { requireValue } from './errors';
import type { PayloadV1 } from './types';

function requireShape(condition: unknown, path: string, expected: string): asserts condition {
  requireValue(condition, `负载格式不正确：${path} ${expected}`);
}

function objectAt(value: unknown, path: string): Record<string, unknown> {
  requireShape(typeof value === 'object' && value !== null && !Array.isArray(value), path, '必须为对象');
  // 已排除 null 和数组；字段值保持 unknown，由各字段的检查继续收窄。
  return value as Record<string, unknown>;
}

function arrayAt(value: unknown, path: string, minimum = 0): unknown[] {
  requireShape(Array.isArray(value), path, '必须为数组');
  requireShape(value.length >= minimum, path, `至少包含 ${minimum} 项`);
  return value;
}

function stringAt(value: unknown, path: string, minimum = 0): string {
  requireShape(typeof value === 'string', path, '必须为字符串');
  requireShape(value.length >= minimum, path, '不能为空');
  return value;
}

function hexAt(value: unknown, path: string): string {
  const text = stringAt(value, path);
  requireShape(/^0x[0-9a-fA-F]+$/.test(text), path, '必须为 0x 前缀的十六进制字符串');
  return text;
}

function integerAt(value: unknown, path: string, minimum: number, maximum: number): number {
  requireShape(typeof value === 'number' && Number.isInteger(value), path, '必须为整数');
  requireShape(value >= minimum && value <= maximum, path, `必须在 ${minimum}–${maximum} 范围内`);
  return value;
}

function requireUnique(values: string[], path: string): void {
  requireShape(new Set(values).size === values.length, path, '不能包含重复项');
}

// 只实现 payload v1 的固定结构，不在运行时解释通用 JSON Schema。
// 字段规则与 documents/payload.schema.json 对齐；未声明的扩展字段原样保留。
export function assertPayloadShape(input: unknown): asserts input is PayloadV1 {
  const payload = objectAt(input, '$');
  requireShape(payload.format === 'asm-executor/payload@1', 'format', '必须为 asm-executor/payload@1');

  const meta = objectAt(payload.meta, 'meta');
  requireShape(meta.isa === 'x86-64' || meta.isa === 'thumb-2', 'meta.isa', '必须为 x86-64 或 thumb-2');
  requireShape(meta.syntax === 'att' || meta.syntax === 'intel' || meta.syntax === 'unified', 'meta.syntax', '必须为 att、intel 或 unified');
  stringAt(meta.title, 'meta.title', 1);
  hexAt(meta.entry, 'meta.entry');

  const code = arrayAt(payload.code, 'code', 1);
  for (const [index, value] of code.entries()) {
    const path = `code[${index}]`;
    const instruction = objectAt(value, path);
    hexAt(instruction.addr, `${path}.addr`);
    const bytes = stringAt(instruction.bytes, `${path}.bytes`);
    requireShape(/^(?:[0-9a-fA-F]{2})+$/.test(bytes), `${path}.bytes`, '必须为非空、偶数长度的十六进制字符串');
    stringAt(instruction.text, `${path}.text`, 1);
  }

  const initial = objectAt(payload.initial, 'initial');
  const regs = objectAt(initial.regs, 'initial.regs');
  for (const [name, value] of Object.entries(regs)) {
    hexAt(value, `initial.regs[${JSON.stringify(name)}]`);
  }

  const memory = arrayAt(initial.memory, 'initial.memory', 1);
  for (const [index, value] of memory.entries()) {
    const path = `initial.memory[${index}]`;
    const region = objectAt(value, path);
    hexAt(region.addr, `${path}.addr`);
    const size = integerAt(region.size, `${path}.size`, 4096, 16777216);
    requireShape(size % 4096 === 0, `${path}.size`, '必须为 4096 的整数倍');
    const perms = stringAt(region.perms, `${path}.perms`);
    requireShape(/^(?:r|w|x|rw|rx|wx|rwx)$/.test(perms), `${path}.perms`, '必须按 rwx 顺序指定权限');
    stringAt(region.bytes, `${path}.bytes`);
  }

  const execution = objectAt(payload.execution, 'execution');
  const stops = arrayAt(execution.stop_before, 'execution.stop_before', 1)
    .map((value, index) => hexAt(value, `execution.stop_before[${index}]`));
  requireUnique(stops, 'execution.stop_before');
  if (execution.max_steps !== undefined) {
    integerAt(execution.max_steps, 'execution.max_steps', 1, 10000);
  }

  if (payload.ui === undefined) return;
  const ui = objectAt(payload.ui, 'ui');
  if (ui.watch_regs !== undefined) {
    const names = arrayAt(ui.watch_regs, 'ui.watch_regs');
    requireShape(names.length <= 32, 'ui.watch_regs', '最多包含 32 项');
    const strings = names.map((value, index) => stringAt(value, `ui.watch_regs[${index}]`));
    requireUnique(strings, 'ui.watch_regs');
  }
  if (ui.stack_window !== undefined) {
    const window = objectAt(ui.stack_window, 'ui.stack_window');
    stringAt(window.around, 'ui.stack_window.around');
    integerAt(window.rows, 'ui.stack_window.rows', 1, 64);
  }
}
