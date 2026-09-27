<script setup lang="ts">
import { nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import type { CodeDiagnostic } from '../utils/cpp-check'
import type { createCppEditor } from '../utils/cpp-editor'

const props = defineProps<{ modelValue: string; label: string; diagnostics: CodeDiagnostic[]; completion?: boolean }>()
const emit = defineEmits<{ 'update:modelValue': [value: string] }>()
const host = ref<HTMLElement | null>(null)
const fallback = ref<HTMLTextAreaElement | null>(null)
const ready = ref(false)
const failed = ref(false)
let disposed = false
let editor: ReturnType<typeof createCppEditor> | undefined
onMounted(async () => {
  try {
    const { createCppEditor } = await import('../utils/cpp-editor')
    if (disposed || !host.value) return
    editor = createCppEditor(host.value, props.modelValue, props.label, value => emit('update:modelValue', value))
    editor.setDiagnostics(props.diagnostics)
    // 创建时默认自动补全开;若异步加载窗口期用户已切到「关」,这里补上(只补关方向)
    if (props.completion === false) editor.setCompletion(false)
    const hadFocus = fallback.value?.matches(':focus') ?? false
    ready.value = true
    // 降级 textarea 若正持有焦点,无缝转焦到编辑器,避免替换瞬间按键落空
    if (hadFocus) { await nextTick(); editor?.focus() }
  } catch { failed.value = true }
})
watch(() => props.modelValue, value => editor?.setSource(value))
watch(() => props.diagnostics, value => editor?.setDiagnostics(value))
watch(() => props.completion, value => editor?.setCompletion(value ?? true))
onBeforeUnmount(() => { disposed = true; editor?.destroy() })
</script>

<template>
  <div class="quiz-problem__editor-wrap quiz-code-editor">
    <div ref="host" v-show="ready" class="quiz-code-editor__host"></div>
    <textarea v-if="!ready" ref="fallback" class="quiz-code-editor__fallback" :value="modelValue" :aria-label="label" spellcheck="false" @input="emit('update:modelValue', ($event.target as HTMLTextAreaElement).value)"></textarea>
    <span v-if="failed" class="quiz-code-editor__notice">高级编辑器未能加载，仍可在文本框中编辑。</span>
  </div>
</template>

<style scoped>
.quiz-code-editor__host { height: 100%; }
.quiz-code-editor__fallback { width: 100%; height: 100%; padding: 16px; resize: none; color: #d7e3f2; font: 14px/23px var(--vp-font-family-mono); background: transparent; white-space: pre; tab-size: 4; }
.quiz-code-editor__notice { position: absolute; bottom: 0; left: 0; padding: 4px 10px; color: #e1eaf5; background: #1c2e46; font-size: 12px; }
</style>
