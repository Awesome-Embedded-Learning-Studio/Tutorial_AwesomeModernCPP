<script setup lang="ts">
// 教学动画嵌入组件:markdown 里 <Anim id="opp1-vector-growth" /> 一行接入。
// id = animations/data/<id>.json 文件名(= animy_maker DSL 文件名)。
// 播放器与数据由 animation_maker 编译产出(-o 直达本目录),勿手改;
// 新增动画只需重新编译,本文件零改动。
// 注:animations/README.md 是编译器随产物写的通用模板,其「接入 VitePress」章节
// 不适用本仓库——本仓库统一走本包装组件 <Anim id>,数据自动收集,页面零 import。
import { computed } from 'vue'
import AnimPlayer from './animations/AnimPlayer.vue'

const modules = import.meta.glob('./animations/data/*.json', { eager: true })
const registry = Object.fromEntries(
  Object.entries(modules).map(([p, m]) => [p.match(/([\w-]+)\.json$/)![1], (m as any).default])
)

const props = defineProps<{ id: string }>()
const data = computed(() => registry[props.id])
</script>

<template>
  <AnimPlayer v-if="data" :data="data" />
  <p v-else>动画 <code>{{ id }}</code> 未找到</p>
</template>
