<script setup lang="ts">
defineProps<{ issue: string }>()
</script>

<template>
  <div class="weekly-art" aria-hidden="true">
    <div class="weekly-art__caption"><span>THE WEEKLY PRACTICE</span><span>↗</span></div>
    <span class="weekly-art__number">{{ issue }}</span>
    <svg class="weekly-art__diagram" viewBox="0 0 420 260" fill="none">
      <g class="weekly-art__paths" stroke="currentColor" stroke-width="1.5">
        <path d="M210 45V85M210 85H108V139M210 85H312V139M108 139V174H52V218M108 174H164V218M312 139V174H256V218M312 174H368V218" />
        <path d="M52 237H368" stroke-dasharray="3 7" opacity=".4" />
      </g>
      <g fill="#14243a" stroke="#7894b5" stroke-width="1.5">
        <rect x="176" y="18" width="68" height="40" rx="8" />
        <rect x="81" y="115" width="54" height="40" rx="8" />
        <rect x="285" y="115" width="54" height="40" rx="8" />
        <circle cx="52" cy="224" r="7" /><circle cx="164" cy="224" r="7" />
        <circle cx="256" cy="224" r="7" /><circle cx="368" cy="224" r="7" />
      </g>
      <g stroke="#b6c9df" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
        <path d="m198 32-6 6 6 6m24-12 6 6-6 6m-10-16-5 20" />
        <path d="m101 129-6 6 6 6m14-12 6 6-6 6m190-12-6 6 6 6m14-12 6 6-6 6" />
      </g>
      <circle class="weekly-art__spark" cx="164" cy="224" r="7" fill="#f4b75d" stroke="#f4b75d" />
    </svg>
    <div class="weekly-art__footer"><span>THINK. WRITE. ITERATE.</span><span>● C++</span></div>
  </div>
</template>
