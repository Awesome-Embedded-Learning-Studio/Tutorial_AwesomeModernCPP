<script setup lang="ts">
import { computed } from 'vue';
import { registerInfo, type Hex } from '../../payload/types';
import type { Access, AluEvent } from '../../engine/types';

const props = defineProps<{
  regs: Record<string, Hex>;
  changed: string[];
  written: string[];
  focus: string[];
  visible: string[];
  pinned: string[];
  alu?: AluEvent;
  flags: string[];
  showAlu: boolean;
  showFlags: boolean;
  detail: boolean;
  showAddress: boolean;
  showBytes: boolean;
  current?: { addr: Hex; text: string; bytes: string };
  next?: { addr: Hex; text: string; bytes: string };
}>();

const emit = defineEmits<{ (event: 'pin', canonical: string): void }>();

const ORDER = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15'];
const FLAG_NAMES = ['CF', 'ZF', 'SF', 'OF', 'PF'];

function short(value: string | undefined): string {
  if (!value) return '—';
  const trimmed = value.replace(/^0x0+/, '0x');
  const text = trimmed === '0x' ? '0x0' : trimmed;
  return text.length > 13 ? `0x…${text.slice(-8)}` : text;
}

function shortAddr(addr: string | undefined): string {
  if (!addr) return '';
  return addr.length <= 6 ? addr : `0x…${addr.slice(-4)}`;
}

function aliasFor(canonicalName: string): string | undefined {
  const match = props.focus.find(name => registerInfo(name)?.canonical === canonicalName);
  return match;
}

interface Cell {
  canonical: string;
  label: string;
  alias?: string;
  full: string;
  short: string;
  focus: boolean;
  changed: boolean;
  written: boolean;
  pinned: boolean;
  visible: boolean;
}

const cells = computed<Cell[]>(() => ORDER.map(canonicalName => {
  const alias = aliasFor(canonicalName);
  const name = alias ?? canonicalName;
  const info = registerInfo(name);
  const bits = info?.bits ?? 64;
  const raw = props.regs[canonicalName] ?? '0x0';
  const masked = bits === 64 ? raw : `0x${(BigInt(raw) & ((1n << BigInt(bits)) - 1n)).toString(16).padStart(bits / 4, '0')}`;
  return {
    canonical: canonicalName,
    label: name.toUpperCase(),
    alias,
    full: masked,
    short: short(masked),
    focus: props.focus.includes(name),
    changed: props.changed.includes(canonicalName),
    written: props.written.includes(canonicalName),
    pinned: props.pinned.includes(canonicalName),
    visible: props.visible.includes(canonicalName),
  };
}));

const aluText = computed(() => {
  if (!props.alu) return undefined;
  const sign: Record<string, string> = { add: '+', sub: '−', cmp: '−', and: '&', or: '|', xor: '^', test: '&', '*': '×', shl: '<<', shr: '>>', sar: '>>' };
  const toNumber = (value: string) => {
    const n = BigInt.asUintN(props.alu!.bits, BigInt(value));
    return n <= 0xffffn ? n.toString(10) : `0x${n.toString(16)}`;
  };
  return {
    left: toNumber(props.alu.lhs),
    sign: sign[props.alu.op] ?? props.alu.op,
    right: toNumber(props.alu.rhs),
    result: toNumber(props.alu.result),
  };
});
</script>

<template>
  <figure class="cpu" :class="{ 'is-detail': detail }" data-anchor="die">
    <svg class="die" viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">
      <path class="die-fill" d="M9 4 H91 L96 9 V91 L91 96 H9 L4 91 V9 Z" />
      <path class="die-line" d="M9 4 H91 L96 9 V91 L91 96 H9 L4 91 V9 Z" />
      <g class="pins">
        <path v-for="n in 9" :key="'t' + n" :d="`M${8 + n * 9} 1 V3`" />
        <path v-for="n in 9" :key="'b' + n" :d="`M${8 + n * 9} 97 V99`" />
        <path v-for="n in 8" :key="'l' + n" :d="`M1 ${10 + n * 9} H3`" />
      </g>
      <g class="traces">
        <path d="M50 18 V23" />
        <path d="M61 44 H64" />
        <path d="M74 56 L74 61 L68 63 H61" />
        <path d="M74 61 V66" />
        <path d="M61 78 H86 L90 82" />
        <path d="M3 10 H0" />
      </g>
    </svg>

    <div class="queue" data-anchor="rip">
      <div
        v-if="props.current"
        class="qrow is-current"
        :data-anchor="`code:${props.current.addr}`"
        :title="`${props.current.addr}  ${props.current.text}`"
      >
        <span class="qtag">刚执行</span>
        <span v-if="props.showAddress" class="qaddr mono">{{ shortAddr(props.current.addr) }}</span>
        <span class="qtext mono">{{ props.current.text }}</span>
        <span v-if="props.showBytes" class="qbytes mono">{{ props.current.bytes }}</span>
      </div>
      <div
        v-if="props.next"
        class="qrow is-next"
        :data-anchor="`code:${props.next.addr}`"
        :title="`${props.next.addr}  ${props.next.text}`"
      >
        <span class="qtag">下一条</span>
        <span v-if="props.showAddress" class="qaddr mono">{{ shortAddr(props.next.addr) }}</span>
        <span class="qtext mono">{{ props.next.text }}</span>
        <span v-if="props.showBytes" class="qbytes mono">{{ props.next.bytes }}</span>
      </div>
      <p v-if="!props.current && !props.next" class="qempty">按单步，这里显示正在执行的指令</p>
    </div>

    <div class="regs" role="list" aria-label="寄存器">
      <button
        v-for="cell in cells"
        :key="cell.canonical"
        class="cell"
        role="listitem"
        type="button"
        :data-register="cell.canonical"
        :data-anchor="'reg:' + (cell.alias ?? cell.canonical)"
        :data-alias="cell.alias"
        :class="{
          'is-focus': cell.focus,
          'is-changed': cell.changed,
          'is-written': cell.written,
          'is-pinned': cell.pinned,
          'is-dim': !cell.visible,
        }"
        :aria-pressed="cell.pinned"
        :title="`${cell.label} = ${cell.full}${cell.pinned ? '（已固定）' : '（点击固定关注）'}`"
        @click="emit('pin', cell.canonical)"
      >
        <span class="name">{{ cell.label }}</span>
        <span class="value mono">{{ cell.short }}</span>
        <span class="sr-only">{{ cell.full }}</span>
        <span v-if="cell.changed" class="sr-only">已改变</span>
        <i v-if="cell.changed" class="dot" aria-hidden="true" />
      </button>
    </div>

    <div v-if="showAlu" class="alu" data-anchor="alu">
      <template v-if="aluText">
        <span class="label">ALU</span>
        <span class="sum mono">{{ aluText.left }} {{ aluText.sign }} {{ aluText.right }}</span>
        <span class="eq mono">= {{ aluText.result }}</span>
      </template>
      <template v-else>
        <span class="label">ALU</span>
        <span class="idle">空闲</span>
      </template>
    </div>

    <div v-if="showFlags" class="flags" aria-label="条件标志">
      <span
        v-for="flag in FLAG_NAMES"
        :key="flag"
        class="flag mono"
        :class="{ 'is-hit': flags.includes(flag) }"
      >{{ flag }}</span>
    </div>

    <span class="port" aria-hidden="true">内存</span>
  </figure>
</template>

<style scoped>
.cpu {
  position: relative;
  margin: 0;
  width: 100%;
  container-type: inline-size;
  aspect-ratio: 1.12;
  max-height: 560px;
  min-height: 280px;
}

.die { position: absolute; inset: 0; width: 100%; height: 100%; overflow: visible; }

.die-fill { fill: var(--paper-2); }
.die-line { fill: none; stroke: var(--line-strong); stroke-width: 1.25; vector-effect: non-scaling-stroke; }
.pins path { stroke: var(--line-strong); stroke-width: 1; vector-effect: non-scaling-stroke; }
.traces path {
  fill: none;
  stroke: var(--line-strong);
  stroke-width: 1;
  stroke-dasharray: 3 4;
  opacity: 0.55;
  vector-effect: non-scaling-stroke;
}

.queue {
  position: absolute;
  left: 12%;
  top: 5%;
  width: 76%;
  display: flex;
  flex-direction: column;
  gap: 1px;
  padding-bottom: 5px;
  border-bottom: 1px solid var(--line);
}
.qrow {
  position: relative;
  display: flex;
  align-items: baseline;
  gap: 8px;
  min-width: 0;
  padding: 3px 6px 3px 11px;
  border: 0;
  border-radius: 0;
  background: transparent;
}
.qrow::before {
  content: '';
  position: absolute;
  left: 0;
  top: 2px;
  bottom: 2px;
  width: 2px;
  background: color-mix(in srgb, var(--line-strong) 45%, transparent);
}
.qrow.is-current::before { background: var(--signal); }
.qrow.is-next::before {
  background: repeating-linear-gradient(to bottom, var(--line-strong) 0 3px, transparent 3px 6px);
  width: 1px;
}
.qtag {
  flex: 0 0 auto;
  font: 600 9px/1.4 var(--sans);
  letter-spacing: 0.12em;
  color: var(--muted);
}
.is-current .qtag { color: var(--signal); }
.qaddr { flex: 0 0 auto; font-size: 10px; color: var(--muted); }
.qtext {
  min-width: 0;
  font-size: 11px;
  color: var(--muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.is-current .qtext { color: var(--ink); }
.is-next .qtext { color: color-mix(in srgb, var(--muted) 88%, transparent); }
.qbytes { flex: 0 0 auto; margin-left: auto; font-size: 10px; color: var(--faint); }
.qempty { margin: 0; padding: 4px 8px; font-size: 10px; color: var(--muted); }

.regs {
  position: absolute;
  left: 6.5%;
  top: 23%;
  width: 55%;
  height: 58%;
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  grid-auto-rows: 1fr;
  gap: 1px 8px;
}

.cell {
  position: relative;
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 4px;
  min-width: 0;
  padding: 3px 6px;
  font: 400 10px/1 var(--sans);
  color: var(--ink-2);
  background: transparent;
  border: 0;
  border-radius: 3px;
  cursor: pointer;
  transition: background 120ms ease, color 120ms ease, opacity 120ms ease;
}
.cell:hover { background: var(--paper-3); }
.cell .name { color: var(--muted); font-weight: 600; letter-spacing: 0.04em; }
.cell .value { color: var(--ink); font-size: 10px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.cell.is-dim { opacity: 0.28; }
.cell.is-focus { background: color-mix(in srgb, var(--signal) 9%, transparent); }
.cell.is-focus .name { color: var(--signal); }
.cell.is-pinned { box-shadow: inset 0 0 0 1px var(--line-strong); }
.cell.is-changed .value { color: var(--write); }
.cell .dot {
  position: absolute;
  right: 3px;
  top: 50%;
  translate: 0 -50%;
  width: 4px;
  height: 4px;
  border-radius: 50%;
  background: var(--write);
}

.alu {
  position: absolute;
  left: 65%;
  top: 31%;
  width: 23%;
  height: 24%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 3px;
  padding: 6px 8px;
  color: var(--ink);
  background: color-mix(in srgb, var(--paper) 55%, transparent);
  border: 1px solid var(--line);
  border-radius: 8px;
}
.alu .label { color: var(--muted); font-size: 9px; }
.alu .sum { font-size: 11px; }
.alu .eq { font-size: 11px; color: var(--write); font-weight: 600; }
.alu .idle { font-size: 10px; color: var(--faint); }

.flags {
  position: absolute;
  left: 65%;
  top: 62%;
  width: 20%;
  display: flex;
  justify-content: space-between;
  gap: 2px;
}
.flag {
  flex: 1;
  text-align: center;
  font-size: 9px;
  padding: 3px 0;
  color: var(--faint);
  border: 1px solid var(--line);
  border-radius: 2px;
}
.flag.is-hit { color: var(--ink); border-color: var(--ink-2); background: var(--paper-2); }

.port {
  position: absolute;
  right: 1.5%;
  top: 78%;
  font: 600 9px/1 var(--sans);
  letter-spacing: 0.1em;
  color: var(--faint);
  font-size: 8px;
  writing-mode: vertical-rl;
}

@container (max-width: 620px) {
  .cpu { aspect-ratio: 1.12; min-height: 280px; }
}

/* 芯片自己够宽就按桌面排，窄了就把留白还给寄存器，以芯片宽度为准而不是页面宽度。 */
@container (max-width: 430px) {
  .queue { left: 11%; width: 78%; top: 5%; }
  .qrow { padding: 2px 4px 2px 6px; gap: 6px; }
  .qbytes { display: none; }
  .regs {
    left: 5.5%;
    width: 89%;
    top: 24%;
    height: 50%;
    gap: 2px 10px;
  }
  .cell {
    padding: 2px 4px;
    border-radius: 2px;
    background: color-mix(in srgb, var(--paper) 55%, transparent);
  }
  .cell.is-dim { opacity: 0.5; }
  .cell .value { font-size: 10px; }
  .flags {
    left: 5.5%;
    width: 89%;
    top: auto;
    bottom: 19.5%;
  }
  .alu {
    display: flex;
    left: 5.5%;
    width: 89%;
    top: auto;
    bottom: 4.5%;
    height: 12.5%;
    flex-direction: row;
    gap: 8px;
  }
  .port { display: none; }
}
</style>
