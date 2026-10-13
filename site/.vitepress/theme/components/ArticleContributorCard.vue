<script setup lang="ts">
import type { ArticleContributions } from '../utils/article-contributors'

withDefaults(defineProps<{ data: ArticleContributions; variant?: 'meta' | 'full' }>(), { variant: 'full' })
</script>

<template>
  <div v-if="data.contributors.length && variant === 'meta'" class="card-contributors-meta" role="group" :aria-label="data.heading">
    <span class="card-meta-caption"><span class="card-meta-label">{{ data.metaLabel }}</span><span class="card-meta-count">{{ data.metaCount }}</span></span>
    <div class="card-meta-avatars">
    <a v-for="person in data.contributors" :key="person.github" class="card-avatar-link"
      :href="`#${person.anchor}`" :aria-label="person.tooltip">
      <span class="card-avatar-fallback" aria-hidden="true">{{ person.name.slice(0, 1).toUpperCase() }}</span>
      <img class="card-avatar" :src="person.avatar" alt="" width="28" height="28" loading="lazy" decoding="async" />
      <span class="card-avatar-tip" aria-hidden="true">{{ person.tooltip }}</span>
    </a>
    </div>
    <a class="card-meta-more" href="#card-contributors-heading">{{ data.signatureLink }} <span aria-hidden="true">↓</span></a>
  </div>
  <section v-else-if="data.contributors.length" class="card-article-contributors" aria-labelledby="card-contributors-heading">
    <h2 id="card-contributors-heading" class="card-section-title">{{ data.heading }}</h2>
    <ul class="card-people">
      <li v-for="person in data.contributors" :id="person.anchor" :key="person.github" class="card-person" tabindex="-1">
        <a class="card-avatar-link" :href="person.profile" target="_blank" rel="noopener noreferrer"
          :aria-label="`${person.name} · ${data.profileLabel}`">
          <span class="card-avatar-fallback" aria-hidden="true">{{ person.name.slice(0, 1).toUpperCase() }}</span>
          <img class="card-avatar" :src="person.avatar" alt="" width="32" height="32" loading="lazy" decoding="async" />
          <span class="card-avatar-tip" aria-hidden="true">{{ person.tooltip }}</span>
        </a>
        <div class="card-person-body">
          <div class="card-person-meta">
            <a class="card-name" :href="person.profile" target="_blank" rel="noopener noreferrer">{{ person.name }}</a>
            <span class="card-roles"><span v-for="role in person.roles" :key="role" class="card-role">{{ role }}</span></span>
          </div>
          <p v-if="person.note || person.prUrl" class="card-note">{{ person.note }}
            <a v-if="person.prUrl" :href="person.prUrl" target="_blank" rel="noopener noreferrer">PR #{{ person.pr }}</a>
          </p>
        </div>
      </li>
    </ul>
    <details v-if="data.history.length" class="card-history">
      <summary class="card-history-summary">{{ data.historyHeading }} <span class="card-history-count">{{ data.history.length }}</span></summary>
      <p class="card-history-hint">{{ data.historyHint }}</p>
      <ol class="card-history-list">
        <li v-for="(entry, index) in data.history" :key="index" class="card-history-entry">
          <div class="card-history-meta">
            <time v-if="entry.date" :datetime="entry.date">{{ entry.date }}</time>
            <span>{{ entry.names }}</span>
            <a v-if="entry.url" :href="entry.url" target="_blank" rel="noopener noreferrer">PR #{{ entry.pr }}</a>
            <a v-if="entry.commitUrl" :href="entry.commitUrl" target="_blank" rel="noopener noreferrer">{{ entry.commit }}</a>
          </div>
          <p class="card-note">{{ entry.note }}</p>
        </li>
      </ol>
    </details>
    <p v-else class="card-history-hint">{{ data.historyEmpty }}</p>
  </section>
</template>
