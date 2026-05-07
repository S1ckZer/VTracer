<script setup lang="ts">
import { ref, watch, nextTick } from 'vue';
import type { LogEntry } from '../types';

const props = defineProps<{ entries: LogEntry[] }>();

const scroller = ref<HTMLDivElement | null>(null);
watch(() => props.entries.length, async () => {
  await nextTick();
  if (scroller.value) scroller.value.scrollTop = scroller.value.scrollHeight;
});

const colorFor = (level: string) => ({
  info:  'text-slate-300',
  warn:  'text-amber-300',
  error: 'text-rose-300',
  debug: 'text-sky-300/70',
}[level] ?? 'text-slate-300');
</script>

<template>
  <article class="flex flex-col rounded-xl border border-slate-800/80 bg-slate-900/40 p-5 backdrop-blur">
    <h2 class="mb-3 text-xs font-semibold uppercase tracking-wider text-slate-400">Activity</h2>
    <div ref="scroller" class="scroll-thin flex-1 overflow-y-auto rounded-md bg-slate-950/60 p-3 font-mono text-xs ring-1 ring-slate-800">
      <p v-if="entries.length === 0" class="text-slate-500">no events yet</p>
      <p v-for="(e, i) in entries" :key="i" :class="['leading-5', colorFor(e.level)]">
        <span class="text-slate-500">{{ e.time }}</span>
        <span class="ml-2 text-slate-500">[{{ e.level }}]</span>
        <span class="ml-2">{{ e.message }}</span>
      </p>
    </div>
  </article>
</template>
