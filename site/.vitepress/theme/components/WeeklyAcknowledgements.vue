<script setup lang="ts">
import { ref } from 'vue'
import type { WeeklyContributor } from '../utils/weekly'

defineProps<{
  people: WeeklyContributor[]
}>()
const unavailableAvatars = ref(new Set<string>())
</script>

<template>
  <aside v-if="people.length" class="weekly-thanks" aria-label="本期鸣谢">
    <div class="weekly-thanks__heading">
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" aria-hidden="true">
        <path d="m12 3 2.6 5.6 6.1.8-4.5 4.3 1.1 6.1-5.3-3-5.3 3 1.1-6.1L3.3 9.4l6.1-.8L12 3Z" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round" />
      </svg>
      <span>本期鸣谢</span><span class="weekly-thanks__note">谢谢你，让好题在这里相遇。</span>
    </div>
    <ul class="weekly-thanks__people">
      <li v-for="person in people" :key="person.github">
        <span class="weekly-thanks__avatar" aria-hidden="true">
          <img v-if="!unavailableAvatars.has(person.github)"
            :src="`https://github.com/${encodeURIComponent(person.github)}.png?size=96`"
            alt="" width="44" height="44" loading="lazy" decoding="async" referrerpolicy="no-referrer"
            @error="unavailableAvatars.add(person.github)" />
          <span v-else>{{ person.github.slice(0, 2).toUpperCase() }}</span>
        </span>
        <div class="weekly-thanks__identity">
          <a :href="`https://github.com/${encodeURIComponent(person.github)}`" target="_blank" rel="noreferrer">
            {{ person.github }} <span aria-hidden="true">↗</span>
            <span class="weekly-thanks__sr">的 GitHub（新窗口打开）</span>
          </a>
          <span class="weekly-thanks__role">{{ person.role }}</span>
        </div>
      </li>
    </ul>
  </aside>
</template>

<style scoped>
.weekly-thanks { width: 100%; margin: 0 0 20px; padding: 15px 18px; border: 1px solid var(--vp-c-divider); border-left: 3px solid var(--weekly-amber); border-radius: 0 10px 10px 0; background: color-mix(in srgb, var(--weekly-amber) 5%, var(--vp-c-bg-elv)); }
.weekly-thanks__heading { display: flex; align-items: center; flex-wrap: wrap; gap: 7px; color: var(--weekly-amber); font-size: 12px; font-weight: 600; line-height: 1.6; }
.weekly-thanks__note { flex-basis: 100%; margin-left: 25px; color: var(--vp-c-text-2); font-size: 11px; font-weight: 400; }
.weekly-thanks__people { list-style: none; margin: 12px 0 0; padding: 0; }
.weekly-thanks__people li { display: flex; align-items: center; gap: 12px; }
.weekly-thanks__avatar { display: grid; place-items: center; flex: 0 0 44px; width: 44px; height: 44px; overflow: hidden; border: 1px solid color-mix(in srgb, var(--weekly-amber) 30%, transparent); border-radius: 12px; background: var(--vp-c-bg); color: var(--weekly-amber); font: 600 14px var(--vp-font-family-mono); }
.weekly-thanks__avatar img { display: block; width: 100%; height: 100%; object-fit: cover; }
.weekly-thanks__identity { display: flex; flex-direction: column; align-items: flex-start; gap: 3px; min-width: 0; }
.weekly-thanks__people li + li { margin-top: 12px; padding-top: 12px; border-top: 1px solid var(--vp-c-divider); }
.weekly-thanks__people a { color: var(--vp-c-text-1); font-family: var(--vp-font-family-mono); font-size: 16px; font-weight: 700; line-height: 1.6; text-decoration: none; overflow-wrap: anywhere; }
.weekly-thanks__people a span[aria-hidden] { color: var(--weekly-amber); font-size: 13px; }
.weekly-thanks__people a:hover { color: var(--weekly-amber); text-decoration: underline; text-underline-offset: 4px; }
.weekly-thanks__role { color: var(--vp-c-text-2); font-size: 12px; line-height: 1.8; }
.weekly-thanks__sr { position: absolute; width: 1px; height: 1px; padding: 0; overflow: hidden; clip-path: inset(50%); white-space: nowrap; }
</style>
