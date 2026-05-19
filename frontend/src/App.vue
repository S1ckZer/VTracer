<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useBridge } from './composables/useBridge';
import TitleBar from './components/TitleBar.vue';
import OrderInputs from './components/OrderInputs.vue';
import GlassesWireframe from './components/GlassesWireframe.vue';
import TraceButtons from './components/TraceButtons.vue';
import ActivityLog from './components/ActivityLog.vue';
import RecentSaves from './components/RecentSaves.vue';
import SnifferPanel from './components/SnifferPanel.vue';
import type { Protocol, TraceSide } from './types';

const {
  ports, log, tracing, tracingSide, catalog, lookup, lastResult, recentSaves,
  wireLog, lastRaw, monitorRunning,
  startTrace, reloadCsv, listPorts, lookupModel, saveResult,
  startMonitor, stopMonitor, clearWireLog,
  winDrag, winMinimize, winMaximize, winClose,
} = useBridge();

const selectedPort = ref('');
const model = ref('');
const batch = ref('');
const note  = ref('');

// localStorage may throw inside a WebView2 NavigateToString document (no real
// origin); wrap so a failure here doesn't take the whole app down.
const TOL_KEY = 'vtracer.tolerance';
const SIM_KEY = 'vtracer.sim';
const PROTO_KEY = 'vtracer.protocol';
function lsGet(k: string): string | null { try { return localStorage.getItem(k); } catch { return null; } }
function lsSet(k: string, v: string)     { try { localStorage.setItem(k, v); } catch { /* ignore */ } }
const tolerance = ref<number>(parseFloat(lsGet(TOL_KEY) ?? '') || 0.50);
const simulate  = ref<boolean>(lsGet(SIM_KEY) === '1');
const protocol  = ref<Protocol>((lsGet(PROTO_KEY) as Protocol) || 'nidek');
watch(tolerance, (v) => lsSet(TOL_KEY, v.toFixed(2)));
watch(simulate,  (v) => lsSet(SIM_KEY, v ? '1' : '0'));
watch(protocol,  (v) => lsSet(PROTO_KEY, v));

watch(ports, (ps) => {
  if (!selectedPort.value && ps.length > 0) selectedPort.value = ps[0];
}, { immediate: true });

const measurement = computed(() => lastResult.value?.measurement ?? null);

const canSave = computed(() => {
  return !!measurement.value
    && !!model.value.trim()
    && !!lastResult.value?.ok;
});

function onTrace(side: TraceSide) {
  if (!simulate.value && !selectedPort.value) return;
  startTrace(selectedPort.value, side, simulate.value, protocol.value);
}

function onSave() {
  const m = measurement.value;
  if (!m) return;
  const exp = lookup.value?.expected;
  saveResult({
    brand: lookup.value?.brand ?? '',
    model: model.value.trim(),
    batch: batch.value.trim(),
    note:  note.value,
    tolerance: tolerance.value,
    aL: m.aL, bL: m.bL, aR: m.aR, bR: m.bR,
    expectedAL: exp?.aL ?? null,
    expectedBL: exp?.bL ?? null,
    expectedAR: exp?.aR ?? null,
    expectedBR: exp?.bR ?? null,
  });
}

function onClear() { lastResult.value = null; }
</script>

<template>
  <div class="flex h-full flex-col overflow-hidden">
    <TitleBar
      :catalog="catalog"
      :com-port="simulate ? 'SIM' : selectedPort"
      @drag="winDrag" @minimize="winMinimize" @maximize="winMaximize" @close="winClose"
    />

    <main class="grid flex-1 gap-4 overflow-hidden p-4 lg:grid-cols-[1fr_360px]">
      <section class="flex min-h-0 flex-col gap-4">
        <OrderInputs
          :lookup="lookup"
          :ports="ports"
          v-model:selected-port="selectedPort"
          v-model:tolerance="tolerance"
          @lookup="lookupModel"
          @reload-csv="reloadCsv"
          @refresh-ports="listPorts"
          @model-changed="(v: string) => model = v"
          @batch-changed="(v: string) => batch = v"
          @note-changed="(v: string) => note = v"
        />
        <GlassesWireframe
          :measurement="measurement"
          :expected="lookup"
          :tracing="tracing"
          :tolerance="tolerance"
        />
        <div class="grid min-h-0 flex-1 gap-4 md:grid-cols-2">
          <ActivityLog :entries="log" class="min-h-0" />
          <SnifferPanel
            :port="selectedPort"
            v-model:protocol="protocol"
            :wire-log="wireLog"
            :last-raw="lastRaw"
            :monitor-running="monitorRunning"
            @start-monitor="() => startMonitor(selectedPort)"
            @stop-monitor="stopMonitor"
            @clear-wire="clearWireLog"
            class="min-h-0"
          />
        </div>
      </section>

      <section class="flex min-h-0 flex-col gap-4">
        <TraceButtons
          :port="selectedPort"
          :tracing="tracing"
          :tracing-side="tracingSide"
          :has-result="!!lastResult"
          :can-save="canSave"
          v-model:simulate="simulate"
          @trace="onTrace"
          @save="onSave"
          @clear="onClear"
        />
        <RecentSaves :saves="recentSaves" class="min-h-0 flex-1" />
      </section>
    </main>
  </div>
</template>
