<script setup lang="ts">
import type { WordView } from './stack';

defineProps<{
  row: WordView;
  showBits: boolean;
}>();
</script>

<template>
  <li
    class="word"
    :class="{
      'in-frame': row.inFrame,
      'is-touched': row.touched,
      'is-unmapped': row.unmapped,
    }"
    tabindex="0"
    :data-anchor="'mem:' + row.addr"
    :aria-label="row.label"
  >
    <span class="word-rail" aria-hidden="true">
      <span v-for="cursor in row.cursors" :key="cursor" class="ptr" :class="'is-' + cursor.toLowerCase()">{{ cursor }}</span>
    </span>

    <span class="word-addr">
      <span class="addr mono" :title="row.addr">{{ row.short }}</span>
      <span v-if="row.offset" class="offset mono">{{ row.offset }}</span>
    </span>

    <span v-if="row.unmapped" class="bytes void" aria-hidden="true">
      <span class="void-line"></span>
    </span>
    <span v-else class="bytes">
      <span
        v-for="cell in row.cells"
        :key="cell.key"
        class="byte mono"
        :class="cell.state"
        :title="cell.title"
      >{{ cell.text }}</span>
    </span>

    <span
      v-if="!row.unmapped"
      class="word-value mono"
      :class="{ 'is-write': row.hasWrite, 'is-read': row.hasRead, 'is-zero': row.zero }"
    >{{ row.value }}</span>
    <span v-else class="word-value mono is-void">未映射</span>

    <code
      v-if="showBits && !row.unmapped && row.touched"
      class="bits mono"
      title="按地址从低到高，每字节 8 位"
      aria-label="位图，按地址从低到高，每字节 8 位"
    >{{ row.bits }}</code>
  </li>
</template>

<style scoped>
.word,
.word *,
.word *::before,
.word *::after {
  box-sizing: border-box;
}

.mono { font-family: var(--mono); }

.word {
  position: relative;
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  padding: 6px 2px 6px 0;
  border-radius: 7px;
  color: var(--muted);
}
.word:hover { background: color-mix(in srgb, var(--paper) 55%, transparent); }
.word:focus-visible {
  outline: 2px solid var(--signal);
  outline-offset: -2px;
}

.word-rail {
  position: relative;
  flex: 0 0 30px;
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}

.ptr {
  align-self: flex-start;
  display: inline-flex;
  align-items: baseline;
  gap: 2px;
  padding: 0;
  border-radius: 0;
  background: none;
  font: 600 8.5px/1 var(--mono);
  letter-spacing: 0.1em;
  white-space: nowrap;
  color: var(--muted);
}
.ptr::before { content: '▸'; font-size: 7px; color: color-mix(in srgb, var(--muted) 75%, transparent); }
.ptr.is-rsp { color: var(--signal); }
.ptr.is-rbp { color: var(--ink-2); }

.in-frame::before {
  content: '';
  position: absolute;
  left: 0;
  top: 2px;
  bottom: 2px;
  width: 1px;
  background: color-mix(in srgb, var(--line-strong) 45%, transparent);
}

.word-addr {
  flex: 0 0 50px;
  display: flex;
  flex-direction: column;
  gap: 1px;
  min-width: 0;
  line-height: 1.15;
}
.addr {
  font-size: 9px;
  letter-spacing: -0.01em;
  color: var(--muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.offset {
  font-size: 8.5px;
  font-weight: 600;
  color: var(--ink-2);
  white-space: nowrap;
}

.bytes {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  gap: 3px;
  min-width: 0;
}
.bytes.void { flex: 1; gap: 0; }

.byte {
  width: 14px;
  text-align: center;
  font-size: 11.5px;
  letter-spacing: -0.02em;
  color: color-mix(in srgb, var(--ink-2) 48%, var(--paper));
  padding: 2px 0;
  border-radius: 3px;
}
.byte:nth-child(4) { margin-right: 7px; }
.byte.read {
  color: var(--read);
  background: var(--read-soft);
}
.byte.write {
  color: var(--write);
  background: var(--write-soft);
  font-weight: 600;
}

.void-line {
  flex: 1;
  height: 1px;
  margin: 0 4px 0 0;
  background: repeating-linear-gradient(90deg, var(--line-strong) 0 5px, transparent 5px 10px);
}

.word-value {
  margin-left: auto;
  padding-left: 11px;
  border-left: 1px solid color-mix(in srgb, var(--line-strong) 40%, transparent);
  font-size: 11.5px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
  letter-spacing: -0.01em;
  color: var(--ink);
  white-space: nowrap;
}
.word-value.is-write { color: var(--write); }
.word-value.is-read { color: var(--read); }
.word-value.is-zero {
  color: color-mix(in srgb, var(--faint) 75%, var(--paper));
  font-weight: 400;
}
.word-value.is-void {
  font-weight: 400;
  font-size: 10px;
  color: var(--faint);
}

.bits {
  flex-basis: 100%;
  padding: 2px 0 3px 36px;
  font-size: 9.5px;
  color: var(--muted);
  white-space: pre-wrap;
  word-break: break-all;
}

@container (max-width: 372px) {
  .word-value { display: none; }
  .word-addr { flex-basis: 46px; }
  .byte { width: auto; flex: 1; min-width: 12px; }
  .bytes { flex: 1; }
  .byte:nth-child(4) { margin-right: 4px; }
}
</style>
