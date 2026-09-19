<script setup lang="ts">
import { onBeforeUnmount, ref } from 'vue'
import { exportQuizData, importQuizData } from '../composables/useQuizProgress'

// 进度跟浏览器走:换设备/清缓存前导出一份 JSON,到新设备再导回来。
// 无账号无云端,这是唯一的搬运手段。
const fileInput = ref<HTMLInputElement | null>(null)
const note = ref('')
let noteTimer: ReturnType<typeof setTimeout> | null = null

function flash(message: string): void {
  note.value = message
  if (noteTimer) clearTimeout(noteTimer)
  noteTimer = setTimeout(() => (note.value = ''), 8000)
}

function exportBackup(): void {
  try {
    const blob = new Blob([exportQuizData()], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `weekly-problems-backup-${new Date().toISOString().slice(0, 10)}.json`
    a.click()
    URL.revokeObjectURL(url)
    flash('备份文件已开始下载,收好它;到新设备回到这里导入就能接上。')
  } catch {
    flash('导出失败:当前浏览器不允许下载文件。')
  }
}

async function onImportFile(event: Event): Promise<void> {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  input.value = ''
  if (!file) return
  try {
    const result = importQuizData(await file.text())
    if (!result) {
      flash('导入失败:这不是「每周一些题」的备份文件。')
      return
    }
    flash(`已导入:合并 ${result.progress} 题进度、${result.drafts} 题草稿(同一题两边都动过时,保留更新时间晚的一方)。`)
  } catch {
    flash('导入失败:文件读不出来。')
  }
}

onBeforeUnmount(() => {
  if (noteTimer) clearTimeout(noteTimer)
})
</script>

<template>
  <div class="weekly-backup">
    <span class="weekly-backup__hint">进度与草稿只存在当前浏览器——换设备或清缓存会丢,走之前导出一份备份,到了新地方再导入接上。</span>
    <span class="weekly-backup__actions">
      <button type="button" class="weekly-text-link" @click="exportBackup">导出备份 ↓</button>
      <button type="button" class="weekly-text-link" @click="fileInput?.click()">导入备份 ↑</button>
      <input ref="fileInput" type="file" accept="application/json,.json" class="weekly-backup__file" @change="onImportFile" />
    </span>
    <span v-if="note" class="weekly-backup__note" role="status">{{ note }}</span>
  </div>
</template>
