<script setup lang="ts">
import { computed } from 'vue'
import { useData, withBase } from 'vitepress'

const { lang } = useData()
const isEn = computed(() => lang.value.startsWith('en'))

// 与 mermaid-plugin 输出一致:把图源 URL 编码进 data-mermaid,
// 现有 mermaid-client 会在 onMounted 扫描并渲染成 SVG。
const enc = (s: string) => encodeURIComponent(s)

const t = computed(() =>
  isEn.value
    ? {
        title: 'Project Roadmap',
        done: 'Fundamentals & modern features',
        doing: 'Concurrency · Performance · Engineering',
        todo: 'Capstone projects',
        next: 'Next up: Vol.3 Standard Library rewrite, Vol.8 embedded expansion, open-source study.',
        pathTitle: 'Learning Paths',
        pathGraph: `flowchart TD
    Start(["Your starting point"])
    NewCPP["New to C/C++"] --> V1["Vol.1: Fundamentals<br/>Types · OOP · Templates"] --> V2["Vol.2: Modern Features<br/>Move · Smart pointers"]
    CEmbedded["C or embedded background"] --> V2 --> Embedded["Vol.8: Embedded<br/>STM32 practice"]
    CPP["Existing C++ experience"] --> Pick["Choose by goal"]
    Pick --> Concurrency["Vol.5: Concurrency"]
    Pick --> Performance["Vol.6: Performance"]
    Pick --> Engineering["Vol.7: Engineering"]
    Pick --> Source["Vol.9: Open Source Study"]
    V2 --> Reference["Reference cards anytime"]

    Start --> NewCPP
    Start --> CEmbedded
    Start --> CPP`,
        cta: 'View the full Roadmap',
        link: '/en/community/dev/',
      }
    : {
        title: '学习路线图',
        done: '基础与现代特性',
        doing: '并发 · 性能 · 工程 推进中',
        todo: '实战项目 规划中',
        next: '下一步重点:卷三标准库重写、卷八嵌入式展开、开源项目研读。',
        pathTitle: '学习路径',
        pathGraph: `flowchart TD
    Start(["你的起点"])
    NewCPP["C/C++ 零基础"] --> V1["卷一：基础<br/>类型 · OOP · 模板"] --> V2["卷二：现代特性<br/>移动 · 智能指针"]
    CEmbedded["有 C 或嵌入式经验"] --> V2 --> Embedded["卷八：嵌入式开发<br/>STM32 实战"]
    CPP["已有 C++ 经验"] --> Pick["按目标选择专题"]
    Pick --> Concurrency["卷五：并发"]
    Pick --> Performance["卷六：性能"]
    Pick --> Engineering["卷七：工程"]
    Pick --> Source["卷九：开源研读"]
    V2 --> Reference["C++ 参考卡随时查"]

    Start --> NewCPP
    Start --> CEmbedded
    Start --> CPP`,
        cta: '点击查看完整 Roadmap',
        link: '/roadmap/',
      },
)
</script>

<template>
  <section id="roadmap" class="home-roadmap">
    <div class="home-roadmap__card">
      <h2 class="home-roadmap__title">📍 {{ t.title }}</h2>

      <div class="home-roadmap__chips">
        <span class="rm-chip rm-chip--done">
          <span class="rm-chip__mark">✓</span>{{ t.done }}
        </span>
        <span class="rm-chip rm-chip--doing">
          <span class="rm-chip__mark">✦</span>{{ t.doing }}
        </span>
        <span class="rm-chip rm-chip--todo">
          <span class="rm-chip__mark">◇</span>{{ t.todo }}
        </span>
      </div>

      <p class="home-roadmap__next">{{ t.next }}</p>

      <hr class="home-roadmap__divider" />

      <div class="home-roadmap__diagram">
        <h3 class="home-roadmap__subtitle">🧭 {{ t.pathTitle }}</h3>
        <div
          class="mermaid-diagram"
          :data-mermaid="enc(t.pathGraph)"
          data-rendered="false"
        />
      </div>

      <a class="home-roadmap__cta" :href="withBase(t.link)">
        📋 {{ t.cta }} →
      </a>
    </div>
  </section>
</template>

<style scoped>
.home-roadmap {
  max-width: 1152px;
  margin: 40px auto 56px;
  padding: 0 24px;
  scroll-margin-top: 80px;
  animation: roadmap-fade-up 0.7s cubic-bezier(0.25, 0.46, 0.45, 0.94) both;
}

.home-roadmap__card {
  padding: 28px 32px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 14px;
  background-color: var(--vp-c-bg);
  box-shadow:
    0 1px 3px rgba(0, 0, 0, 0.04),
    0 1px 2px rgba(0, 0, 0, 0.06);
  text-align: center;
}

.dark .home-roadmap__card {
  background-color: var(--vp-c-bg-elv);
  border-color: var(--vp-c-border);
  box-shadow:
    0 1px 3px rgba(0, 0, 0, 0.2),
    0 1px 2px rgba(0, 0, 0, 0.15);
}

.home-roadmap__title {
  margin: 0 0 18px;
  font-size: 20px;
  font-weight: 700;
  line-height: 1.4;
  color: var(--vp-c-text-1);
  border-top: 0;
  padding-top: 0;
}

.home-roadmap__chips {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  gap: 12px;
  margin-bottom: 18px;
}

.rm-chip {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  border-radius: 999px;
  border: 1px solid var(--vp-c-divider);
  background: var(--vp-c-bg-soft);
  font-size: 14px;
  font-weight: 500;
  line-height: 1;
  color: var(--vp-c-text-1);
  white-space: nowrap;
}

.rm-chip__mark {
  font-size: 14px;
  font-weight: 700;
}

.rm-chip--done .rm-chip__mark { color: var(--vp-c-green-1); }
.rm-chip--doing .rm-chip__mark { color: #ffc107; }
.rm-chip--todo .rm-chip__mark { color: var(--vp-c-text-3); }

.home-roadmap__next {
  margin: 0 auto 0;
  max-width: 640px;
  font-size: 14px;
  line-height: 1.7;
  color: var(--vp-c-text-2);
}

/* separates project status from the content maps below */
.home-roadmap__divider {
  margin: 24px 0;
  border: 0;
  border-top: 1px dashed var(--vp-c-divider);
  opacity: 0.7;
}

.home-roadmap__diagram {
  margin-bottom: 28px;
  padding: 22px 20px 8px;
  border-radius: 12px;
  background: var(--vp-c-bg-soft);
}

.home-roadmap__diagram:last-of-type {
  margin-bottom: 24px;
}

.home-roadmap__subtitle {
  margin: 0 0 14px;
  font-size: 16px;
  font-weight: 600;
  line-height: 1.4;
  color: var(--vp-c-text-1);
}

.home-roadmap__card :deep(.mermaid-diagram) {
  margin: 0 auto;
}

.home-roadmap__cta {
  display: inline-block;
  padding: 10px 22px;
  border-radius: 8px;
  background: var(--vp-c-brand-1);
  color: var(--vp-c-white) !important;
  font-size: 14px;
  font-weight: 600;
  text-decoration: none !important;
  transition: background 0.2s ease, transform 0.2s ease;
}

.home-roadmap__cta:hover {
  background: var(--vp-c-brand-2);
  transform: translateY(-2px);
}

@keyframes roadmap-fade-up {
  from { opacity: 0; transform: translateY(18px); }
  to   { opacity: 1; transform: translateY(0); }
}

@media (prefers-reduced-motion: reduce) {
  .home-roadmap { animation: none !important; }
  .home-roadmap__cta { transition: none; }
}

@media (max-width: 639px) {
  .home-roadmap { padding: 0 16px; margin: 28px auto 36px; }
  .home-roadmap__card { padding: 22px 18px; }
  .home-roadmap__title { font-size: 18px; }
  .rm-chip { font-size: 13px; padding: 7px 13px; }
  .home-roadmap__subtitle { font-size: 15px; }
}
</style>
