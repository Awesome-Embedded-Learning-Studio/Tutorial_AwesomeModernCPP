<script setup lang="ts">
import { computed } from 'vue'

const props = withDefaults(defineProps<{
  talkTitle: string
  speaker: string
  conference: string
  year: number | string
  videoBilibili?: string
  videoYoutube?: string
  slidesUrl?: string
}>(), {
  videoBilibili: '',
  videoYoutube: '',
  slidesUrl: '',
})

interface ConferenceMeta {
  label: string
  color: string
  bg: string
}

const conferences: Record<string, ConferenceMeta> = {
  cppcon: { label: 'CppCon', color: '#6366f1', bg: 'rgba(99,102,241,0.10)' },
  cppnow: { label: 'CppNow', color: '#22c55e', bg: 'rgba(34,197,94,0.10)' },
  meetingpp: { label: 'Meeting C++', color: '#a855f7', bg: 'rgba(168,85,247,0.10)' },
  course: { label: 'Course', color: '#f59e0b', bg: 'rgba(245,158,11,0.10)' },
  blog: { label: 'Blog', color: '#06b6d4', bg: 'rgba(6,182,212,0.10)' },
}

const meta = computed(() => conferences[props.conference] ?? conferences.blog)

const hasLinks = computed(() =>
  props.videoBilibili || props.videoYoutube || props.slidesUrl,
)
</script>

<template>
  <div class="talk-info-card">
    <div class="talk-header">
      <span class="talk-badge" :style="{ color: meta.color, backgroundColor: meta.bg }">
        {{ meta.label }}
      </span>
      <span class="talk-year">{{ year }}</span>
    </div>

    <div class="talk-body">
      <p class="talk-title">{{ talkTitle }}</p>
      <p class="talk-speaker">{{ speaker }}</p>
    </div>

    <div v-if="hasLinks" class="talk-footer">
      <a v-if="videoBilibili" :href="videoBilibili" target="_blank" rel="noopener noreferrer" class="talk-link">
        <svg class="talk-link-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <polygon points="5 3 19 12 5 21 5 3" />
        </svg>
        Bilibili
      </a>
      <a v-if="videoYoutube" :href="videoYoutube" target="_blank" rel="noopener noreferrer" class="talk-link">
        <svg class="talk-link-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <polygon points="5 3 19 12 5 21 5 3" />
        </svg>
        YouTube
      </a>
      <a v-if="slidesUrl" :href="slidesUrl" target="_blank" rel="noopener noreferrer" class="talk-link">
        <svg class="talk-link-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
          <polyline points="14 2 14 8 20 8" />
        </svg>
        Slides
      </a>
    </div>
  </div>
</template>

<style scoped>
.talk-info-card {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 18px 20px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  background-color: var(--vp-c-bg);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.04),
              0 1px 2px rgba(0, 0, 0, 0.06);
  transition: border-color 0.35s ease,
              box-shadow 0.35s ease,
              transform 0.35s ease;
}

.talk-info-card:hover {
  border-color: var(--vp-c-brand-1);
  box-shadow: 0 10px 28px rgba(0, 0, 0, 0.1),
              0 4px 8px rgba(0, 0, 0, 0.06);
  transform: translateY(-2px);
}

/* ── Header ──────────────────────────────── */

.talk-header {
  display: flex;
  align-items: center;
  gap: 10px;
}

.talk-badge {
  display: inline-flex;
  align-items: center;
  padding: 2px 10px;
  border-radius: 6px;
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.03em;
}

.talk-year {
  font-size: 13px;
  color: var(--vp-c-text-3);
  font-variant-numeric: tabular-nums;
}

/* ── Body ────────────────────────────────── */

.talk-body {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.talk-title {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
  line-height: 1.5;
  color: var(--vp-c-text-1);
}

.talk-speaker {
  margin: 0;
  font-size: 13px;
  color: var(--vp-c-text-2);
}

/* ── Footer ──────────────────────────────── */

.talk-footer {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.talk-link {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 4px 10px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  font-size: 12px;
  font-weight: 500;
  color: var(--vp-c-text-2);
  text-decoration: none !important;
  transition: border-color 0.25s ease, color 0.25s ease, background-color 0.25s ease;
}

.talk-link:hover {
  border-color: var(--vp-c-brand-1);
  color: var(--vp-c-brand-1);
  background-color: var(--vp-c-brand-soft);
}

.talk-link-icon {
  width: 14px;
  height: 14px;
}

/* ── Dark Mode ───────────────────────────── */

.dark .talk-info-card {
  background-color: var(--vp-c-bg-elv);
  border-color: var(--vp-c-border);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.2),
              0 1px 2px rgba(0, 0, 0, 0.15);
}

.dark .talk-info-card:hover {
  box-shadow: 0 10px 28px rgba(0, 0, 0, 0.3),
              0 4px 8px rgba(0, 0, 0, 0.2);
}

/* ── Responsive ──────────────────────────── */

@media (max-width: 639px) {
  .talk-info-card {
    padding: 14px 16px;
  }

  .talk-title {
    font-size: 14px;
  }
}
</style>
