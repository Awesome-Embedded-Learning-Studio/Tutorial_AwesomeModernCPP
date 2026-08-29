<script setup lang="ts">
import { computed, ref } from 'vue'
import { useData, withBase } from 'vitepress'

const QQ_GROUP_NUMBER = '1107100989'
const QQ_GROUP_LINK = 'https://qm.qq.com/q/cD89HxtmUg'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))

// public 目录下的静态矢量二维码,内容就是 QQ_GROUP_LINK,永不过期
const qrSrc = withBase('/qq-group.svg')

const copied = ref(false)
let resetTimer: ReturnType<typeof setTimeout> | undefined

async function copyNumber() {
  try {
    await navigator.clipboard.writeText(QQ_GROUP_NUMBER)
  } catch {
    // http 环境没有 clipboard API,退回 execCommand 老办法
    const ta = document.createElement('textarea')
    ta.value = QQ_GROUP_NUMBER
    document.body.appendChild(ta)
    ta.select()
    document.execCommand('copy')
    document.body.removeChild(ta)
  }
  copied.value = true
  clearTimeout(resetTimer)
  resetTimer = setTimeout(() => {
    copied.value = false
  }, 2000)
}
</script>

<template>
  <div class="qq-group-card">
    <img
      class="qq-group-qr"
      :src="qrSrc"
      :alt="isEn ? 'QQ group QR code' : 'QQ 群二维码'"
      width="140"
      height="140"
    />
    <div class="qq-group-info">
      <p class="qq-group-name">
        <svg class="qq-penguin" viewBox="0 0 24 24" aria-hidden="true">
          <path
            fill="currentColor"
            d="M21.395 15.035a40 40 0 0 0-.803-2.264l-1.079-2.695c.001-.032.014-.562.014-.836C19.526 4.632 17.351 0 12 0S4.474 4.632 4.474 9.241c0 .274.013.804.014.836l-1.08 2.695a39 39 0 0 0-.802 2.264c-1.021 3.283-.69 4.643-.438 4.673.54.065 2.103-2.472 2.103-2.472c0 1.469.756 3.387 2.394 4.771c-.612.188-1.363.479-1.845.835c-.434.32-.379.646-.301.778c.343.578 5.883.369 7.482.189c1.6.18 7.14.389 7.483-.189c.078-.132.132-.458-.301-.778c-.483-.356-1.233-.646-1.846-.836c1.637-1.384 2.393-3.302 2.393-4.771c0 0 1.563 2.537 2.103 2.472c.251-.03.581-1.39-.438-4.673"
          />
        </svg>
        <template v-if="isEn">TAMCPP Chat Group</template>
        <template v-else>TAMCPP 交流群</template>
      </p>

      <p class="qq-group-id">
        <span class="id-label">{{ isEn ? 'Group ID' : '群号' }}</span>
        <span class="id-value">{{ QQ_GROUP_NUMBER }}</span>
        <button class="copy-btn" type="button" @click="copyNumber">
          {{ copied ? (isEn ? 'Copied' : '已复制') : isEn ? 'Copy' : '复制' }}
        </button>
      </p>

      <div class="qq-group-actions">
        <a class="join-link" :href="QQ_GROUP_LINK" target="_blank" rel="noopener noreferrer">
          {{ isEn ? 'Join the Group' : '一键加群' }}
        </a>
      </div>

      <p class="qq-group-hint">
        <template v-if="isEn">Scan with the QQ app, or search the group ID in QQ. Chinese-language chat.</template>
        <template v-else>手机 QQ 扫码，或在 QQ 里搜索群号。</template>
      </p>
    </div>
  </div>
</template>

<style scoped>
.qq-group-card {
  display: flex;
  align-items: center;
  gap: 24px;
  max-width: 560px;
  padding: 20px 24px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 12px;
  background-color: var(--vp-c-bg);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.04), 0 1px 2px rgba(0, 0, 0, 0.06);
}

.qq-group-qr {
  flex-shrink: 0;
  width: 140px;
  height: 140px;
  /* 二维码必须在白底上,暗色模式也不能反色,否则扫不出来 */
  background-color: #fff;
  border: 1px solid var(--vp-c-divider);
  border-radius: 8px;
  padding: 6px;
}

.qq-group-info {
  display: flex;
  flex-direction: column;
  gap: 10px;
  min-width: 0;
}

.qq-group-name {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--vp-c-text-1);
}

.qq-penguin {
  width: 18px;
  height: 18px;
  color: var(--vp-c-brand-1);
}

.qq-group-id {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 0;
  font-size: 14px;
}

.id-label {
  color: var(--vp-c-text-3);
}

.id-value {
  font-variant-numeric: tabular-nums;
  letter-spacing: 0.02em;
  color: var(--vp-c-text-1);
  font-weight: 500;
}

.copy-btn {
  padding: 2px 10px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  background-color: var(--vp-c-bg);
  color: var(--vp-c-text-2);
  font-size: 12px;
  cursor: pointer;
  transition: border-color 0.25s ease, color 0.25s ease, background-color 0.25s ease;
}

.copy-btn:hover {
  border-color: var(--vp-c-brand-1);
  color: var(--vp-c-brand-1);
  background-color: var(--vp-c-brand-soft);
}

.qq-group-actions {
  display: flex;
}

.join-link {
  display: inline-flex;
  align-items: center;
  padding: 6px 16px;
  border-radius: 8px;
  border: 1px solid var(--vp-c-brand-1);
  background-color: var(--vp-c-brand-1);
  color: #fff;
  font-size: 14px;
  font-weight: 600;
  text-decoration: none !important;
  transition: background-color 0.25s ease, border-color 0.25s ease;
}

.join-link:hover {
  border-color: var(--vp-c-brand-2);
  background-color: var(--vp-c-brand-2);
  color: #fff;
}

.qq-group-hint {
  margin: 0;
  font-size: 12px;
  line-height: 1.6;
  color: var(--vp-c-text-3);
}

/* ── Dark Mode ───────────────────────────── */

.dark .qq-group-card {
  background-color: var(--vp-c-bg-elv);
  border-color: var(--vp-c-border);
}

.dark .qq-group-qr {
  border-color: var(--vp-c-border);
}

/* ── Responsive ──────────────────────────── */

@media (max-width: 519px) {
  .qq-group-card {
    flex-direction: column;
    align-items: stretch;
    gap: 16px;
    padding: 16px;
  }

  .qq-group-qr {
    align-self: center;
  }

  .qq-group-actions {
    justify-content: center;
  }
}
</style>
