<script setup lang="ts">
// 文章状态提示框:frontmatter 里带 current_status 的文章,进入页面时
// 右下角滑入一个 MC 成就风格的提示。每次进入(含刷新)都会再弹一次,
// 悬停暂停倒计时,超时或点 × 滑出。
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute } from 'vitepress'

const route = useRoute()

type StatusVariant = 'polishing' | 'draft' | 'verified'

interface StatusConfig {
  title: string
  detail: string
  variant: StatusVariant
  icon: string
}

const VARIANT_META: Record<StatusVariant, { border: string; glow: string; icon: string; label: string }> = {
  polishing: { border: '#ffaa00', glow: 'rgba(255, 170, 0, 0.35)', icon: '🔨', label: '状态更新' },
  draft: { border: '#7aa2c4', glow: 'rgba(122, 162, 196, 0.30)', icon: '✏️', label: '草稿' },
  verified: { border: '#55c07a', glow: 'rgba(85, 192, 122, 0.30)', icon: '✅', label: '已验证' },
}

// frontmatter 的 current_status 接受一行字符串或 { title, detail, variant, icon }
function normalize(raw: unknown): StatusConfig | null {
  if (typeof raw === 'string' && raw.trim()) {
    return { title: raw.trim(), detail: '', variant: 'polishing', icon: '' }
  }
  if (raw && typeof raw === 'object') {
    const obj = raw as Record<string, unknown>
    const title = typeof obj.title === 'string' ? obj.title.trim() : ''
    if (!title) return null
    const variant = (['polishing', 'draft', 'verified'] as const).includes(obj.variant as StatusVariant)
      ? (obj.variant as StatusVariant)
      : 'polishing'
    return {
      title,
      detail: typeof obj.detail === 'string' ? obj.detail.trim() : '',
      variant,
      icon: typeof obj.icon === 'string' ? obj.icon.trim() : '',
    }
  }
  return null
}

const config = computed<StatusConfig | null>(() => normalize(route.data.frontmatter?.current_status))
const meta = computed(() => (config.value ? VARIANT_META[config.value.variant] : null))

const visible = ref(false)
const leaving = ref(false)
let hideTimer: ReturnType<typeof setTimeout> | null = null
let showTimer: ReturnType<typeof setTimeout> | null = null
let remainingMs = 6000
let startedAt = 0

function clearTimers() {
  if (showTimer) clearTimeout(showTimer), (showTimer = null)
  if (hideTimer) clearTimeout(hideTimer), (hideTimer = null)
}

function beginHide() {
  if (!visible.value || leaving.value) return
  leaving.value = true
  // 滑出动画结束后再卸载(与 CSS transition 时长对应)
  setTimeout(() => {
    visible.value = false
    leaving.value = false
  }, 420)
}

function scheduleHide() {
  hideTimer = setTimeout(beginHide, remainingMs)
}

function pauseHide() {
  if (!hideTimer) return
  clearTimeout(hideTimer)
  hideTimer = null
  remainingMs = Math.max(600, remainingMs - (Date.now() - startedAt))
}

function resumeHide() {
  if (hideTimer || !visible.value) return
  startedAt = Date.now()
  scheduleHide()
}

function show() {
  clearTimers()
  remainingMs = 6000
  showTimer = setTimeout(() => {
    visible.value = true
    startedAt = Date.now()
    scheduleHide()
  }, 800)
}

function maybeShow() {
  if (config.value) show()
}

onMounted(maybeShow)
watch(() => [route.data.path, config.value?.title, config.value?.detail], () => {
  clearTimers()
  visible.value = false
  leaving.value = false
  maybeShow()
})
onBeforeUnmount(clearTimers)
</script>

<template>
  <Teleport to="body">
    <div
      v-if="visible && config && meta"
      class="status-toast"
      :class="{ 'status-toast--leaving': leaving }"
      :style="{ '--st-border': meta.border, '--st-glow': meta.glow }"
      role="status"
      aria-live="polite"
      @mouseenter="pauseHide"
      @mouseleave="resumeHide"
    >
      <div class="status-toast__icon">{{ config.icon || meta.icon }}</div>
      <div class="status-toast__body">
        <div class="status-toast__title">{{ config.title }}</div>
        <div v-if="config.detail" class="status-toast__detail">{{ config.detail }}</div>
      </div>
      <button class="status-toast__close" aria-label="关闭提示" @click="beginHide">×</button>
    </div>
  </Teleport>
</template>

<style scoped>
.status-toast {
  position: fixed;
  bottom: 1.25rem;
  right: 1rem;
  z-index: 60;
  display: flex;
  align-items: center;
  gap: 0.7rem;
  max-width: min(22rem, calc(100vw - 2rem));
  padding: 0.65rem 0.9rem 0.65rem 0.65rem;
  background: rgba(24, 24, 27, 0.96);
  border: 2px solid var(--st-border, #ffaa00);
  border-radius: 4px;
  box-shadow:
    inset 0 0 0 1px rgba(255, 255, 255, 0.08),
    0 6px 24px rgba(0, 0, 0, 0.35),
    0 0 18px var(--st-glow, rgba(255, 170, 0, 0.3));
  color: #e4e4e7;
  font-size: 0.82rem;
  line-height: 1.45;
  animation: status-toast-in 0.42s cubic-bezier(0.2, 0.9, 0.3, 1.2) both;
}

.status-toast--leaving {
  animation: status-toast-out 0.4s ease-in both;
}

.status-toast__icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 2.4rem;
  height: 2.4rem;
  flex: none;
  background: rgba(0, 0, 0, 0.45);
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 3px;
  font-size: 1.15rem;
}

.status-toast__body {
  min-width: 0;
}

.status-toast__title {
  color: var(--st-border, #ffaa00);
  font-weight: 700;
  font-size: 0.86rem;
  letter-spacing: 0.02em;
}

.status-toast__detail {
  margin-top: 0.15rem;
  color: #a1a1aa;
  overflow-wrap: anywhere;
}

.status-toast__close {
  align-self: flex-start;
  margin-left: 0.2rem;
  padding: 0 0.3rem;
  background: none;
  border: none;
  color: #71717a;
  font-size: 1.05rem;
  line-height: 1;
  cursor: pointer;
}

.status-toast__close:hover {
  color: #d4d4d8;
}

@keyframes status-toast-in {
  from {
    transform: translateX(calc(100% + 1.5rem));
    opacity: 0;
  }
  to {
    transform: translateX(0);
    opacity: 1;
  }
}

@keyframes status-toast-out {
  from {
    transform: translateX(0);
    opacity: 1;
  }
  to {
    transform: translateX(calc(100% + 1.5rem));
    opacity: 0;
  }
}

@media (prefers-reduced-motion: reduce) {
  .status-toast,
  .status-toast--leaving {
    animation: none;
  }
}
</style>
