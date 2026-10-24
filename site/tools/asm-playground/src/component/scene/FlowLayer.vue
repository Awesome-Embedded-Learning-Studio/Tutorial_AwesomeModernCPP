<script setup lang="ts">
import { nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import type { MemAccess, RegAccess, StepEvent } from '../../engine/types';
import { byteGroups } from './model';

const props = defineProps<{
  host?: HTMLElement;
  event?: StepEvent;
  enabled: boolean;
  duration: number;
}>();

const emit = defineEmits<{ (event: 'settled'): void }>();

interface Motion { from: string; to: string; label: string; tone: 'read' | 'write' }

const chips = ref<{ key: string; label: string; tone: string; x: number; y: number }[]>([]);
const path = ref<string>('');
let run = 0;
let frame = 0;
let resize: ResizeObserver | undefined;

function anchors(): Map<string, DOMRect> {
  const map = new Map<string, DOMRect>();
  if (!props.host) return map;
  for (const node of props.host.querySelectorAll<HTMLElement>('[data-anchor]')) {
    const key = node.dataset.anchor;
    if (!key || map.has(key)) continue;
    const rect = node.getBoundingClientRect();
    if (rect.width === 0 && rect.height === 0) continue;
    map.set(key, rect);
  }
  return map;
}

function rel(rect: DOMRect): { x: number; y: number } {
  const host = props.host!.getBoundingClientRect();
  return { x: rect.left - host.left, y: rect.top - host.top };
}

function measure(key: string, map: Map<string, DOMRect>): { x: number; y: number } | undefined {
  const rect = map.get(key);
  if (!rect) return undefined;
  const point = rel(rect);
  return { x: point.x + rect.width / 2, y: point.y + rect.height / 2 };
}

/** 一条指令最多两条运动：先读进运算区，再写回目标。 */
function plan(event: StepEvent): Motion[] {
  const motions: Motion[] = [];
  const regWrite = event.writes.find((access): access is RegAccess => access.kind === 'reg' && access.canonical !== 'rsp');
  const memWrite = event.writes.find((access): access is MemAccess => access.kind === 'mem');
  const memRead = event.reads.find((access): access is MemAccess => access.kind === 'mem');
  const regRead = event.reads.find((access): access is RegAccess => access.kind === 'reg');
  const value = (bytes: string) => `0x${[...byteGroups(bytes)].reverse().join('')}`;

  if (memRead && !memWrite) {
    motions.push({
      from: `mem:${memRead.addr}`,
      // 返回地址取出来是交给 RIP 的，不进运算区。
      to: event.flow.kind === 'return' ? 'rip' : regWrite ? `reg:${regWrite.name}` : 'alu',
      label: value(memRead.bytes), tone: 'read',
    });
  }
  if (memWrite) {
    motions.push({
      from: regRead ? `reg:${regRead.name}` : `code:${event.pc}`,
      to: `mem:${memWrite.addr}`,
      label: value(memWrite.bytes), tone: 'write',
    });
  } else if (regWrite) {
    const source = event.alu ? 'alu' : regRead ? `reg:${regRead.name}` : `code:${event.pc}`;
    motions.push({
      from: source,
      to: `reg:${regWrite.name}`,
      label: regWrite.value.replace(/^0x0+/, '0x'), tone: 'write',
    });
  }
  return motions.slice(0, 2);
}

async function play() {
  const my = ++run;
  chips.value = [];
  path.value = '';
  if (!props.host) { emit('settled'); return; }
  await nextTick();
  const map = anchors();
  const event = props.event;
  if (!event || !props.enabled) { emit('settled'); return; }
  const motions = plan(event).map(motion => {
    const from = measure(motion.from, map);
    const to = measure(motion.to, map);
    return from && to ? { ...motion, from, to } : undefined;
  }).filter((motion): motion is Motion & { from: { x: number; y: number }; to: { x: number; y: number } } => !!motion);
  if (!motions.length) { emit('settled'); return; }

  chips.value = motions.map((motion, index) => ({
    key: `${event.index}-${index}`, label: motion.label, tone: motion.tone,
    x: motion.from.x, y: motion.from.y,
  }));
  path.value = motions.map(motion => {
    const bow = Math.abs(motion.to.x - motion.from.x) * 0.12 + 12;
    const cx = (motion.from.x + motion.to.x) / 2, cy = (motion.from.y + motion.to.y) / 2 - bow;
    return `M${motion.from.x} ${motion.from.y} Q${cx} ${cy} ${motion.to.x} ${motion.to.y}`;
  }).join(' ');
  await nextTick();

  const el = props.host.querySelectorAll<HTMLElement>('.flow-chip');
  el.forEach((node, index) => {
    const motion = motions[index];
    if (!motion) return;
    const bow = Math.abs(motion.to.x - motion.from.x) * 0.12 + 12;
    const keyframes = [
      { transform: `translate(${motion.from.x}px, ${motion.from.y}px)` },
      { transform: `translate(${(motion.from.x + motion.to.x) / 2}px, ${(motion.from.y + motion.to.y) / 2 - bow}px)` },
      { transform: `translate(${motion.to.x}px, ${motion.to.y}px)` },
    ];
    node.animate(keyframes, { duration: props.duration, easing: 'cubic-bezier(.45,.05,.25,1)', fill: 'forwards' });
  });

  window.setTimeout(() => {
    if (my !== run) return;
    chips.value = [];
    path.value = '';
    emit('settled');
  }, props.duration + 160);
}

watch(() => [props.event?.index, props.event?.pc], play, { flush: 'post' });

/** 布局变了，旧坐标的芯片立刻作废，免得留在画面外撑出滚动条。 */
function clearStale() {
  if (!chips.value.length) return;
  run++;
  chips.value = [];
  path.value = '';
}

onMounted(() => {
  frame = window.requestAnimationFrame(() => void play());
  if (typeof ResizeObserver !== 'undefined' && props.host) {
    resize = new ResizeObserver(() => window.requestAnimationFrame(clearStale));
    resize.observe(props.host);
  }
  window.addEventListener('resize', clearStale);
});

onBeforeUnmount(() => {
  run++;
  window.cancelAnimationFrame(frame);
  resize?.disconnect();
  window.removeEventListener('resize', clearStale);
});
</script>

<template>
  <svg class="flow" aria-hidden="true">
    <path v-if="path" class="trace-path" :d="path" />
  </svg>
  <template v-for="chip in chips" :key="chip.key">
    <span class="flow-chip" :class="'is-' + chip.tone" :style="{ transform: `translate(${chip.x}px, ${chip.y}px)` }">{{ chip.label }}</span>
  </template>
</template>

<style scoped>
.flow {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
  overflow: hidden;
}
.trace-path {
  fill: none;
  stroke: var(--signal);
  stroke-width: 1.25;
  stroke-dasharray: 4 5;
  opacity: 0.5;
}
.flow-chip {
  position: absolute;
  top: 0;
  left: 0;
  transform-origin: center;
  translate: -50% -50%;
  padding: 2px 6px;
  font: 600 10px/1.4 var(--mono);
  border-radius: 2px;
  pointer-events: none;
  white-space: nowrap;
  z-index: 4;
}
.flow-chip.is-read { color: var(--read); background: var(--paper-2); box-shadow: inset 0 0 0 1px var(--read); }
.flow-chip.is-write { color: #fff; background: var(--write); box-shadow: 0 1px 3px rgb(23 37 31 / 18%); }
</style>
