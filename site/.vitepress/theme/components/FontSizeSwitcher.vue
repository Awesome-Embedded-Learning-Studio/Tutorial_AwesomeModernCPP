<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'

// 正文字号五档:超小 / 小 / 正常 / 大(默认) / 超大,用 A- / A+ 步进切换。
// 写 documentElement.dataset.fontSize → custom.css 的 html[data-font-size] 用 zoom 整页缩放。
// 存 localStorage,首屏由 config head 内联脚本提前应用(默认 large),避免刷新闪烁。
type Size = 'xxsmall' | 'small' | 'normal' | 'large' | 'xxlarge'
const STORAGE_KEY = 'vp-font-size'
const order: Size[] = ['xxsmall', 'small', 'normal', 'large', 'xxlarge']
const label: Record<Size, string> = {
  xxsmall: '超小',
  small: '小',
  normal: '正常',
  large: '大',
  xxlarge: '超大',
}
const current = ref<Size>('normal')

function apply(s: Size) {
  current.value = s
  try {
    localStorage.setItem(STORAGE_KEY, s)
  } catch {}
  if (typeof document !== 'undefined') {
    document.documentElement.dataset.fontSize = s
  }
}

function step(dir: -1 | 1) {
  const i = order.indexOf(current.value)
  const next = Math.min(order.length - 1, Math.max(0, i + dir))
  if (next !== i) apply(order[next])
}

const atMin = computed(() => current.value === order[0])
const atMax = computed(() => current.value === order[order.length - 1])

onMounted(() => {
  try {
    const saved = localStorage.getItem(STORAGE_KEY) as Size | null
    if (saved && order.includes(saved)) current.value = saved
  } catch {}
})
</script>

<template>
  <div class="font-size-switcher" role="group" aria-label="正文字号">
    <button
      type="button"
      class="font-size-switcher__btn"
      :disabled="atMin"
      title="调小字号"
      aria-label="调小字号"
      @click="step(-1)"
    >A-</button>
    <span class="font-size-switcher__current" :title="'当前：' + label[current]">{{ label[current] }}</span>
    <button
      type="button"
      class="font-size-switcher__btn"
      :disabled="atMax"
      title="调大字号"
      aria-label="调大字号"
      @click="step(1)"
    >A+</button>
  </div>
</template>

<style scoped>
.font-size-switcher {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  margin: 0 8px;
  padding: 2px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 14px;
  background: var(--vp-c-bg-soft);
}

.font-size-switcher__btn {
  min-width: 26px;
  height: 24px;
  padding: 0 7px;
  border: 0;
  border-radius: 11px;
  background: transparent;
  color: var(--vp-c-text-2);
  font-size: 12px;
  font-weight: 600;
  line-height: 1;
  cursor: pointer;
  transition: background-color 0.2s ease, color 0.2s ease;
}

.font-size-switcher__btn:hover:not(:disabled) {
  color: var(--vp-c-brand-1);
}

.font-size-switcher__btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.font-size-switcher__current {
  min-width: 32px;
  color: var(--vp-c-text-2);
  font-size: 12px;
  font-weight: 600;
  line-height: 1;
  text-align: center;
  user-select: none;
}
</style>
