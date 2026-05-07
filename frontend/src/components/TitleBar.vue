<script setup lang="ts">
import type { CatalogState } from '../types';

defineProps<{
  catalog: CatalogState;
  comPort: string;
}>();

const emit = defineEmits<{
  drag: [];
  minimize: [];
  maximize: [];
  close: [];
}>();

function onMouseDown(e: MouseEvent) {
  // Only start a drag on a primary-button press; let the shell handle dbl-click
  // (Windows toggles maximize on dbl-click on the title bar).
  if (e.button === 0 && e.detail === 1) emit('drag');
}
function onDblClick(e: MouseEvent) {
  if (e.button === 0) emit('maximize');
}
</script>

<template>
  <header
    class="flex h-9 select-none items-center bg-slate-950/95 text-slate-200 ring-1 ring-slate-800/80"
    @mousedown="onMouseDown"
    @dblclick="onDblClick"
  >
    <!-- App icon + title -->
    <div class="flex items-center gap-2 px-3">
      <span class="flex h-5 w-5 items-center justify-center rounded-sm bg-blue-600/30 ring-1 ring-blue-500/40">
        <svg viewBox="0 0 24 24" class="h-3.5 w-3.5 text-blue-300" fill="none">
          <ellipse cx="7" cy="12" rx="5" ry="4" stroke="currentColor" stroke-width="1.5" />
          <ellipse cx="17" cy="12" rx="5" ry="4" stroke="currentColor" stroke-width="1.5" />
        </svg>
      </span>
      <span class="text-xs font-semibold tracking-wide">VTracer</span>
      <span class="text-xs text-slate-500">·</span>
      <span
        class="text-xs text-slate-400"
        :title="catalog.files.map(f => `${f.brand || '(unknown)'} - ${f.path.split(/[\\\/]/).pop()} (${f.accepted})`).join('\n') || 'no catalog'"
      >
        <template v-if="catalog.error">catalog error</template>
        <template v-else-if="catalog.files.length === 0">no catalog</template>
        <template v-else>{{ catalog.files.length }} catalog{{ catalog.files.length === 1 ? '' : 's' }} · {{ catalog.stats.accepted }} models</template>
      </span>
      <span v-if="comPort" class="text-xs text-slate-500">·</span>
      <span v-if="comPort" class="text-xs text-slate-400">{{ comPort }}</span>
    </div>

    <!-- Drag spacer (the entire empty bar drags via @mousedown above) -->
    <div class="flex-1" />

    <!-- Window buttons -->
    <div class="flex h-full items-stretch" @mousedown.stop>
      <button class="flex h-full w-11 items-center justify-center text-slate-300 hover:bg-slate-800/80"
              title="Minimize" @click="emit('minimize')">
        <svg viewBox="0 0 12 12" class="h-3 w-3"><line x1="2" y1="6" x2="10" y2="6" stroke="currentColor" stroke-width="1.2" /></svg>
      </button>
      <button class="flex h-full w-11 items-center justify-center text-slate-300 hover:bg-slate-800/80"
              title="Maximize" @click="emit('maximize')">
        <svg viewBox="0 0 12 12" class="h-3 w-3"><rect x="2.5" y="2.5" width="7" height="7" stroke="currentColor" stroke-width="1.2" fill="none" /></svg>
      </button>
      <button class="flex h-full w-11 items-center justify-center text-slate-300 hover:bg-rose-600 hover:text-white"
              title="Close" @click="emit('close')">
        <svg viewBox="0 0 12 12" class="h-3 w-3"><path d="M2.5 2.5 L9.5 9.5 M9.5 2.5 L2.5 9.5" stroke="currentColor" stroke-width="1.2" /></svg>
      </button>
    </div>
  </header>
</template>
