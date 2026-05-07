<script setup lang="ts">
import type { TraceSide } from '../types';

const props = defineProps<{
  port: string;
  tracing: boolean;
  tracingSide: TraceSide | '';
  hasResult: boolean;
  canSave: boolean;
  simulate: boolean;
}>();

const emit = defineEmits<{
  trace: [side: TraceSide];
  save: [];
  clear: [];
  'update:simulate': [v: boolean];
}>();

function disabled() {
  return props.tracing || (!props.simulate && !props.port);
}
</script>

<template>
  <article class="rounded-xl border border-slate-800/80 bg-slate-900/40 p-5 backdrop-blur">
    <div class="mb-3 flex items-center justify-between">
      <h2 class="text-xs font-semibold uppercase tracking-wider text-slate-400">LT-980 control</h2>
      <label class="flex cursor-pointer items-center gap-2 text-xs text-slate-300">
        <input
          type="checkbox"
          :checked="simulate"
          @change="emit('update:simulate', ($event.target as HTMLInputElement).checked)"
          class="h-3.5 w-3.5 accent-amber-400"
        />
        <span :class="simulate ? 'text-amber-300' : 'text-slate-400'">Simulator</span>
      </label>
    </div>

    <div class="grid grid-cols-3 gap-3">
      <button
        v-for="side in (['left', 'center', 'right'] as const)"
        :key="side"
        class="group flex flex-col items-center gap-2 rounded-lg border border-slate-700 bg-slate-900/70 p-4 text-slate-200 transition hover:border-blue-500/60 hover:bg-blue-500/5 disabled:cursor-not-allowed disabled:opacity-40"
        :class="tracingSide === side ? 'ring-2 ring-amber-400/70' : ''"
        :disabled="disabled()"
        @click="emit('trace', side)"
      >
        <span class="relative flex h-14 w-14 items-center justify-center rounded-full bg-slate-800 ring-1 ring-slate-600 group-hover:ring-blue-400">
          <span v-if="side === 'left'"  class="text-xl">&#x25D0;</span>
          <span v-else-if="side === 'right'" class="text-xl">&#x25D1;</span>
          <span v-else class="text-xl">&#x25CF;</span>
          <span v-if="tracingSide === side" class="absolute inset-0 rounded-full ring-2 ring-amber-400 nt-pulse" />
        </span>
        <span class="text-xs font-semibold uppercase tracking-wider">
          {{ side === 'left' ? 'Links' : side === 'right' ? 'Rechts' : 'Mitte' }}
        </span>
        <span class="text-[10px] text-slate-500">REQ={{ side === 'left' ? 'TRCL' : side === 'right' ? 'TRCR' : 'TRC' }}</span>
      </button>
    </div>

    <button
      class="mt-4 w-full rounded-lg bg-blue-600 px-4 py-3 text-sm font-semibold text-white shadow-lg shadow-blue-900/40 transition hover:bg-blue-500 disabled:cursor-not-allowed disabled:bg-slate-700 disabled:text-slate-400 disabled:shadow-none"
      :disabled="disabled()"
      @click="emit('trace', 'both')"
    >
      <span v-if="!tracing" class="flex items-center justify-center gap-2">
        <span class="inline-block h-2 w-2 rounded-full bg-emerald-300" />
        Beide Gläser tracen
      </span>
      <span v-else class="flex items-center justify-center gap-2">
        <span class="inline-block h-2 w-2 rounded-full bg-amber-300 nt-pulse" />
        Tracing {{ tracingSide || '...' }}
      </span>
    </button>

    <div class="mt-4 grid grid-cols-2 gap-3">
      <button
        class="rounded-md border border-emerald-500/40 bg-emerald-500/10 py-2 text-sm font-semibold text-emerald-300 transition hover:bg-emerald-500/20 disabled:cursor-not-allowed disabled:opacity-40"
        :disabled="!canSave"
        @click="emit('save')"
      >
        Speichern
      </button>
      <button
        class="rounded-md border border-slate-700 bg-slate-800/40 py-2 text-sm text-slate-200 hover:bg-slate-700/60 disabled:cursor-not-allowed disabled:opacity-40"
        :disabled="!hasResult"
        @click="emit('clear')"
      >
        Zurücksetzen
      </button>
    </div>

    <p v-if="!simulate && !port" class="mt-3 text-xs text-amber-300">Wähle erst einen COM-Port (oder aktiviere Simulator).</p>
    <p v-else-if="simulate" class="mt-3 text-xs text-amber-300">Simulator aktiv - kein echtes Gerät wird angesprochen.</p>
  </article>
</template>
