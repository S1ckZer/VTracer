<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import type { Measurement, ModelLookup } from '../types';

const expanded = ref(false);
function onKey(e: KeyboardEvent) { if (e.key === 'Escape') expanded.value = false; }
onMounted(() => window.addEventListener('keydown', onKey));
onBeforeUnmount(() => window.removeEventListener('keydown', onKey));

const props = defineProps<{
  measurement: Measurement | null;
  expected: ModelLookup | null;
  tracing: boolean;
  tolerance: number;
}>();

interface LensInfo {
  a: number | null;
  b: number | null;
  expectedA: number | null;
  expectedB: number | null;
  passA: 'pass' | 'fail' | 'na';
  passB: 'pass' | 'fail' | 'na';
  pass: 'pass' | 'fail' | 'na';
}

function check(a: number | null, exp: number | null): 'pass' | 'fail' | 'na' {
  if (a == null || exp == null) return 'na';
  return Math.abs(a - exp) <= props.tolerance ? 'pass' : 'fail';
}

const lensL = computed<LensInfo>(() => {
  const m = props.measurement;
  const e = props.expected?.expected;
  const passA = check(m?.aL ?? null, e?.aL ?? null);
  const passB = check(m?.bL ?? null, e?.bL ?? null);
  return {
    a: m?.aL ?? null, b: m?.bL ?? null,
    expectedA: e?.aL ?? null, expectedB: e?.bL ?? null,
    passA, passB,
    pass: (passA === 'na' || passB === 'na') ? 'na'
        : (passA === 'pass' && passB === 'pass') ? 'pass' : 'fail',
  };
});
const lensR = computed<LensInfo>(() => {
  const m = props.measurement;
  const e = props.expected?.expected;
  const passA = check(m?.aR ?? null, e?.aR ?? null);
  const passB = check(m?.bR ?? null, e?.bR ?? null);
  return {
    a: m?.aR ?? null, b: m?.bR ?? null,
    expectedA: e?.aR ?? null, expectedB: e?.bR ?? null,
    passA, passB,
    pass: (passA === 'na' || passB === 'na') ? 'na'
        : (passA === 'pass' && passB === 'pass') ? 'pass' : 'fail',
  };
});

const svg = computed(() => {
  const L = lensL.value, R = lensR.value;
  const aL = L.a ?? L.expectedA ?? 50;
  const bL = L.b ?? L.expectedB ?? 40;
  const aR = R.a ?? R.expectedA ?? 50;
  const bR = R.b ?? R.expectedB ?? 40;
  const dbl = props.measurement?.dbl ?? 18;
  const pad = 5;
  const w = aL + dbl + aR + 2 * pad;
  const h = Math.max(bL, bR) + 2 * pad;
  return {
    w, h,
    L: { cx: pad + aL / 2,           cy: h / 2, rx: aL / 2, ry: bL / 2 },
    R: { cx: pad + aL + dbl + aR / 2, cy: h / 2, rx: aR / 2, ry: bR / 2 },
    bridge: { x1: pad + aL, y1: h / 2, x2: pad + aL + dbl, y2: h / 2 },
  };
});

// Map device-space polar->cartesian points (centered, +y up) to SVG coords
// (translated to lens center, +y down).  Empty string when no points.
function polygonAttr(side: 'L' | 'R'): string {
  const pts = side === 'L' ? props.measurement?.pointsL : props.measurement?.pointsR;
  if (!pts || pts.length < 8) return '';
  const c = side === 'L' ? svg.value.L : svg.value.R;
  return pts.map(p => `${(c.cx + p.x).toFixed(3)},${(c.cy - p.y).toFixed(3)}`).join(' ');
}

const polyL = computed(() => polygonAttr('L'));
const polyR = computed(() => polygonAttr('R'));

const fmt = (n: number | null) => n == null ? '-' : n.toFixed(2);
const colorFor = (p: 'pass' | 'fail' | 'na') => p === 'pass' ? '#10b981' : p === 'fail' ? '#ef4444' : '#475569';
const badgeBg  = (p: 'pass' | 'fail' | 'na') =>
  p === 'pass' ? 'bg-emerald-500/15 text-emerald-300 ring-emerald-500/40'
  : p === 'fail' ? 'bg-rose-500/15 text-rose-300 ring-rose-500/40'
  : 'bg-slate-800 text-slate-400 ring-slate-700';

function badgeText(p: 'pass' | 'fail' | 'na', delta: number | null | undefined) {
  if (p === 'na') return '-';
  if (p === 'pass') return `+/-${Math.abs(delta ?? 0).toFixed(2)} mm`;
  return `${(delta ?? 0) > 0 ? '+' : ''}${(delta ?? 0).toFixed(2)} mm`;
}
function delta(meas: number | null, expected: number | null): number | null {
  if (meas == null || expected == null) return null;
  return meas - expected;
}
</script>

<template>
  <article class="rounded-xl border border-slate-800/80 bg-slate-900/40 p-5 backdrop-blur">
    <div class="mb-3 flex items-center justify-between">
      <h2 class="text-xs font-semibold uppercase tracking-wider text-slate-400">Frame outline</h2>
      <div class="flex items-center gap-3">
        <p class="text-xs text-slate-500">+/- {{ props.tolerance.toFixed(2) }} mm tolerance</p>
        <button
          @click="expanded = true"
          title="Vergroessern"
          class="flex h-7 w-7 items-center justify-center rounded-md border border-slate-700 bg-slate-800/50 text-slate-300 transition hover:border-blue-500/60 hover:bg-blue-500/10 hover:text-blue-200"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="h-4 w-4">
            <circle cx="11" cy="11" r="6" />
            <line x1="21" y1="21" x2="15.5" y2="15.5" />
            <line x1="11" y1="8.5" x2="11" y2="13.5" />
            <line x1="8.5" y1="11" x2="13.5" y2="11" />
          </svg>
        </button>
      </div>
    </div>

    <!-- 3-column layout: L details · wireframe · R details -->
    <div class="grid grid-cols-[180px_1fr_180px] items-stretch gap-4">
      <!-- LEFT lens stats -->
      <div class="flex flex-col gap-2">
        <h3 class="text-xs font-semibold uppercase tracking-wider text-blue-300">Linkes Glas</h3>
        <div class="rounded-lg border border-slate-800 bg-slate-950/50 p-3">
          <div class="flex items-baseline justify-between text-xs text-slate-400">
            <span>Width (A)</span>
            <span class="rounded px-2 py-0.5 ring-1" :class="badgeBg(lensL.passA)">
              {{ badgeText(lensL.passA, delta(lensL.a, lensL.expectedA)) }}
            </span>
          </div>
          <div class="mt-1 font-mono text-lg text-slate-100">{{ fmt(lensL.a) }} mm</div>
          <div class="text-xs text-slate-500">target {{ fmt(lensL.expectedA) }}</div>
        </div>
        <div class="rounded-lg border border-slate-800 bg-slate-950/50 p-3">
          <div class="flex items-baseline justify-between text-xs text-slate-400">
            <span>Height (B)</span>
            <span class="rounded px-2 py-0.5 ring-1" :class="badgeBg(lensL.passB)">
              {{ badgeText(lensL.passB, delta(lensL.b, lensL.expectedB)) }}
            </span>
          </div>
          <div class="mt-1 font-mono text-lg text-slate-100">{{ fmt(lensL.b) }} mm</div>
          <div class="text-xs text-slate-500">target {{ fmt(lensL.expectedB) }}</div>
        </div>
        <div class="rounded-md px-3 py-2 text-center text-xs font-semibold ring-1"
             :class="badgeBg(lensL.pass)"
        >
          {{ lensL.pass === 'pass' ? '✓ Linkes Glas i.O.' : lensL.pass === 'fail' ? '✗ Linkes Glas außerhalb' : '- kein Vergleich' }}
        </div>
      </div>

      <!-- WIREFRAME -->
      <div class="flex items-center justify-center rounded-lg bg-slate-950/60 ring-1 ring-slate-800">
        <svg
          :viewBox="`0 0 ${svg.w} ${svg.h}`"
          class="h-full w-full p-3"
          preserveAspectRatio="xMidYMid meet"
          :class="tracing ? 'opacity-70 nt-pulse' : ''"
        >
          <defs>
            <linearGradient id="lensFill" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stop-color="rgba(59,130,246,0.18)" />
              <stop offset="100%" stop-color="rgba(16,185,129,0.10)" />
            </linearGradient>
          </defs>
          <line :x1="svg.bridge.x1" :y1="svg.bridge.y1" :x2="svg.bridge.x2" :y2="svg.bridge.y2"
                stroke="#475569" stroke-width="0.4" stroke-dasharray="0.6 0.6" />
          <polygon v-if="polyL"
                   :points="polyL"
                   fill="url(#lensFill)" :stroke="colorFor(lensL.pass)" stroke-width="0.5" stroke-linejoin="round" />
          <ellipse v-else :cx="svg.L.cx" :cy="svg.L.cy" :rx="svg.L.rx" :ry="svg.L.ry"
                   fill="url(#lensFill)" :stroke="colorFor(lensL.pass)" stroke-width="0.6" stroke-dasharray="0.8 0.8" />

          <polygon v-if="polyR"
                   :points="polyR"
                   fill="url(#lensFill)" :stroke="colorFor(lensR.pass)" stroke-width="0.5" stroke-linejoin="round" />
          <ellipse v-else :cx="svg.R.cx" :cy="svg.R.cy" :rx="svg.R.rx" :ry="svg.R.ry"
                   fill="url(#lensFill)" :stroke="colorFor(lensR.pass)" stroke-width="0.6" stroke-dasharray="0.8 0.8" />
          <text :x="svg.L.cx" :y="svg.L.cy + 2.2" text-anchor="middle"
                font-family="JetBrains Mono, ui-monospace, monospace" font-size="6" font-weight="600" fill="#e2e8f0">
            L {{ fmt(lensL.a) }}x{{ fmt(lensL.b) }}
          </text>
          <text :x="svg.R.cx" :y="svg.R.cy + 2.2" text-anchor="middle"
                font-family="JetBrains Mono, ui-monospace, monospace" font-size="6" font-weight="600" fill="#e2e8f0">
            R {{ fmt(lensR.a) }}x{{ fmt(lensR.b) }}
          </text>
        </svg>
      </div>

      <!-- RIGHT lens stats -->
      <div class="flex flex-col gap-2">
        <h3 class="text-right text-xs font-semibold uppercase tracking-wider text-blue-300">Rechtes Glas</h3>
        <div class="rounded-lg border border-slate-800 bg-slate-950/50 p-3">
          <div class="flex items-baseline justify-between text-xs text-slate-400">
            <span>Width (A)</span>
            <span class="rounded px-2 py-0.5 ring-1" :class="badgeBg(lensR.passA)">
              {{ badgeText(lensR.passA, delta(lensR.a, lensR.expectedA)) }}
            </span>
          </div>
          <div class="mt-1 font-mono text-lg text-slate-100">{{ fmt(lensR.a) }} mm</div>
          <div class="text-xs text-slate-500">target {{ fmt(lensR.expectedA) }}</div>
        </div>
        <div class="rounded-lg border border-slate-800 bg-slate-950/50 p-3">
          <div class="flex items-baseline justify-between text-xs text-slate-400">
            <span>Height (B)</span>
            <span class="rounded px-2 py-0.5 ring-1" :class="badgeBg(lensR.passB)">
              {{ badgeText(lensR.passB, delta(lensR.b, lensR.expectedB)) }}
            </span>
          </div>
          <div class="mt-1 font-mono text-lg text-slate-100">{{ fmt(lensR.b) }} mm</div>
          <div class="text-xs text-slate-500">target {{ fmt(lensR.expectedB) }}</div>
        </div>
        <div class="rounded-md px-3 py-2 text-center text-xs font-semibold ring-1"
             :class="badgeBg(lensR.pass)"
        >
          {{ lensR.pass === 'pass' ? '✓ Rechtes Glas i.O.' : lensR.pass === 'fail' ? '✗ Rechtes Glas außerhalb' : '- kein Vergleich' }}
        </div>
      </div>
    </div>

    <Teleport to="body">
      <div
        v-if="expanded"
        class="fixed inset-0 z-50 flex flex-col bg-slate-950/95 p-6 backdrop-blur"
        @click.self="expanded = false"
      >
        <div class="mb-4 flex items-center justify-between">
          <h2 class="text-sm font-semibold tracking-wide text-slate-100">Frame outline - {{ fmt(lensL.a) }}x{{ fmt(lensL.b) }} L / {{ fmt(lensR.a) }}x{{ fmt(lensR.b) }} R</h2>
          <button
            @click="expanded = false"
            title="Schliessen (Esc)"
            class="flex h-9 w-9 items-center justify-center rounded-md border border-slate-700 bg-slate-800/60 text-slate-200 hover:bg-rose-600 hover:text-white"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" class="h-4 w-4">
              <line x1="6" y1="6" x2="18" y2="18" />
              <line x1="18" y1="6" x2="6" y2="18" />
            </svg>
          </button>
        </div>
        <div class="flex flex-1 items-center justify-center rounded-lg bg-slate-950/80 ring-1 ring-slate-800">
          <svg
            :viewBox="`0 0 ${svg.w} ${svg.h}`"
            class="h-full w-full p-6"
            preserveAspectRatio="xMidYMid meet"
          >
            <defs>
              <linearGradient id="lensFillBig" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stop-color="rgba(59,130,246,0.18)" />
                <stop offset="100%" stop-color="rgba(16,185,129,0.10)" />
              </linearGradient>
            </defs>
            <line :x1="svg.bridge.x1" :y1="svg.bridge.y1" :x2="svg.bridge.x2" :y2="svg.bridge.y2"
                  stroke="#475569" stroke-width="0.4" stroke-dasharray="0.6 0.6" />
            <polygon v-if="polyL" :points="polyL"
                     fill="url(#lensFillBig)" :stroke="colorFor(lensL.pass)" stroke-width="0.4" stroke-linejoin="round" />
            <ellipse v-else :cx="svg.L.cx" :cy="svg.L.cy" :rx="svg.L.rx" :ry="svg.L.ry"
                     fill="url(#lensFillBig)" :stroke="colorFor(lensL.pass)" stroke-width="0.5" stroke-dasharray="0.8 0.8" />
            <polygon v-if="polyR" :points="polyR"
                     fill="url(#lensFillBig)" :stroke="colorFor(lensR.pass)" stroke-width="0.4" stroke-linejoin="round" />
            <ellipse v-else :cx="svg.R.cx" :cy="svg.R.cy" :rx="svg.R.rx" :ry="svg.R.ry"
                     fill="url(#lensFillBig)" :stroke="colorFor(lensR.pass)" stroke-width="0.5" stroke-dasharray="0.8 0.8" />
            <text :x="svg.L.cx" :y="svg.L.cy + 1.4" text-anchor="middle"
                  font-family="JetBrains Mono, ui-monospace, monospace" font-size="3.5" font-weight="600" fill="#e2e8f0">
              L {{ fmt(lensL.a) }}x{{ fmt(lensL.b) }}
            </text>
            <text :x="svg.R.cx" :y="svg.R.cy + 1.4" text-anchor="middle"
                  font-family="JetBrains Mono, ui-monospace, monospace" font-size="3.5" font-weight="600" fill="#e2e8f0">
              R {{ fmt(lensR.a) }}x{{ fmt(lensR.b) }}
            </text>
          </svg>
        </div>
      </div>
    </Teleport>
  </article>
</template>
