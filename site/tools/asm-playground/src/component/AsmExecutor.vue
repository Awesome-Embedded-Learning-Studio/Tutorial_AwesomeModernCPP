<script setup lang="ts">
import { computed, inject, onBeforeUnmount, onMounted, ref, shallowRef, watch } from 'vue';
import { GPRS, registerInfo, type PayloadV1 } from '../payload/types';
import { validatePayload } from '../payload/validate';
import type { ExecutorClient } from '../engine/client';
import type { MemAccess, Snapshot } from '../engine/types';
import { DEFAULT_WORKER_URL, optionsKey } from './options';
import CodeRail from './scene/CodeRail.vue';
import CpuDie from './scene/CpuDie.vue';
import StackPanel from './scene/StackPanel.vue';
import SceneControls from './scene/SceneControls.vue';
import FlowLayer from './scene/FlowLayer.vue';
import { buildScene } from './scene/model';
import { ALL_DISPLAY, DEFAULT_DISPLAY, SPEED_MS, type DisplayToggles, type Preset, type Speed } from './scene/display';
import './scene/scene.css';

const props = defineProps<{ src: string; workerUrl?: string }>();
const configured = inject(optionsKey, {});

const root = ref<HTMLElement>();
const sceneEl = ref<HTMLElement>();
const payload = shallowRef<PayloadV1>();
const state = shallowRef<Snapshot>();
const previous = shallowRef<Snapshot>();
const loading = ref(false), error = ref(''), visible = ref(false), pending = ref(false);
const width = ref(0);

const preset = ref<Preset>('follow');
const custom = ref<DisplayToggles>({ ...DEFAULT_DISPLAY });
const pins = ref<string[]>([]);
const speed = ref<Speed>('normal');
const playing = ref(false);
const settingsOpen = ref(false);
const codeOpen = ref(false);
const reduced = ref(false);

let client: ExecutorClient | undefined;
let observer: IntersectionObserver | undefined;
let controller: AbortController | undefined;
let resizeObserver: ResizeObserver | undefined;
let timer: ReturnType<typeof setTimeout> | undefined;
let revision = 0;

const labels: Record<string, string> = { ready: '准备就绪', running: '执行中', paused: '已暂停', finished: '执行完成', error: '执行出错' };
const status = computed(() => loading.value ? '加载中' : error.value ? '执行出错' : state.value ? labels[state.value.status] : '滚动到此处后加载');
const scene = computed(() => (payload.value ? buildScene(payload.value, state.value, previous.value) : undefined));
const display = computed<DisplayToggles>(() => preset.value === 'all' ? ALL_DISPLAY : custom.value);
const layout = computed(() => (width.value >= 980 ? 'wide' : width.value >= 620 ? 'mid' : 'narrow'));
const motionOn = computed(() => display.value.motion && !reduced.value);
const speedMs = computed(() => SPEED_MS[speed.value]);

const canExecute = computed(() => !!state.value && !loading.value && !pending.value && !error.value && !['finished', 'error', 'running'].includes(state.value.status));
const canStep = computed(() => canExecute.value && !playing.value);
const canRun = computed(() => canExecute.value && !playing.value);
const canPause = computed(() => state.value?.status === 'running' || playing.value);

// 跟随本步：关注集合随当前指令走；自定义：只留固定项与栈指针；全部：16 个全开。
const visibleRegs = computed(() => {
  if (preset.value === 'all') return [...GPRS];
  const set = new Set(pins.value.filter(name => (GPRS as readonly string[]).includes(name)));
  if (preset.value === 'follow') {
    for (const name of scene.value?.focus ?? []) {
      const canonicalName = registerInfo(name)?.canonical;
      if (canonicalName && (GPRS as readonly string[]).includes(canonicalName)) set.add(canonicalName);
    }
  }
  set.add('rsp');
  set.add('rbp');
  return [...set];
});

const memReads = computed<MemAccess[]>(() => (scene.value?.reads ?? []).filter((access): access is MemAccess => access.kind === 'mem'));
const memWrites = computed<MemAccess[]>(() => (scene.value?.writes ?? []).filter((access): access is MemAccess => access.kind === 'mem'));
const wallTouched = computed(() => memReads.value.length > 0 || memWrites.value.length > 0);
const registers = [...GPRS, 'rip', 'rflags'].map(name => ({ name: name.toUpperCase(), canonical: name }));
const message = computed(() => error.value || state.value?.reason || '');

function togglePin(name: string) {
  pins.value = pins.value.includes(name) ? pins.value.filter(item => item !== name) : [...pins.value, name];
  if (preset.value === 'all') preset.value = 'custom';
}

function setPreset(value: Preset) { preset.value = value; }

function toggle(patch: Partial<DisplayToggles>) {
  custom.value = { ...custom.value, ...patch };
  if (preset.value === 'all') preset.value = 'custom';
}

function setSpeed(value: Speed) { speed.value = value; }

function requestStep() {
  if (!canExecute.value) { stopPlay(); return; }
  pending.value = true;
  client?.step();
}

function schedule(delay = speedMs.value.dwell) {
  clearTimeout(timer);
  timer = setTimeout(() => {
    if (!playing.value) return;
    if (!canExecute.value) { stopPlay(); return; }
    requestStep();
  }, delay);
}

function startPlay() {
  if (!canExecute.value) return;
  playing.value = true;
  requestStep();
}

function stopPlay() {
  playing.value = false;
  clearTimeout(timer);
  timer = undefined;
}

function togglePlay() { if (playing.value) stopPlay(); else startPlay(); }

function onSettled() {
  if (!playing.value) return;
  if (state.value?.status === 'finished' || state.value?.status === 'error') { stopPlay(); return; }
  schedule();
}

async function load() {
  const current = ++revision;
  stopPlay();
  controller?.abort(); controller = new AbortController(); client?.destroy(); client = undefined;
  loading.value = true; error.value = ''; state.value = undefined; previous.value = undefined; payload.value = undefined; pending.value = false;
  try {
    const response = await fetch(new URL(props.src, document.baseURI), { signal: controller.signal });
    if (!response.ok) throw new Error(`负载请求失败：HTTP ${response.status}`);
    const p = validatePayload(await response.json());
    if (current !== revision) return;
    const { ExecutorClient } = await import('../engine/client');
    if (current !== revision) return;
    payload.value = p;
    client = new ExecutorClient({
      workerUrl: props.workerUrl ?? configured.workerUrl ?? DEFAULT_WORKER_URL,
      onSnapshot(snapshot) {
        if (current !== revision) return;
        previous.value = state.value; state.value = snapshot; loading.value = false; pending.value = false;
        if (snapshot.status === 'finished' || snapshot.status === 'error') stopPlay();
      },
      onError(text) { if (current !== revision) return; error.value = text; loading.value = false; pending.value = false; stopPlay(); },
    });
    client.load(p);
  } catch (cause) {
    if (current !== revision) return;
    error.value = String(cause); loading.value = false;
  }
}

function reset() { void load(); }
function run() { stopPlay(); if (canRun.value) { pending.value = true; client?.run(); } }
function pause() { stopPlay(); client?.pause(); }

onMounted(() => {
  observer = new IntersectionObserver(entries => {
    if (!entries.some(entry => entry.isIntersecting)) return;
    observer?.disconnect(); visible.value = true; void load();
  });
  observer.observe(root.value!);
  if (typeof ResizeObserver !== 'undefined' && root.value) {
    resizeObserver = new ResizeObserver(entries => { width.value = entries[0].contentRect.width; });
    resizeObserver.observe(root.value);
    width.value = root.value.getBoundingClientRect().width;
  }
  const query = window.matchMedia('(prefers-reduced-motion: reduce)');
  reduced.value = query.matches;
  query.addEventListener('change', event => { reduced.value = event.matches; });
});

watch(() => [props.src, props.workerUrl], () => { if (visible.value) void load(); });
onBeforeUnmount(() => {
  revision++; stopPlay(); observer?.disconnect(); resizeObserver?.disconnect(); controller?.abort(); client?.destroy();
});
</script>

<template>
  <section ref="root" class="asm-executor asm-scene" aria-label="汇编执行器" :aria-busy="loading">
    <header class="head">
      <div class="title">
        <span class="label">CPU · X86-64</span>
        <h2>{{ payload?.meta.title ?? '汇编执行器' }}</h2>
      </div>
      <div class="meta">
        <span class="counter mono">第 {{ state?.steps ?? 0 }} 步</span>
        <span class="status" role="status">{{ status }}<template v-if="state"> · {{ state.steps }} 步</template></span>
      </div>
    </header>

    <div v-if="payload && state && scene" ref="sceneEl" class="stage" :class="'is-' + layout">
      <div class="core">
        <CpuDie
          :regs="state.regs"
          :changed="[...scene.changed]"
          :written="[...scene.written]"
          :focus="[...scene.focus]"
          :visible="visibleRegs"
          :pinned="pins"
          :alu="scene.event?.alu"
          :flags="scene.event?.flags.changed ?? []"
          :show-alu="display.alu"
          :show-flags="display.flags"
          :detail="display.cpuDetail"
          :show-address="display.address"
          :show-bytes="display.bytes"
          :current="scene.current"
          :next="scene.next"
          @pin="togglePin"
        />
      </div>
      <div class="side">
      <StackPanel
        v-if="display.memory"
        class="wall-col"
        :rows="scene.mem"
        :pointers="scene.pointers"
        :reads="memReads"
        :writes="memWrites"
        :show-bits="display.bits"
        :class="{ 'is-idle': !wallTouched && (state?.steps ?? 0) > 0 }"
      />
      <p v-if="display.note" class="narration" aria-live="polite">
        <span class="narration-text">{{ scene.narration.text }}</span>
        <span v-if="scene.narration.chips.length" class="narration-chips">
          <span v-for="chip in scene.narration.chips" :key="chip.label + chip.value" class="chip mono" :class="'is-' + chip.tone">
            <span class="chip-label">{{ chip.label }}</span>{{ chip.value }}
          </span>
        </span>
      </p>
      </div>
      <section v-if="display.code" class="code-panel" :class="{ 'is-open': codeOpen }" aria-label="代码列表">
        <button type="button" class="code-tab" :aria-expanded="codeOpen" @click="codeOpen = !codeOpen">
          <span class="code-tab-label">代码列表</span>
          <span class="code-tab-meta mono">{{ scene.current?.text ?? '…' }}</span>
          <span class="code-tab-caret" aria-hidden="true">{{ codeOpen ? '收起来 ▴' : '展开 ▾' }}</span>
        </button>
        <CodeRail
          v-if="codeOpen"
          bare
          class="code-body"
          :lines="scene.lines"
          :current="scene.current"
          :next="scene.next"
          :target="scene.event?.flow.target"
          :show-address="display.address"
          :show-bytes="display.bytes"
          :dense="layout !== 'wide'"
        />
      </section>
      <FlowLayer
        :host="sceneEl"
        :event="scene.event"
        :enabled="motionOn"
        :duration="speedMs.move"
        @settled="onSettled"
      />
    </div>

    <p v-else class="placeholder">
      {{ loading ? '正在准备机器码与执行环境…' : error ? '请检查负载，或点击重置重试。' : '准备好后，在这里亲手观察一次函数调用。' }}
    </p>

    <div class="dock" :class="{ 'is-settings': settingsOpen }">
    <SceneControls
      :status="error ? 'error' : (state?.status ?? 'ready')"
      :steps="state?.steps ?? 0"
      :can-step="canStep"
      :can-run="canRun"
      :can-pause="canPause"
      :playing="playing"
      :speed="speed"
      :preset="preset"
      :display="display"
      :pins="pins"
      :registers="registers"
      @step="requestStep"
      @run="run"
      @pause="pause"
      @reset="reset"
      @toggle-play="togglePlay"
      @speed="setSpeed"
      @preset="setPreset"
      @toggle="toggle"
      @pin="togglePin"
      @settings="settingsOpen = $event"
    />
    </div>

    <p v-if="message" class="asm-message" :role="error || state?.status === 'error' ? 'alert' : 'status'">{{ message }}</p>
  </section>
</template>

<style scoped>
.asm-executor {
  display: flex;
  flex-direction: column;
  padding: 0;
  color: var(--ink);
  background: var(--paper);
  border: 1px solid var(--line);
  border-radius: 6px;
  overflow: visible;
  text-align: left;
  max-width: 100%;
}

.head {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 16px;
  padding: 18px 22px 12px;
}
.title h2 { margin: 4px 0 0; font: 600 19px/1.3 var(--sans); color: var(--ink); border: 0; padding: 0; }
.meta { display: flex; align-items: center; gap: 12px; }
.counter { font-size: 12px; color: var(--muted); }
.status {
  font-size: 11px;
  padding: 4px 10px;
  border-radius: 30px;
  color: var(--ink-2);
  background: var(--paper-3);
  white-space: nowrap;
}

.stage {
  position: relative;
  overflow: hidden;
  display: grid;
  gap: 16px;
  width: 100%;
  max-width: 960px;
  margin: 0 auto;
  padding: 0 18px 14px;
  align-items: start;
}
.stage.is-wide {
  grid-template-columns: minmax(0, 1fr) 400px;
  grid-template-areas: "cpu side" "code code";
}
.stage.is-mid {
  grid-template-columns: minmax(0, 1fr) 352px;
  grid-template-areas: "cpu side" "code code";
}
.stage.is-narrow {
  grid-template-columns: minmax(0, 1fr);
  grid-template-areas: "cpu" "note" "wall" "code";
}


.side {
  grid-area: side;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.stage.is-narrow .side { display: contents; }

.code-panel {
  grid-area: code;
  min-width: 0;
  border: 1px solid var(--line);
  border-radius: 10px;
  background: color-mix(in srgb, var(--paper) 60%, var(--paper-2));
}

.code-tab {
  display: flex;
  align-items: baseline;
  gap: 10px;
  width: 100%;
  padding: 8px 12px;
  border: 0;
  border-radius: 10px;
  background: transparent;
  color: var(--muted);
  font: 12px/1.4 var(--sans);
  text-align: left;
  cursor: pointer;
}
.code-tab:hover { background: color-mix(in srgb, var(--paper-2) 55%, transparent); }
.code-tab-label { font-size: 11px; font-weight: 600; letter-spacing: 0.1em; }
.code-tab-meta {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: var(--ink-2);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}
.code-tab-caret { flex: 0 0 auto; font-size: 10px; }

.code-body {
  height: 168px;
  border-top: 1px solid var(--line);
}

.core { grid-area: cpu; min-width: 0; display: flex; flex-direction: column; gap: 10px; }
.stage.is-wide .core, .stage.is-mid .core { max-width: 520px; justify-self: center; width: 100%; }
.stage.is-narrow .core { max-width: 440px; margin: 0 auto; width: 100%; }

.narration {
  grid-area: note;
  display: flex;
  flex-wrap: wrap;
  align-items: baseline;
  gap: 8px 14px;
  margin: 0;
  padding: 10px 14px;
  border-left: 3px solid color-mix(in srgb, var(--signal) 65%, transparent);
  border-radius: 8px;
  background: var(--paper-2);
  color: var(--ink);
  font-size: 14px;
  line-height: 1.55;
}
.narration-text { flex: 1 1 240px; min-width: 0; }
.narration-chips { display: flex; flex-wrap: wrap; gap: 6px; }
.chip {
  display: inline-flex;
  align-items: baseline;
  gap: 5px;
  font-size: 11px;
  padding: 2px 7px;
  border-radius: 3px;
  background: var(--paper-3);
  color: var(--ink-2);
}
.chip-label { font: 600 9px/1 var(--sans); letter-spacing: 0.08em; color: var(--muted); }
.chip.is-read { box-shadow: inset 0 0 0 1px var(--read); background: transparent; color: var(--read); }
.chip.is-write { background: var(--write-soft); color: var(--write); }

.wall-col { grid-area: wall; min-width: 0; }
.stage.is-narrow .wall-col { max-height: 180px; }

.placeholder {
  margin: 0;
  padding: 48px 22px;
  color: var(--muted);
  font-size: 13px;
}

.asm-message {
  margin: 0;
  padding: 10px 22px;
  font-size: 12px;
  color: var(--write);
  border-top: 1px solid var(--line);
  background: var(--paper-2);
}
.asm-message[role="status"] { color: var(--ink-2); }

@container (max-width: 620px) {
  /* 窄屏把操作条钉在组件底部，读者不必滚过整块场景才找得到单步。 */
  /* 设置面板展开时别把整块面板钉在屏幕上。 */
  .dock.is-settings { position: static; }
  .dock {
    position: sticky;
    bottom: 0;
    z-index: 6;
    padding: 0 12px calc(6px + env(safe-area-inset-bottom, 0px));
    background: var(--paper);
    border-top: 1px solid var(--line);
    box-shadow: 0 -10px 18px -14px rgb(23 37 31 / 45%);
  }
  .wall-col.is-idle { display: none; }
  .head { align-items: center; padding: 10px 12px; gap: 8px; }
  .title .label { display: none; }
  .title h2 { margin-top: 0; font-size: 16px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .meta { flex: 1 1 auto; justify-content: flex-end; gap: 6px; }
  .counter { display: none; }
  .status { font-size: 10px; padding: 3px 8px; }
  .stage { padding: 0 12px 12px; gap: 12px; }
  .narration { font-size: 13px; padding: 8px 10px; }
}
</style>
