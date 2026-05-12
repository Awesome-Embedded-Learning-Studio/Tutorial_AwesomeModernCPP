<script setup lang="ts">
import { provide } from 'vue'

const props = withDefaults(defineProps<{
  variant?: 'main' | 'sub'
}>(), {
  variant: 'main'
})

provide('chapterNavVariant', props.variant)
</script>

<template>
  <div class="chapter-nav" :class="[`chapter-nav--${variant}`]">
    <slot />
  </div>
</template>

<style scoped>
.chapter-nav {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 12px;
  margin: 1.5em 0;
}

.chapter-nav--sub {
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 10px;
  margin: 1.2em 0;
}

@media (max-width: 639px) {
  .chapter-nav,
  .chapter-nav--sub {
    grid-template-columns: 1fr;
  }
}
</style>
