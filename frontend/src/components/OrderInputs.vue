<script setup lang="ts">
import { ref, watch } from 'vue';
import type { ModelLookup } from '../types';

const props = defineProps<{
  lookup: ModelLookup | null;
  ports: string[];
  selectedPort: string;
  tolerance: number;
}>();

const emit = defineEmits<{
  'update:selectedPort': [port: string];
  'update:tolerance': [t: number];
  lookup: [model: string];
  reloadCsv: [];
  refreshPorts: [];
  modelChanged: [model: string];
  batchChanged: [batch: string];
  noteChanged: [note: string];
}>();

const model = ref('');
const batch = ref('');
const note  = ref('');

let lookupT: number | null = null;
watch(model, (m) => {
  emit('modelChanged', m);
  if (lookupT) clearTimeout(lookupT);
  if (m.trim().length >= 3) {
    lookupT = window.setTimeout(() => emit('lookup', m.trim()), 300);
  }
});
watch(batch, (b) => emit('batchChanged', b));
watch(note,  (n) => emit('noteChanged', n));

function onTolInput(e: Event) {
  const v = parseFloat((e.target as HTMLInputElement).value.replace(',', '.'));
  if (Number.isFinite(v) && v >= 0 && v <= 5) emit('update:tolerance', v);
}
</script>

<template>
  <section class="rounded-xl border border-slate-800/80 bg-slate-900/40 p-4 backdrop-blur">
    <h2 class="mb-3 text-xs font-semibold uppercase tracking-wider text-slate-400">Auftrag</h2>

    <div class="grid grid-cols-1 gap-3 lg:grid-cols-[1fr_1fr_1fr_110px_auto_auto]">
      <div>
        <label class="mb-1 block text-xs text-slate-400">Modell-Nr.</label>
        <input
          v-model="model"
          type="text" placeholder="z. B. 40103" autocomplete="off"
          class="w-full rounded-md border border-slate-700 bg-slate-950 px-3 py-2 font-mono text-sm text-slate-100 focus:border-blue-500 focus:outline-none"
        />
        <p class="mt-1 text-xs"
           :class="lookup ? (lookup.found ? 'text-emerald-300' : 'text-amber-300') : 'text-slate-500'"
        >
          <template v-if="!lookup">enter to lookup catalog</template>
          <template v-else-if="lookup.found">
            OK {{ lookup.brand }} . {{ lookup.expected?.aL.toFixed(2) }}x{{ lookup.expected?.bL.toFixed(2) }} L
            / {{ lookup.expected?.aR.toFixed(2) }}x{{ lookup.expected?.bR.toFixed(2) }} R
          </template>
          <template v-else>not in catalog</template>
        </p>
      </div>

      <div>
        <label class="mb-1 block text-xs text-slate-400">Charge / Batch</label>
        <input
          v-model="batch" type="text" placeholder="z. B. C39" autocomplete="off"
          class="w-full rounded-md border border-slate-700 bg-slate-950 px-3 py-2 font-mono text-sm text-slate-100 focus:border-blue-500 focus:outline-none"
        />
        <p class="mt-1 text-xs text-slate-500">in gespeichertem Datensatz</p>
      </div>

      <div>
        <label class="mb-1 block text-xs text-slate-400">Notiz (optional)</label>
        <input
          v-model="note" type="text" placeholder="freier Text" autocomplete="off"
          class="w-full rounded-md border border-slate-700 bg-slate-950 px-3 py-2 text-sm text-slate-100 focus:border-blue-500 focus:outline-none"
        />
      </div>

      <div>
        <label class="mb-1 block text-xs text-slate-400">Toleranz (mm)</label>
        <input
          :value="props.tolerance.toFixed(2)"
          type="number" step="0.05" min="0" max="5"
          @input="onTolInput"
          class="w-full rounded-md border border-slate-700 bg-slate-950 px-3 py-2 font-mono text-sm text-slate-100 focus:border-blue-500 focus:outline-none"
        />
        <p class="mt-1 text-xs text-slate-500">+/- pro Achse</p>
      </div>

      <div>
        <label class="mb-1 block text-xs text-slate-400">COM</label>
        <div class="flex gap-1">
          <select
            :value="props.selectedPort"
            @change="emit('update:selectedPort', ($event.target as HTMLSelectElement).value)"
            class="rounded-md border border-slate-700 bg-slate-950 px-2 py-2 text-sm text-slate-100 focus:border-blue-500 focus:outline-none"
          >
            <option v-if="!ports.length" value="">none</option>
            <option v-for="p in ports" :key="p" :value="p">{{ p }}</option>
          </select>
          <button
            class="rounded-md border border-slate-700 bg-slate-800/60 px-2 py-2 text-xs text-slate-200 hover:bg-slate-700/60"
            title="Re-scan COM ports" @click="emit('refreshPorts')"
          >&#x21bb;</button>
        </div>
      </div>

      <div class="flex items-end">
        <button
          class="rounded-md border border-slate-700 bg-slate-800/40 px-3 py-2 text-xs text-slate-200 hover:bg-slate-700/60"
          @click="emit('reloadCsv')"
        >Reload CSV</button>
      </div>
    </div>
  </section>
</template>
