<script setup lang="ts">
import { computed, ref } from 'vue';
import type { Hex } from '../../payload/types';
import type { MemAccess, MemoryRow } from '../../engine/types';
import { buildStackView } from './stack';
import StackWord from './StackWord.vue';

const props = defineProps<{
  rows: MemoryRow[];
  pointers: { rsp?: Hex; rbp?: Hex };
  reads: MemAccess[];
  writes: MemAccess[];
  showBits: boolean;
}>();

/** 折叠组的展开状态，按首行地址记忆，换步后自然失效。 */
const open = ref(new Set<string>());

function toggleGap(key: string) {
  const next = new Set(open.value);
  if (next.has(key)) next.delete(key);
  else next.add(key);
  open.value = next;
}

const view = computed(() => buildStackView(props.rows, props.pointers, props.reads, props.writes, open.value));
const aboveHints = computed(() => view.value.hints.filter(hint => hint.side === 'above'));
const belowHints = computed(() => view.value.hints.filter(hint => hint.side === 'below'));

function shortOf(addr: Hex): string {
  return `…${addr.slice(2).toLowerCase().padStart(6, '0').slice(-6)}`;
}

function gapRange(from: Hex, to: Hex): string {
  return `${shortOf(from)} ~ ${shortOf(to)}`;
}
</script>

<template>
  <section class="asm-stack" aria-label="栈与内存">
    <div class="panel-head">
      <h3 class="panel-title">栈</h3>
      <span class="panel-grow">向低地址增长 ↓</span>
    </div>

    <p v-if="view.empty" class="panel-empty">本步没有观察内存</p>

    <template v-else>
      <p v-for="hint in aboveHints" :key="`above-${hint.name}`" class="pointer-hint">
        <span class="pointer-name">{{ hint.name }}</span>
        <span class="pointer-text">在窗口上方</span>
        <code class="pointer-addr">{{ hint.addr }}</code>
      </p>

      <div class="rows-viewport">
        <ol class="rows">
          <template v-for="item in view.items" :key="item.kind === 'word' ? item.addr : item.key">
            <li v-if="item.kind === 'gap'" class="gap-wrap" :class="{ 'in-frame': item.inFrame }">
              <button
                type="button"
                class="gap mono"
                :aria-expanded="open.has(item.key)"
                @click="toggleGap(item.key)"
              >
                <span class="gap-dots" aria-hidden="true">⋯</span>
                <span class="gap-count">空白 ×{{ item.count }}</span>
                <span class="gap-range">{{ gapRange(item.from, item.to) }}</span>
                <span class="gap-caret" aria-hidden="true">{{ open.has(item.key) ? '收起' : '展开' }}</span>
              </button>
            </li>
            <StackWord v-else :row="item" :show-bits="showBits" />
          </template>
        </ol>
      </div>

      <p v-for="hint in belowHints" :key="`below-${hint.name}`" class="pointer-hint">
        <span class="pointer-name">{{ hint.name }}</span>
        <span class="pointer-text">在窗口下方</span>
        <code class="pointer-addr">{{ hint.addr }}</code>
      </p>
    </template>
  </section>
</template>

<style scoped>
.asm-stack,
.asm-stack *,
.asm-stack *::before,
.asm-stack *::after {
  box-sizing: border-box;
}

.asm-stack {
  container-type: inline-size;
  min-width: 0;
  padding: 9px 11px 7px;
  border: 1px solid color-mix(in srgb, var(--line) 90%, transparent);
  border-radius: 12px;
  background: color-mix(in srgb, var(--paper) 55%, var(--paper-2));
  color: var(--ink);
  font-family: var(--sans);
}

.mono { font-family: var(--mono); }

.panel-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
  min-width: 0;
  padding: 0 2px 7px;
  border-bottom: 1px solid color-mix(in srgb, var(--line) 80%, transparent);
}
.panel-title {
  margin: 0;
  font: 600 10px/1.3 var(--sans);
  letter-spacing: 0.16em;
  color: var(--muted);
}
.panel-grow {
  font: 400 9.5px/1.3 var(--sans);
  color: var(--faint);
  white-space: nowrap;
}

.panel-empty {
  margin: 0;
  padding: 6px 2px 4px;
  font: 400 11px/1.5 var(--sans);
  color: var(--muted);
}

.pointer-hint {
  display: flex;
  align-items: baseline;
  gap: 6px;
  margin: 0;
  padding: 5px 2px 0;
  font: 400 9.5px/1.5 var(--sans);
  color: var(--faint);
}
.pointer-name {
  font: 600 8.5px/1.4 var(--mono);
  letter-spacing: 0.08em;
  color: var(--signal);
}
.pointer-addr {
  margin-left: auto;
  font: 400 9.5px/1.4 var(--mono);
  color: var(--muted);
}

.rows-viewport { min-width: 0; }

.rows {
  margin: 0;
  padding: 3px 0 1px;
  list-style: none;
  display: flex;
  flex-direction: column;
  max-height: 268px;
  overflow-y: auto;
  overscroll-behavior: contain;
  scrollbar-width: thin;
}

.gap-wrap { position: relative; min-width: 0; }

.gap {
  display: flex;
  align-items: baseline;
  gap: 7px;
  width: 100%;
  padding: 4px 4px 4px 90px;
  border: 0;
  border-radius: 6px;
  background: transparent;
  color: var(--faint);
  font-size: 9.5px;
  text-align: left;
  cursor: pointer;
}
.gap:hover { background: color-mix(in srgb, var(--paper) 55%, transparent); color: var(--muted); }
.gap-dots { letter-spacing: 0.18em; }
.gap-count { flex: 0 0 auto; }
.gap-range {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: color-mix(in srgb, var(--faint) 85%, transparent);
}
.gap-caret { flex: 0 0 auto; font-size: 9px; }

@container (max-width: 345px) {
  .gap { padding-left: 82px; }
}
</style>
