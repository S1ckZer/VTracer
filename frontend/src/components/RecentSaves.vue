<script setup lang="ts">
import type { RecentSave } from '../types';

defineProps<{ saves: RecentSave[] }>();

const badge = (p: 'PASS' | 'FAIL' | '-') =>
  p === 'PASS' ? 'bg-emerald-500/15 text-emerald-300 ring-emerald-500/40'
  : p === 'FAIL' ? 'bg-rose-500/15 text-rose-300 ring-rose-500/40'
  : 'bg-slate-800 text-slate-400 ring-slate-700';
</script>

<template>
  <article class="flex h-full flex-col rounded-xl border border-slate-800/80 bg-slate-900/40 p-5 backdrop-blur">
    <h2 class="mb-3 text-xs font-semibold uppercase tracking-wider text-slate-400">Letzte Speicherungen</h2>
    <div v-if="saves.length === 0" class="flex flex-1 items-center justify-center text-xs text-slate-500">
      noch nichts gespeichert
    </div>
    <ul v-else class="scroll-thin flex-1 space-y-1.5 overflow-y-auto pr-1">
      <li v-for="(s, i) in saves" :key="i"
          class="flex items-center justify-between rounded-md bg-slate-950/50 px-3 py-2 text-xs ring-1 ring-slate-800">
        <div class="flex items-baseline gap-2 min-w-0">
          <span class="text-slate-500">{{ s.time }}</span>
          <span class="font-mono text-slate-100">{{ s.model }}</span>
          <span class="rounded bg-slate-800 px-1.5 py-0.5 font-mono text-slate-300">{{ s.batch || '-' }}</span>
        </div>
        <div class="flex gap-1.5">
          <span class="rounded px-2 py-0.5 ring-1" :class="badge(s.passL)">L {{ s.passL }}</span>
          <span class="rounded px-2 py-0.5 ring-1" :class="badge(s.passR)">R {{ s.passR }}</span>
        </div>
      </li>
    </ul>
  </article>
</template>
