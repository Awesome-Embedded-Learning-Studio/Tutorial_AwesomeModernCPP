<script setup lang="ts">
import { inject, computed } from 'vue'

const props = withDefaults(defineProps<{
  num?: string | number
  href: string
  variant?: 'main' | 'sub'
}>(), {
  variant: undefined
})

const navVariant = inject<'main' | 'sub'>('chapterNavVariant', 'main')
const effectiveVariant = computed(() => props.variant ?? navVariant)
</script>

<template>
  <a :href="href" class="chapter-link" :class="[`chapter-link--${effectiveVariant}`]">
    <span v-if="effectiveVariant === 'main' && num !== undefined" class="chapter-badge">
      {{ String(num).padStart(2, '0') }}
    </span>
    <span class="chapter-title">
      <slot />
    </span>
    <span class="chapter-arrow" aria-hidden="true">→</span>
  </a>
</template>

<style scoped>
.chapter-link {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px 18px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  background-color: var(--vp-c-bg);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.04),
              0 1px 2px rgba(0, 0, 0, 0.06);
  text-decoration: none !important;
  color: var(--vp-c-text-1);
  transition: border-color 0.35s ease,
              box-shadow 0.35s ease,
              transform 0.35s ease;
}

.chapter-link:hover {
  border-color: var(--vp-c-brand-1);
  box-shadow: 0 10px 28px rgba(0, 0, 0, 0.1),
              0 4px 8px rgba(0, 0, 0, 0.06);
  transform: translateY(-3px);
}

/* ── Badge ────────────────────────────────── */

.chapter-badge {
  flex-shrink: 0;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 36px;
  height: 36px;
  padding: 0 6px;
  border-radius: 8px;
  background: linear-gradient(
    135deg,
    var(--vp-c-brand-soft) 0%,
    var(--vp-c-indigo-soft) 100%
  );
  color: var(--vp-c-brand-1);
  font-size: 13px;
  font-weight: 700;
  font-variant-numeric: tabular-nums;
  letter-spacing: 0.02em;
  transition: background 0.35s ease, color 0.35s ease, transform 0.35s ease;
}

.chapter-link:hover .chapter-badge {
  background: linear-gradient(
    135deg,
    var(--vp-c-brand-1) 0%,
    var(--vp-c-indigo-1) 100%
  );
  color: var(--vp-c-white);
  transform: scale(1.06);
}

/* ── Title ────────────────────────────────── */

.chapter-title {
  flex: 1;
  min-width: 0;
  font-size: 14px;
  font-weight: 500;
  line-height: 1.5;
  transition: color 0.35s ease;
}

.chapter-link:hover .chapter-title {
  color: var(--vp-c-brand-1);
}

/* ── Arrow ────────────────────────────────── */

.chapter-arrow {
  flex-shrink: 0;
  font-size: 16px;
  color: var(--vp-c-text-3);
  transition: transform 0.35s ease, color 0.35s ease;
}

.chapter-link:hover .chapter-arrow {
  transform: translateX(4px);
  color: var(--vp-c-brand-1);
}

/* ── Sub variant (supplementary materials) ── */

.chapter-link--sub {
  padding: 12px 16px;
  border-radius: 10px;
  gap: 10px;
}

.chapter-link--sub .chapter-title {
  font-size: 13.5px;
  font-weight: 400;
}

.chapter-link--sub .chapter-arrow {
  font-size: 14px;
}

.chapter-link--sub:hover {
  transform: translateY(-2px);
}

/* ── Dark Mode ────────────────────────────── */

.dark .chapter-link {
  background-color: var(--vp-c-bg-elv);
  border-color: var(--vp-c-border);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.2),
              0 1px 2px rgba(0, 0, 0, 0.15);
}

.dark .chapter-link:hover {
  box-shadow: 0 10px 28px rgba(0, 0, 0, 0.3),
              0 4px 8px rgba(0, 0, 0, 0.2);
}

/* ── Responsive ───────────────────────────── */

@media (max-width: 639px) {
  .chapter-link {
    padding: 14px 14px;
    gap: 10px;
  }

  .chapter-badge {
    min-width: 32px;
    height: 32px;
    font-size: 12px;
  }

  .chapter-title {
    font-size: 13.5px;
  }
}
</style>
