<script setup lang="ts">
import { ref, watch } from 'vue'

const props = withDefaults(defineProps<{ github: string; name: string; size?: number }>(), { size: 36 })
const failed = ref(false)
watch(() => props.github, () => { failed.value = false })
</script>

<template>
  <span class="community-avatar" :style="{ width: `${size}px`, height: `${size}px` }">
    <span aria-hidden="true">{{ name.slice(0, 1).toUpperCase() }}</span>
    <img v-if="!failed" :src="`https://github.com/${github}.png?size=96`"
      alt="" :width="size" :height="size" loading="lazy" decoding="async" @error="failed = true" />
  </span>
</template>

<style scoped>
.community-avatar { position: relative; display: inline-grid; place-items: center; flex: 0 0 auto; overflow: hidden; border-radius: 50%; background: var(--vp-c-brand-soft); color: var(--vp-c-brand-1); font-size: 14px; font-weight: 600; }
.community-avatar img { position: absolute; inset: 0; width: 100%; height: 100%; object-fit: cover; }
</style>
