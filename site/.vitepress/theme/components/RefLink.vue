<script setup lang="ts">
withDefaults(defineProps<{
  id: number | string
  preview?: string
}>(), {
  preview: '',
})

function scrollToRef(id: number | string) {
  const el = document.getElementById(`ref-${id}`)
  if (el) {
    el.scrollIntoView({ behavior: 'smooth', block: 'center' })
    el.classList.add('ref-highlight')
    setTimeout(() => el.classList.remove('ref-highlight'), 2000)
  }
}
</script>

<template>
  <span class="ref-link-wrapper">
    <sup class="ref-link" @click="scrollToRef(id)">
      [{{ id }}]
    </sup>
    <span v-if="preview" class="ref-bubble" v-html="preview" />
  </span>
</template>

<style scoped>
.ref-link-wrapper {
  position: relative;
  display: inline;
}

.ref-link {
  color: var(--vp-c-brand-1);
  cursor: pointer;
  font-size: 0.75em;
  font-weight: 600;
  line-height: 0;
  vertical-align: super;
  transition: color 0.25s ease;
  user-select: none;
  padding: 0 1px;
}

.ref-link:hover {
  color: var(--vp-c-brand-2);
}

.ref-bubble {
  position: absolute;
  bottom: calc(100% + 8px);
  left: 50%;
  transform: translateX(-50%) translateY(4px);
  z-index: 99;
  min-width: 180px;
  max-width: 320px;
  padding: 10px 14px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  background-color: var(--vp-c-bg-elv);
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.1);
  font-size: 13px;
  line-height: 1.5;
  color: var(--vp-c-text-2);
  font-style: italic;
  white-space: normal;
  opacity: 0;
  pointer-events: none;
  transition: opacity 0.25s ease, transform 0.25s ease;
}

.ref-bubble::after {
  content: '';
  position: absolute;
  top: 100%;
  left: 50%;
  transform: translateX(-50%);
  border: 6px solid transparent;
  border-top-color: var(--vp-c-bg-elv);
}

.ref-link-wrapper:hover .ref-bubble {
  opacity: 1;
  pointer-events: auto;
  transform: translateX(-50%) translateY(0);
}

/* ── Dark Mode ───────────────────────────── */

.dark .ref-bubble {
  border-color: var(--vp-c-border);
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.3);
}

.dark .ref-bubble::after {
  border-top-color: var(--vp-c-bg-elv);
}

/* ── Mobile: tap to toggle ───────────────── */

@media (max-width: 639px) {
  .ref-bubble {
    left: 0;
    transform: translateX(0) translateY(4px);
    max-width: 260px;
  }

  .ref-bubble::after {
    left: 20px;
    transform: none;
  }

  .ref-link-wrapper:hover .ref-bubble {
    transform: translateX(0) translateY(0);
  }
}
</style>
