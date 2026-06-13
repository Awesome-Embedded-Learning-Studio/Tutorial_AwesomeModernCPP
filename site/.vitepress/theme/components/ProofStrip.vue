<script setup lang="ts">
import { computed } from 'vue'
import { useData } from 'vitepress'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))

const items = computed(() =>
  isEn.value
    ? ['350+ articles', 'C++11–23', 'Compilable CMake examples', 'STM32 embedded', 'Bilingual']
    : ['350+ 篇', 'C++11–23', '可编译 CMake 示例', 'STM32 嵌入式实战', '中英双语'],
)
</script>

<template>
  <div class="proof-strip">
    <div class="proof-strip__inner">
      <span
        v-for="(it, i) in items"
        :key="i"
        class="proof-chip"
      >
        <span class="proof-chip__dot" />
        <span class="proof-chip__text">{{ it }}</span>
      </span>
    </div>
  </div>
</template>

<style scoped>
.proof-strip {
  max-width: 1152px;
  margin: 8px auto 40px;
  padding: 0 24px;
  animation: proof-fade-up 0.7s cubic-bezier(0.25, 0.46, 0.45, 0.94) 0.1s both;
}

.proof-strip__inner {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: center;
  gap: 10px 20px;
  padding: 12px 20px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 999px;
  background: var(--vp-c-bg-soft);
}

.proof-chip {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  color: var(--vp-c-text-2);
  font-size: 13px;
  font-weight: 500;
  line-height: 1;
  white-space: nowrap;
}

.proof-chip__dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: linear-gradient(135deg, var(--vp-c-brand-1), var(--vp-c-indigo-1));
  flex-shrink: 0;
}

@keyframes proof-fade-up {
  from { opacity: 0; transform: translateY(10px); }
  to   { opacity: 1; transform: translateY(0); }
}

@media (prefers-reduced-motion: reduce) {
  .proof-strip { animation: none !important; }
}

@media (max-width: 639px) {
  .proof-strip {
    padding: 0 16px;
    margin-bottom: 20px;
  }
  .proof-strip__inner {
    gap: 8px 14px;
    padding: 10px 14px;
    border-radius: 16px;
  }
  .proof-chip { font-size: 12px; }
}
</style>
