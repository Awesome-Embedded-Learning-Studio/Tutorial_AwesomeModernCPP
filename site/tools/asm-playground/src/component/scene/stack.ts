// 栈面板的数据层：把内存窗口整理成字槽视图。
// 职责只有一件：给 StackPanel 一份可直接渲染的条目列表，
// 包括读写高亮、RSP/RBP 挂点、相对 RBP 的偏移名、连续零行折叠。

import type { Hex } from '../../payload/types';
import type { MemAccess, MemoryRow } from '../../engine/types';
import { byteGroups, rowValue } from './model';

export type ByteState = 'none' | 'read' | 'write';
export type PointerName = 'RSP' | 'RBP';

export interface ByteCell {
  key: string;
  text: string;
  state: ByteState;
  title: string;
}

export interface WordView {
  kind: 'word';
  addr: Hex;
  short: string;           // …ffd0a8
  offset: string | null;   // rbp-8，帧附近才有
  cells: ByteCell[];
  unmapped: boolean;
  value: string | null;
  cursors: PointerName[];
  bits: string;
  label: string;
  touched: boolean;
  hasWrite: boolean;
  hasRead: boolean;
  inFrame: boolean;
  zero: boolean;
}

export interface GapView {
  kind: 'gap';
  key: string;
  count: number;
  from: Hex;
  to: Hex;
  inFrame: boolean;
}

export type StackItem = WordView | GapView;

export interface PointerHint {
  name: PointerName;
  addr: Hex;
  side: 'above' | 'below';
}

export interface StackView {
  items: StackItem[];
  hints: PointerHint[];
  empty: boolean;
}

/** 把真实访问展开成逐字节地址集合，用来给格子挂读写轮廓。 */
function covered(accesses: MemAccess[]): Set<string> {
  const bytes = new Set<string>();
  for (const access of accesses) {
    const start = BigInt(access.addr);
    for (let offset = 0; offset < access.size; offset += 1) {
      bytes.add((start + BigInt(offset)).toString());
    }
  }
  return bytes;
}

function shortAddr(addr: Hex): string {
  const hex = addr.slice(2).toLowerCase();
  return `…${hex.padStart(6, '0').slice(-6)}`;
}

export function buildStackView(
  rows: MemoryRow[],
  pointers: { rsp?: Hex; rbp?: Hex },
  reads: MemAccess[],
  writes: MemAccess[],
  expanded: ReadonlySet<string>,
): StackView {
  const read = covered(reads);
  const write = covered(writes);
  const rbp = pointers.rbp !== undefined ? BigInt(pointers.rbp) : null;
  const rsp = pointers.rsp !== undefined ? BigInt(pointers.rsp) : null;

  const descending = [...rows].sort((left, right) => {
    const a = BigInt(left.addr);
    const b = BigInt(right.addr);
    return a === b ? 0 : a > b ? -1 : 1;
  });

  const words: WordView[] = descending.map(row => {
    const start = BigInt(row.addr);
    const groups = byteGroups((row.bytes ?? '').toLowerCase());
    const cells: ByteCell[] = groups.map((text, index) => {
      const key = (start + BigInt(index)).toString();
      const state: ByteState = write.has(key) ? 'write' : read.has(key) ? 'read' : 'none';
      const touch = state === 'write' ? '，本步写入' : state === 'read' ? '，本步读取' : '';
      return { key, text, state, title: `${key} = ${text}${touch}` };
    });

    const cursors: PointerName[] = [];
    for (const [name, addr] of [['RSP', pointers.rsp], ['RBP', pointers.rbp]] as const) {
      if (!addr) continue;
      const at = BigInt(addr);
      if (at >= start && at < start + 8n) cursors.push(name);
    }

    const unmapped = row.bytes === null;
    const value = unmapped ? null : rowValue(groups.join(''));
    const bits = groups.map(group => parseInt(group, 16).toString(2).padStart(8, '0')).join(' ');

    let offset: string | null = null;
    if (rbp !== null) {
      const off = rbp - start;
      if (off >= 0n && off <= 128n) offset = off === 0n ? 'rbp' : `rbp-${off}`;
      else if (off < 0n && off >= -128n) offset = `rbp+${-off}`;
    }

    const inFrame = rsp !== null && rbp !== null && rsp <= rbp && start <= rbp && start + 8n > rsp;

    const hasWrite = cells.some(cell => cell.state === 'write');
    const hasRead = !hasWrite && cells.some(cell => cell.state === 'read');
    const touched = cells.some(cell => cell.state !== 'none');

    let label = `地址 ${row.addr}`;
    if (unmapped) {
      label += '，未映射';
    } else {
      label += `，字节 ${groups.join(' ')}，整行值 ${value}`;
      if (hasWrite) label += '，本步写入';
      else if (hasRead) label += '，本步读取';
    }
    if (offset) label += `，相对栈帧基址 ${offset}`;
    if (cursors.length) label += `，${cursors.join('、')} 指向这里`;

    const zero = value === `0x${'0'.repeat(16)}`;

    return {
      kind: 'word', addr: row.addr, short: shortAddr(row.addr), offset,
      cells, unmapped, value, cursors, bits, label, touched, hasWrite, hasRead, inFrame, zero,
    };
  });

  // 连续的全零、已映射、无指针、无触碰行折成一条占位，默认收起。
  const items: StackItem[] = [];
  let pending: WordView[] = [];
  const flush = () => {
    if (pending.length >= 2 && !expanded.has(`gap-${pending[0].addr}`)) {
      const everyInFrame = pending.every(word => word.inFrame);
      items.push({
        kind: 'gap', key: `gap-${pending[0].addr}`,
        count: pending.length, from: pending[pending.length - 1].addr, to: pending[0].addr,
        inFrame: everyInFrame,
      });
    } else {
      items.push(...pending);
    }
    pending = [];
  };
  for (const word of words) {
    const boring = !word.unmapped && !word.touched && word.cursors.length === 0
      && word.cells.every(cell => cell.text === '00');
    if (boring) pending.push(word);
    else { flush(); items.push(word); }
  }
  flush();

  // 出窗提示：指针落在窗口外时告诉读者它在哪一侧。
  const hints: PointerHint[] = [];
  if (words.length) {
    let low = BigInt(words[words.length - 1].addr);
    let high = BigInt(words[0].addr) + 8n;
    const shown = new Set(words.flatMap(word => word.cursors));
    for (const [name, addr] of [['RSP', pointers.rsp], ['RBP', pointers.rbp]] as const) {
      if (!addr || shown.has(name)) continue;
      const at = BigInt(addr);
      if (at >= high) hints.push({ name, addr, side: 'above' });
      else if (at < low) hints.push({ name, addr, side: 'below' });
    }
  }

  return { items, hints, empty: words.length === 0 };
}
