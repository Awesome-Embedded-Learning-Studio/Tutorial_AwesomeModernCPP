<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue';
import type { Hex } from '../../payload/types';
import type { CodeLine } from './model';

const props = defineProps<{
  lines: CodeLine[];
  current?: CodeLine;   // 刚执行的指令
  next?: CodeLine;      // 下一条（RIP 指向）
  target?: Hex;         // 本步跳转/返回的目标地址，可能不在列表里
  showAddress: boolean; // 显示地址列
  showBytes: boolean;   // 显示机器码列
  dense: boolean;       // 窄容器：只铺开当前指令前后的几行
  bare?: boolean;       // 藏在子面板里：不画自己的头
}>();

interface RailLine {
  kind: 'line';
  line: CodeLine;
}

interface RailGap {
  kind: 'gap';
  key: string;
}

type RailItem = RailLine | RailGap;

const rowsEl = ref<HTMLElement>();

/** 完整列表，或 dense 模式下当前/下一条 ±1 行，中间留一个省略占位。 */
const visible = computed<RailItem[]>(() => {
  const lines = props.lines;
  if (!props.dense) return lines.map((line): RailItem => ({ kind: 'line', line }));
  if (!lines.length) return [];

  const indexOf = (line: CodeLine | undefined): number => {
    if (!line) return 0;
    const found = lines.findIndex(candidate => candidate.addr === line.addr);
    return found >= 0 ? found : 0;
  };

  const picked = new Set<number>();
  for (const anchor of [indexOf(props.current), indexOf(props.next)]) {
    for (let offset = -1; offset <= 1; offset += 1) {
      const index = anchor + offset;
      if (index >= 0 && index < lines.length) picked.add(index);
    }
  }

  const items: RailItem[] = [];
  let previous: number | undefined;
  for (const index of [...picked].sort((a, b) => a - b)) {
    if (previous !== undefined && index > previous + 1) {
      items.push({ kind: 'gap', key: `gap:${previous}:${index}` });
    }
    items.push({ kind: 'line', line: lines[index] });
    previous = index;
  }
  return items;
});

const focusLine = computed(() => props.current ?? props.next ?? props.lines[0]);
const fullAddress = computed(() => focusLine.value?.addr ?? '');
const shortAddress = computed(() => shortAddr(fullAddress.value));

function shortAddr(addr: string): string {
  return addr.length <= 6 ? addr : `0x…${addr.slice(-4)}`;
}

function isCurrent(line: CodeLine): boolean {
  return props.current !== undefined && line.addr === props.current.addr;
}

function isNext(line: CodeLine): boolean {
  return props.next !== undefined && line.addr === props.next.addr;
}

function rowClass(line: CodeLine): Record<string, boolean> {
  return {
    'is-current': isCurrent(line),
    'is-next': isNext(line),
    'is-target': props.target !== undefined && line.addr === props.target && !isNext(line),
  };
}

/** 当前行跟随 RIP 时滚到列表中间；SSR 下直接跳过。 */
async function revealActive(): Promise<void> {
  await nextTick();
  if (typeof window === 'undefined') return;
  const container = rowsEl.value;
  const addr = props.current?.addr ?? props.next?.addr;
  if (!container || !addr) return;
  const target = `code:${addr}`;
  const row = [...container.querySelectorAll<HTMLElement>('.row[data-anchor]')]
    .find(item => item.dataset.anchor === target);
  row?.scrollIntoView({ block: 'center' });
}

watch(
  () => [props.current?.addr, props.next?.addr],
  () => { void revealActive(); },
);
</script>

<template>
  <section class="rail" aria-label="代码轨道">
    <div v-if="!props.bare" class="rail-head">
      <span class="label">代码</span>
      <span
        v-if="fullAddress"
        class="rail-addr"
        tabindex="0"
        :title="fullAddress"
        :aria-label="`完整地址 ${fullAddress}`"
      >{{ shortAddress }}</span>
    </div>

    <p v-if="!current" class="rail-empty">按单步，这里会跟着 RIP 走</p>

    <ol ref="rowsEl" class="rows" role="list" tabindex="0" aria-label="代码行">
      <li
        v-for="item in visible"
        :key="item.kind === 'line' ? item.line.addr : item.key"
        class="row"
        :class="item.kind === 'line' ? rowClass(item.line) : 'row-gap'"
        :data-anchor="item.kind === 'line' ? 'code:' + item.line.addr : undefined"
        :title="item.kind === 'line' && item.line.addr === target ? '跳到这里' : undefined"
        :aria-current="item.kind === 'line' && isCurrent(item.line) ? 'step' : undefined"
        :tabindex="item.kind === 'line' && item.line.addr === target ? 0 : undefined"
      >
        <template v-if="item.kind === 'line'">
          <span
            class="mark"
            :class="{ 'mark-current': isCurrent(item.line), 'mark-next': isNext(item.line) && !isCurrent(item.line) }"
            aria-hidden="true"
          ></span>
          <span v-if="showAddress" class="addr mono" :title="item.line.addr">{{ shortAddr(item.line.addr) }}</span>
          <span class="text mono">{{ item.line.text }}</span>
          <span v-if="showBytes" class="bytes mono">{{ item.line.bytes }}</span>
          <span v-if="isCurrent(item.line) || isNext(item.line)" class="tags">
            <span v-if="isCurrent(item.line)" class="tag tag-current">刚执行</span>
            <span v-if="isNext(item.line)" class="tag tag-next">下一条</span>
          </span>
          <span v-if="item.line.addr === target" class="sr-only">跳到这里</span>
        </template>
        <template v-else>
          <span class="gap-dots" aria-hidden="true">⋯</span>
          <span class="sr-only">省略了中间的代码行</span>
        </template>
      </li>
    </ol>
  </section>
</template>

<style scoped>
.rail,
.rail *,
.rail *::before,
.rail *::after {
  box-sizing: border-box;
}

.rail {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
  color: var(--ink);
  font-family: var(--sans);
}

.mono {
  font-family: var(--mono);
  font-variant-numeric: tabular-nums;
}

.rail-head {
  flex: 0 0 auto;
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
  padding: 8px 10px;
  border-bottom: 1px solid var(--line);
}

.label {
  font: 600 10px/1 var(--sans);
  letter-spacing: 0.12em;
  text-transform: uppercase;
  color: var(--muted);
}

.rail-addr {
  min-width: 0;
  max-width: 45%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-family: var(--mono);
  font-size: 11px;
  font-variant-numeric: tabular-nums;
  color: var(--ink-2);
}

.rail-empty {
  flex: 0 0 auto;
  margin: 0;
  padding: 8px 10px 4px;
  color: var(--muted);
  font: 11px/1.6 var(--sans);
}

.rows {
  flex: 1 1 auto;
  min-height: 0;
  max-height: 100%;
  margin: 0;
  padding: 4px 0 8px;
  list-style: none;
  overflow: auto;
  overscroll-behavior: contain;
}

.row {
  position: relative;
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  padding: 4px 9px 4px 8px;
  border-radius: 6px;
  color: var(--muted);
  font-size: 12px;
  line-height: 1.5;
}

.row:hover { background: color-mix(in srgb, var(--paper-2) 60%, transparent); }
.row.is-current:hover { background: var(--signal-soft); }

.row.is-current {
  background: var(--signal-soft);
  box-shadow: inset 2px 0 var(--signal);
  border-radius: 6px;
  color: var(--ink);
}

.row.is-next:not(.is-current) {
  background-image: repeating-linear-gradient(
    to bottom,
    var(--line-strong) 0 3px,
    transparent 3px 6px
  );
  background-size: 1px 100%;
  background-position: left top;
  background-repeat: no-repeat;
  color: var(--ink-2);
}

.row.is-target {
  outline: 1px dashed color-mix(in srgb, var(--line-strong) 55%, transparent);
  outline-offset: -2px;
  border-radius: 2px;
}

.mark {
  flex: 0 0 12px;
  width: 12px;
  align-self: stretch;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.mark-current::before {
  content: "";
  width: 0;
  height: 0;
  border-left: 5px solid var(--signal);
  border-top: 4px solid transparent;
  border-bottom: 4px solid transparent;
}

.mark-next::before {
  content: "";
  width: 7px;
  height: 7px;
  border: 1px dashed var(--line-strong);
}

.addr,
.bytes {
  flex: 0 1 auto;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 10px;
  font-variant-numeric: tabular-nums;
}

.addr {
  max-width: 7.5ch;
  color: var(--muted);
}

.bytes {
  max-width: 13ch;
  color: var(--faint);
}

.text {
  flex: 1 1 auto;
  min-width: 0;
  color: inherit;
  font-family: var(--mono);
  font-size: 12px;
  line-height: 1.55;
  white-space: pre-wrap;
  overflow-wrap: anywhere;
}

.tags {
  flex: 0 0 auto;
  margin-left: auto;
  display: inline-flex;
  align-items: center;
  gap: 5px;
  white-space: nowrap;
}

.tag {
  font: 500 10px/1 var(--sans);
  letter-spacing: 0.02em;
}

.tag-current {
  color: var(--signal);
  font-weight: 600;
}

.tag-next {
  color: var(--muted);
}

.row-gap {
  justify-content: center;
  padding-top: 1px;
  padding-bottom: 1px;
  color: var(--faint);
  font-family: var(--mono);
  font-size: 12px;
  line-height: 1.2;
  user-select: none;
}

.gap-dots {
  letter-spacing: 0.15em;
}

.row:focus-visible,
.rows:focus-visible,
.rail-addr:focus-visible {
  outline: 2px solid var(--signal);
  outline-offset: -2px;
}

.sr-only {
  position: absolute;
  width: 1px;
  height: 1px;
  margin: -1px;
  padding: 0;
  overflow: hidden;
  clip-path: inset(50%);
  white-space: nowrap;
  border: 0;
}
</style>
