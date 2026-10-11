<script setup lang="ts">
/**
 * 路线图页「交互路径图」。
 *
 * 由 HomePathGraph 渲染 SVG 交互图，数据来自 home-path-data.ts。
 * 首页「完整路线图」入口落到本页，交互图集中在这里展示。
 * 页面宽度由 roadmap.css 统一控制，图表跟随正文列，避免向两侧扩张时盖住右侧大纲。
 * 窄屏靠图内缩放兜底。
 */
import HomePathGraph from './HomePathGraph.vue'
</script>

<template>
  <div class="rj-wrap">
    <HomePathGraph />
  </div>
</template>

<style scoped>
.rj-wrap {
  margin: 8px 0 20px;
}
</style>
