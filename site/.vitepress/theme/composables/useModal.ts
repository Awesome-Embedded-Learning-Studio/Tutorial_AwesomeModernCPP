import { onBeforeUnmount, ref } from 'vue'

// 模态浮层骨架:开关状态 + ESC 关闭 + body 滚动锁定 + 卸载清理。
// close 时先调 onClose 回调(回写状态),再摘遮罩/监听——与 OnlineCompilerDemo
// 原 closeModal 的顺序一致(closeEditor 先于 overflow 恢复)。
export function useModal(onClose?: () => void) {
  const modalOpen = ref(false)

  function onKeydown(e: KeyboardEvent): void {
    if (e.key === 'Escape' && modalOpen.value) close()
  }

  function open(): void {
    modalOpen.value = true
    document.body.style.overflow = 'hidden'
    window.addEventListener('keydown', onKeydown)
  }

  function close(): void {
    onClose?.()
    modalOpen.value = false
    document.body.style.overflow = ''
    window.removeEventListener('keydown', onKeydown)
  }

  onBeforeUnmount(() => {
    window.removeEventListener('keydown', onKeydown)
    if (modalOpen.value) document.body.style.overflow = ''
  })

  return { modalOpen, open, close }
}
