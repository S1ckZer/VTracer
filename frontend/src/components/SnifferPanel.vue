<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue';
import type { Protocol, TraceRaw, WireEvent } from '../types';

const props = defineProps<{
  port: string;
  protocol: Protocol;
  wireLog: WireEvent[];
  lastRaw: TraceRaw | null;
  monitorRunning: boolean;
}>();

const emit = defineEmits<{
  'update:protocol': [v: Protocol];
  startMonitor: [];
  stopMonitor: [];
  clearWire: [];
}>();

const autoScroll = ref(true);
const scroller = ref<HTMLDivElement | null>(null);

watch(() => props.wireLog.length, async () => {
  if (!autoScroll.value) return;
  await nextTick();
  const el = scroller.value;
  if (el) el.scrollTop = el.scrollHeight;
});

const totals = computed(() => {
  let tx = 0, rx = 0;
  for (const e of props.wireLog) {
    if (e.dir === 'tx') tx += e.n; else rx += e.n;
  }
  return { tx, rx };
});

async function copyRaw() {
  if (!props.lastRaw) return;
  try { await navigator.clipboard.writeText(props.lastRaw.hex); }
  catch { /* WebView2 may block; user can still select manually */ }
}

async function copyWireLog() {
  const text = props.wireLog
    .map(e => `${e.time}  ${e.dir.toUpperCase()}  ${e.hex}`)
    .join('\n');
  try { await navigator.clipboard.writeText(text); } catch { /* ignore */ }
}
</script>

<template>
  <article class="flex min-h-0 flex-col rounded-xl border border-slate-800/80 bg-slate-900/40 p-5 backdrop-blur">
    <div class="mb-3 flex items-center justify-between gap-2">
      <h2 class="text-xs font-semibold uppercase tracking-wider text-slate-400">Wire sniffer</h2>
      <div class="flex items-center gap-2 text-[10px] text-slate-500">
        <span>TX {{ totals.tx }}</span>
        <span>·</span>
        <span>RX {{ totals.rx }}</span>
      </div>
    </div>

    <div class="mb-3 flex items-center gap-2">
      <label class="text-[11px] uppercase tracking-wider text-slate-500">Protocol</label>
      <select
        :value="protocol"
        @change="emit('update:protocol', ($event.target as HTMLSelectElement).value as Protocol)"
        class="rounded-md border border-slate-700 bg-slate-900/60 px-2 py-1 text-xs text-slate-200"
      >
        <option value="nidek">Nidek native (LT-980)</option>
        <option value="vca">VCA / OMA</option>
      </select>
    </div>

    <div class="mb-3 grid grid-cols-2 gap-2">
      <button
        v-if="!monitorRunning"
        class="rounded-md border border-emerald-500/40 bg-emerald-500/10 py-1.5 text-xs font-semibold text-emerald-300 hover:bg-emerald-500/20 disabled:cursor-not-allowed disabled:opacity-40"
        :disabled="!port"
        @click="emit('startMonitor')"
      >
        Start sniffer
      </button>
      <button
        v-else
        class="rounded-md border border-amber-500/40 bg-amber-500/10 py-1.5 text-xs font-semibold text-amber-300 hover:bg-amber-500/20"
        @click="emit('stopMonitor')"
      >
        Stop sniffer
      </button>
      <button
        class="rounded-md border border-slate-700 bg-slate-800/40 py-1.5 text-xs text-slate-200 hover:bg-slate-700/60 disabled:cursor-not-allowed disabled:opacity-40"
        :disabled="wireLog.length === 0 && !lastRaw"
        @click="emit('clearWire')"
      >
        Clear
      </button>
    </div>

    <div
      ref="scroller"
      class="min-h-0 flex-1 overflow-auto rounded-md border border-slate-800 bg-slate-950/60 p-2 font-mono text-[11px] leading-tight"
    >
      <p v-if="wireLog.length === 0" class="text-slate-500">
        No wire traffic yet. Start the sniffer or run a trace.
      </p>
      <div v-for="(e, i) in wireLog" :key="i" class="flex gap-2 whitespace-nowrap">
        <span class="text-slate-500">{{ e.time.slice(0, 12) }}</span>
        <span
          :class="e.dir === 'tx' ? 'text-amber-300' : 'text-emerald-300'"
          class="w-6"
        >{{ e.dir === 'tx' ? 'PC' : 'LT' }}</span>
        <span class="text-slate-200">{{ e.hex }}</span>
      </div>
    </div>

    <label class="mt-2 flex cursor-pointer items-center gap-2 text-[11px] text-slate-400">
      <input type="checkbox" v-model="autoScroll" class="h-3 w-3 accent-blue-400" />
      Auto-scroll
    </label>

    <div v-if="lastRaw" class="mt-3 rounded-md border border-slate-800 bg-slate-950/40 p-2 text-[11px]">
      <div class="mb-1 flex items-center justify-between">
        <span class="font-semibold text-slate-300">
          Last trace payload · {{ lastRaw.bytes }} bytes · {{ lastRaw.protocol }}
        </span>
        <button
          class="rounded border border-slate-700 px-2 py-0.5 text-[10px] text-slate-300 hover:bg-slate-700/60"
          @click="copyRaw"
        >Copy hex</button>
      </div>
      <pre class="max-h-24 overflow-auto whitespace-pre-wrap break-all font-mono text-[10px] text-slate-300">{{ lastRaw.hex }}</pre>
    </div>

    <button
      v-if="wireLog.length > 0"
      class="mt-2 self-start rounded border border-slate-700 px-2 py-0.5 text-[10px] text-slate-300 hover:bg-slate-700/60"
      @click="copyWireLog"
    >Copy wire log</button>
  </article>
</template>
