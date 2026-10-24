<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import type { ExecutorStatus } from '../../engine/types';
import type { DisplayToggles, Preset, Speed } from './display';

const props = defineProps<{
  status: ExecutorStatus;
  steps: number;
  canStep: boolean;
  canRun: boolean;
  canPause: boolean;
  playing: boolean;
  speed: Speed;
  preset: Preset;
  display: DisplayToggles;
  pins: string[];
  registers: { name: string; canonical: string }[];
}>();

const emit = defineEmits<{
  (event: 'step'): void;
  (event: 'run'): void;
  (event: 'pause'): void;
  (event: 'reset'): void;
  (event: 'toggle-play'): void;
  (event: 'speed', value: Speed): void;
  (event: 'preset', value: Preset): void;
  (event: 'toggle', patch: Partial<DisplayToggles>): void;
  (event: 'pin', canonical: string): void;
  (event: 'settings', open: boolean): void;
}>();

const settingsOpen = ref(false);
watch(settingsOpen, open => emit('settings', open));

const speedOptions: { value: Speed; label: string }[] = [
  { value: 'slow', label: '慢' },
  { value: 'normal', label: '标准' },
  { value: 'fast', label: '快' },
];

const presetOptions: { value: Preset; label: string }[] = [
  { value: 'follow', label: '跟随本步' },
  { value: 'custom', label: '自定义' },
  { value: 'all', label: '全部展示' },
];

const toggleOptions: { key: keyof DisplayToggles; label: string }[] = [
  { key: 'code', label: '代码列表' },
  { key: 'cpuDetail', label: 'CPU 内部细节' },
  { key: 'alu', label: 'ALU 解释' },
  { key: 'flags', label: '条件标志' },
  { key: 'memory', label: '栈 / 内存' },
  { key: 'address', label: '地址' },
  { key: 'bytes', label: '机器码' },
  { key: 'bits', label: '字节位图' },
  { key: 'note', label: '教学说明' },
  { key: 'motion', label: '运动效果' },
];

/** 同一规范寄存器的别名只留一个 chip，显示用规范名。 */
const pinChoices = computed(() => {
  const seen = new Set<string>();
  return props.registers.filter(register => {
    if (seen.has(register.canonical)) return false;
    seen.add(register.canonical);
    return true;
  });
});

/** 就绪态没有可重置的东西；运行中也要留一个出口。 */
const canReset = computed(
  () => props.playing || ['running', 'paused', 'finished', 'error'].includes(props.status),
);

/** 播放中随时可以停住；空闲时要能起播。 */
const togglePlayDisabled = computed(() => !props.playing && !props.canRun && !props.canStep);

function onSpeed(event: Event) {
  emit('speed', (event.target as HTMLSelectElement).value as Speed);
}

function onToggle(key: keyof DisplayToggles, event: Event) {
  const checked = (event.target as HTMLInputElement).checked;
  emit('toggle', { [key]: checked } as Partial<DisplayToggles>);
}
</script>

<template>
  <div class="controls" role="group" aria-label="播放控制与显示设置">
    <div class="transport">
      <button type="button" class="primary" :disabled="!props.canStep" @click="emit('step')">单步</button>

      <button
        type="button"
        class="toggle-play"
        :aria-pressed="props.playing"
        :disabled="togglePlayDisabled"
        :title="props.playing ? '停住自动播放，停在当前这一步' : '按当前速度逐条自动播放'"
        @click="emit('toggle-play')"
      >
        <svg v-if="!props.playing" class="glyph" viewBox="0 0 16 16" width="11" height="11" aria-hidden="true" focusable="false">
          <path d="M4.5 2.5 12 8l-7.5 5.5Z" fill="currentColor" />
        </svg>
        <svg v-else class="glyph" viewBox="0 0 16 16" width="11" height="11" aria-hidden="true" focusable="false">
          <path d="M4.5 2.5h2.6v11H4.5Zm4.4 0h2.6v11H8.9Z" fill="currentColor" />
        </svg>
        {{ props.playing ? '停住' : '自动播放' }}
      </button>

      <button type="button" :disabled="!props.canPause" @click="emit('pause')">暂停</button>

      <button
        type="button"
        class="secondary"
        :disabled="!props.canRun"
        title="不做逐条动画，直接跑到结束或预算上限"
        @click="emit('run')"
      >继续</button>

      <button type="button" class="reset" :disabled="!canReset" @click="emit('reset')">重置</button>

      <span class="hint">第 {{ props.steps }} 步 · 每步一条真实指令</span>

      <div class="view-tools">
        <select class="speed" aria-label="播放速度" :value="props.speed" @change="onSpeed">
          <option v-for="option in speedOptions" :key="option.value" :value="option.value">
            {{ option.label }}
          </option>
        </select>

        <button
          type="button"
          class="gear"
          :aria-expanded="settingsOpen"
          aria-controls="scene-display-settings"
          :title="settingsOpen ? '收起显示设置' : '展开显示设置'"
          @click="settingsOpen = !settingsOpen"
        >
          <svg
            class="glyph"
            viewBox="0 0 24 24"
            width="13"
            height="13"
            fill="none"
            stroke="currentColor"
            stroke-width="1.6"
            stroke-linecap="round"
            stroke-linejoin="round"
            aria-hidden="true"
            focusable="false"
          >
            <circle cx="12" cy="12" r="3" />
            <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1Z" />
          </svg>
          <span>显示设置</span>
        </button>
      </div>
    </div>

    <div v-if="settingsOpen" id="scene-display-settings" class="settings-row" role="group" aria-label="显示设置">
      <div class="settings-grid">
        <div class="setting setting-presets">
          <span class="setting-label">显示预设</span>
          <div class="segments" role="group" aria-label="显示预设">
            <button
              v-for="option in presetOptions"
              :key="option.value"
              type="button"
              class="preset"
              :aria-pressed="props.preset === option.value"
              @click="emit('preset', option.value)"
            >{{ option.label }}</button>
          </div>
        </div>

        <div class="setting setting-toggles">
          <span class="setting-label">显示项</span>
          <div class="toggle-list" role="group" aria-label="显示项">
            <label v-for="option in toggleOptions" :key="option.key" class="toggle">
              <input type="checkbox" :checked="props.display[option.key]" @change="onToggle(option.key, $event)" />
              <span>{{ option.label }}</span>
            </label>
          </div>
        </div>

        <div class="setting setting-pins">
          <span class="setting-label">固定关注</span>
          <p class="setting-note">点一下固定，跨步不丢</p>
          <div v-if="pinChoices.length" class="chips" role="group" aria-label="固定关注的寄存器">
            <button
              v-for="register in pinChoices"
              :key="register.canonical"
              type="button"
              class="chip"
              :aria-pressed="props.pins.includes(register.canonical)"
              :title="props.pins.includes(register.canonical)
                ? `取消固定 ${register.canonical.toUpperCase()}`
                : `固定 ${register.canonical.toUpperCase()}，跨步不丢`"
              @click="emit('pin', register.canonical)"
            >{{ register.canonical.toUpperCase() }}</button>
          </div>
          <p v-else class="setting-note">暂无可固定的寄存器</p>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.controls {
  display: flex;
  flex-direction: column;
  gap: 10px;
  min-width: 0;
  padding-top: 12px;
  border-top: 1px solid var(--line);
  color: var(--ink);
  font-family: var(--sans);
}

.transport {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.controls button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  min-height: 30px;
  padding: 7px 12px;
  border: 1px solid var(--line-strong);
  border-radius: 7px;
  background: var(--paper-2);
  color: var(--ink-2);
  font: 600 12px/1.2 var(--sans);
  cursor: pointer;
  transition: background 120ms ease, color 120ms ease, border-color 120ms ease;
}

.controls button:hover:not(:disabled) {
  background: var(--paper-3);
}

.controls button:disabled {
  opacity: 0.38;
  cursor: default;
}

.controls button:focus-visible,
.controls select:focus-visible,
.controls input:focus-visible {
  outline: 2px solid var(--signal);
  outline-offset: 2px;
}

.primary {
  border-color: var(--signal);
  background: var(--signal);
  color: var(--paper-2);
  font-weight: 700;
}

.primary:hover:not(:disabled) {
  background: color-mix(in srgb, var(--signal) 84%, var(--ink));
}

.toggle-play[aria-pressed='true'] {
  border-color: var(--signal);
  background: color-mix(in srgb, var(--signal) 10%, var(--paper-2));
  color: var(--signal);
}

.toggle-play[aria-pressed='true']:hover:not(:disabled) {
  background: color-mix(in srgb, var(--signal) 16%, var(--paper-2));
}

.glyph {
  flex: none;
}

.secondary {
  border-color: var(--line-strong);
  background: transparent;
  color: var(--ink-2);
}

.reset {
  border-color: var(--line);
  color: var(--muted);
}

.hint {
  min-width: 0;
  color: var(--muted);
  font: 400 10px/1.5 var(--mono);
  font-variant-numeric: tabular-nums;
  overflow-wrap: anywhere;
}

.view-tools {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  min-width: 0;
  margin-left: auto;
}

.speed {
  max-width: 100%;
  min-height: 30px;
  padding: 6px 8px;
  border: 1px solid var(--line-strong);
  border-radius: 7px;
  background: var(--paper-2);
  color: var(--ink-2);
  font: 600 12px/1.2 var(--sans);
  cursor: pointer;
}

.gear {
  color: var(--ink-2);
}

.gear .glyph {
  color: var(--muted);
}

.gear[aria-expanded='true'] {
  border-color: var(--line-strong);
  background: var(--paper-3);
  color: var(--ink);
}

.gear[aria-expanded='true'] .glyph {
  color: var(--ink-2);
}

.settings-row {
  min-width: 0;
  padding: 12px;
  border: 1px solid var(--line);
  border-radius: 3px;
  background: var(--paper-2);
}

.settings-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px 24px;
  align-items: start;
  min-width: 0;
}

.setting {
  display: flex;
  flex-direction: column;
  gap: 8px;
  min-width: 0;
}

.setting-presets {
  grid-column: 1;
  grid-row: 1;
}

.setting-toggles {
  grid-column: 2;
  grid-row: 1 / span 2;
}

.setting-pins {
  grid-column: 1;
  grid-row: 2;
}

.setting-label {
  color: var(--muted);
  font: 600 10px/1.2 var(--sans);
  letter-spacing: 0.1em;
}

.segments {
  display: inline-flex;
  flex-wrap: wrap;
  gap: 4px;
  width: fit-content;
  max-width: 100%;
  padding: 3px;
  border: 1px solid var(--line);
  border-radius: 3px;
  background: var(--paper);
}

.segments .preset {
  border-color: transparent;
  background: transparent;
  color: var(--ink-2);
  font-weight: 600;
}

.segments .preset:hover:not(:disabled) {
  background: var(--paper-3);
}

.segments .preset[aria-pressed='true'] {
  border-color: var(--ink);
  background: var(--ink);
  color: var(--paper-2);
}

.segments .preset[aria-pressed='true']:hover:not(:disabled) {
  background: var(--ink);
}

.toggle-list {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 6px 14px;
  min-width: 0;
}

.toggle {
  display: flex;
  align-items: flex-start;
  gap: 7px;
  min-width: 0;
  color: var(--ink-2);
  font: 400 14px/1.45 var(--sans);
  cursor: pointer;
}

.toggle input {
  flex: none;
  width: 14px;
  height: 14px;
  margin: 2px 0 0;
  accent-color: var(--signal);
  cursor: pointer;
}

.toggle span {
  min-width: 0;
  overflow-wrap: anywhere;
}

.setting-note {
  margin: 0;
  color: var(--muted);
  font: 400 11px/1.5 var(--sans);
}

.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  min-width: 0;
}

.chip {
  min-height: 26px;
  padding: 5px 8px;
  border: 1px solid var(--line);
  border-radius: 3px;
  background: var(--paper);
  color: var(--ink-2);
  font: 500 11px/1.1 var(--mono);
  font-variant-numeric: tabular-nums;
  letter-spacing: 0.02em;
}

.chip:hover:not(:disabled) {
  background: var(--paper-3);
}

.chip[aria-pressed='true'] {
  border-color: var(--signal);
  background: color-mix(in srgb, var(--signal) 7%, var(--paper-2));
  color: var(--signal);
}

.chip[aria-pressed='true']:hover:not(:disabled) {
  background: color-mix(in srgb, var(--signal) 13%, var(--paper-2));
}

@container (max-width: 640px) {
  .settings-grid {
    grid-template-columns: minmax(0, 1fr);
  }

  .setting-presets,
  .setting-toggles,
  .setting-pins {
    grid-column: auto;
    grid-row: auto;
  }
}

@container (max-width: 430px) {
  .view-tools {
    margin-left: 0;
  }
}

@container (max-width: 300px) {
  .toggle-list {
    grid-template-columns: minmax(0, 1fr);
  }
}
</style>
