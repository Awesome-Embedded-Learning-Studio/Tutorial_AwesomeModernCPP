<script setup lang="ts">
withDefaults(defineProps<{
  title?: string
}>(), {
  title: '参考文献',
})
</script>

<template>
  <div class="reference-card">
    <div class="reference-card-header">
      <svg class="reference-card-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20" />
        <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z" />
      </svg>
      <span class="reference-card-title">{{ title }}</span>
    </div>
    <div class="reference-card-body">
      <slot />
    </div>
  </div>
</template>

<style scoped>
.reference-card {
  margin-top: 32px;
  border-left: 3px solid var(--vp-c-brand-1);
  border-radius: 12px;
  background-color: var(--vp-c-bg-soft);
  padding: 18px 20px;
}

.reference-card-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 16px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--vp-c-divider);
}

.reference-card-icon {
  width: 18px;
  height: 18px;
  color: var(--vp-c-brand-1);
  flex-shrink: 0;
}

.reference-card-title {
  font-size: 16px;
  font-weight: 700;
  color: var(--vp-c-text-1);
}

.reference-card-body {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

/* ── Dark Mode ───────────────────────────── */

.dark .reference-card {
  background-color: var(--vp-c-bg-elv);
}

/* ── Responsive ──────────────────────────── */

@media (max-width: 639px) {
  .reference-card {
    padding: 14px 16px;
  }

  .reference-card-title {
    font-size: 15px;
  }
}
</style>
