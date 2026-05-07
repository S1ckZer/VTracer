import { reactive, ref } from 'vue';
import type {
  CatalogState, LogEntry, LogLevel, ModelLookup,
  RecentSave, SaveResult, TraceFinished, TraceSide,
} from '../types';

type Outbound =
  | { type: 'ready' }
  | { type: 'list_ports' }
  | { type: 'reload_csv' }
  | { type: 'lookup_model'; model: string }
  | { type: 'start_trace'; port: string; side: TraceSide; simulate: boolean; expected?: { aL: number; bL: number; aR: number; bR: number } | null }
  | { type: 'save_result' } & SaveResultPayload
  | { type: 'window'; action: 'drag' | 'minimize' | 'maximize' | 'close' }
  | { type: 'ping' };

interface SaveResultPayload {
  brand: string;
  model: string;
  batch: string;
  note: string;
  tolerance: number;
  aL: number | null;
  bL: number | null;
  aR: number | null;
  bR: number | null;
  expectedAL: number | null;
  expectedBL: number | null;
  expectedAR: number | null;
  expectedBR: number | null;
}

type Inbound =
  | { type: 'ports'; ports: string[] }
  | { type: 'log'; level: LogLevel; msg: string }
  | { type: 'catalog' } & CatalogState
  | { type: 'model_lookup' } & ModelLookup
  | { type: 'pong' }
  | { type: 'save_result_done' } & SaveResult
  | { type: 'trace_finished' } & TraceFinished;

const isWebView = typeof window !== 'undefined' && !!window.chrome?.webview;

export function useBridge() {
  const ports = ref<string[]>([]);
  const log = ref<LogEntry[]>([]);
  const tracing = ref(false);
  const tracingSide = ref<TraceSide | ''>('');
  const catalog = reactive<CatalogState>({
    dir: '',
    error: '',
    stats: { total: 0, accepted: 0, flagged: 0, skipped: 0 },
    files: [],
    flagged: [],
  });
  const lookup = ref<ModelLookup | null>(null);
  const lastResult = ref<TraceFinished | null>(null);
  const recentSaves = ref<RecentSave[]>([]);

  function pushLog(level: LogLevel, message: string) {
    const time = new Date().toLocaleTimeString('de-DE', { hour12: false });
    log.value.push({ time, level, message });
    if (log.value.length > 400) log.value.splice(0, log.value.length - 400);
  }

  function send(msg: Outbound) {
    if (isWebView) {
      window.chrome!.webview!.postMessage(JSON.stringify(msg));
    } else {
      mockHandle(msg);
    }
  }

  function handle(msg: Inbound) {
    switch (msg.type) {
      case 'ports':
        ports.value = msg.ports;
        break;
      case 'log':
        pushLog(msg.level, msg.msg);
        break;
      case 'catalog': {
        catalog.dir = msg.dir;
        catalog.error = msg.error;
        catalog.stats = msg.stats;
        catalog.files = msg.files;
        catalog.flagged = msg.flagged;
        if (msg.error) pushLog('error', `Catalog: ${msg.error}`);
        else pushLog('info',
          `Catalog reloaded: ${msg.files.length} file(s), ` +
          `${msg.stats.accepted} matchable, ${msg.stats.flagged} flagged`);
        break;
      }
      case 'model_lookup':
        lookup.value = msg;
        if (msg.found) pushLog('info', `Model ${msg.model}: ${msg.expected?.aL.toFixed(2)}×${msg.expected?.bL.toFixed(2)} L · ${msg.expected?.aR.toFixed(2)}×${msg.expected?.bR.toFixed(2)} R`);
        else pushLog('warn', `Model ${msg.model} not in catalog`);
        break;
      case 'trace_finished':
        tracing.value = false;
        tracingSide.value = '';
        lastResult.value = { side: msg.side, ok: msg.ok, error: msg.error, measurement: msg.measurement };
        if (msg.ok) {
          pushLog('info', `Trace done (${msg.side})`);
        } else {
          pushLog('error', msg.error ?? 'Trace failed');
        }
        break;
      case 'save_result_done': {
        if (msg.ok) {
          pushLog('info', `Saved ${msg.model}/${msg.batch} -> ${msg.path}`);
          recentSaves.value.unshift({
            time: new Date().toLocaleTimeString('de-DE', { hour12: false }),
            brand: '',
            model: msg.model,
            batch: msg.batch,
            passL: msg.passL,
            passR: msg.passR,
          });
          if (recentSaves.value.length > 12) recentSaves.value.length = 12;
        } else {
          pushLog('error', `Save failed: ${msg.path}`);
        }
        break;
      }
      case 'pong':
      default:
        break;
    }
  }

  if (isWebView) {
    window.chrome!.webview!.addEventListener('message', (ev) => {
      try { handle(JSON.parse(ev.data) as Inbound); }
      catch { /* ignore */ }
    });
    send({ type: 'ready' });
  } else {
    setTimeout(() => {
      handle({
        type: 'catalog',
        dir: '(mock) frontend dev mode - no backend',
        error: '',
        stats: { total: 105, accepted: 102, flagged: 1, skipped: 4 },
        files: [{ path: '(mock)/Roy Robson.csv', brand: 'Roy Robson',
                  accepted: 102, flagged: 1, skipped: 4, error: '' }],
        flagged: [{ brand: 'Roy Robson', model: '60136', reason: 'L/R differ by 9.63 mm' }],
      });
      handle({ type: 'ports', ports: ['COM3', 'COM4', 'COM7'] });
    }, 50);
  }

  function startTrace(port: string, side: TraceSide, simulate = false) {
    if (!simulate && !port) return;
    tracing.value = true;
    tracingSide.value = side;
    const exp = lookup.value?.expected ? { ...lookup.value.expected } : null;
    pushLog('info', `Starting trace (${side})${simulate ? ' [SIM]' : ` on ${port}`}...`);
    send({ type: 'start_trace', port, side, simulate, expected: exp });
  }
  function reloadCsv()                       { send({ type: 'reload_csv' });  }
  function listPorts()                       { send({ type: 'list_ports' });  }
  function lookupModel(model: string)        {
    if (!model) { lookup.value = null; return; }
    send({ type: 'lookup_model', model });
  }
  function saveResult(p: SaveResultPayload)  { send({ type: 'save_result', ...p }); }
  function winDrag()                         { send({ type: 'window', action: 'drag' });     }
  function winMinimize()                     { send({ type: 'window', action: 'minimize' }); }
  function winMaximize()                     { send({ type: 'window', action: 'maximize' }); }
  function winClose()                        { send({ type: 'window', action: 'close' });    }

  // ---------------------------------------------------------------------
  function mockHandle(msg: Outbound) {
    if (msg.type === 'list_ports') {
      handle({ type: 'ports', ports: ['COM3', 'COM4', 'COM7'] });
      return;
    }
    if (msg.type === 'reload_csv') {
      handle({
        type: 'catalog',
        dir: '(mock)',
        error: '',
        stats: { total: 105, accepted: 102, flagged: 1, skipped: 4 },
        files: [{ path: '(mock)/Roy Robson.csv', brand: 'Roy Robson',
                  accepted: 102, flagged: 1, skipped: 4, error: '' }],
        flagged: [{ brand: 'Roy Robson', model: '60136', reason: 'L/R differ by 9.63 mm' }],
      });
      return;
    }
    if (msg.type === 'lookup_model') {
      const fake: Record<string, ModelLookup> = {
        '40103': {
          model: '40103', found: true, brand: 'Roy Robson', intro: '08.2025',
          flagged: false, flagReason: '',
          expected: { aL: 53.47, bL: 47.52, aR: 53.38, bR: 47.48 },
        },
        '60134': {
          model: '60134', found: true, brand: 'Roy Robson', intro: '01.2025',
          expected: { aL: 54.21, bL: 41.55, aR: 54.20, bR: 41.43 },
        },
      };
      handle({ type: 'model_lookup', ...(fake[msg.model] ?? { model: msg.model, found: false }) });
      return;
    }
    if (msg.type === 'start_trace') {
      pushLog('debug', `PC -> ENQ (${msg.side})`);
      setTimeout(() => pushLog('debug', 'LT -> ACK'), 200);
      setTimeout(() => pushLog('debug', `PC -> STX REQ=${msg.side === 'left' ? 'TRCL' : msg.side === 'right' ? 'TRCR' : 'TRC'} ETX BCC`), 400);
      setTimeout(() => pushLog('info', 'Received 6324 bytes from tracer'), 700);
      setTimeout(() => {
        const base = { aL: 53.47, bL: 47.52, aR: 53.38, bR: 47.48 };
        const jitter = () => (Math.random() - 0.5) * 0.6;
        const aL = base.aL + jitter(), bL = base.bL + jitter();
        const aR = base.aR + jitter(), bR = base.bR + jitter();
        const synth = (a: number, b: number) => {
          const N = 240;
          const variant = Math.floor(Math.random() * 3);
          const ph = Math.random() * Math.PI * 2;
          const out: { x: number; y: number }[] = [];
          for (let i = 0; i < N; i++) {
            const t = i * 2 * Math.PI / N;
            const rEll = (a * 0.5 * b * 0.5) /
              Math.sqrt((b * 0.5 * Math.cos(t)) ** 2 + (a * 0.5 * Math.sin(t)) ** 2);
            let extra = 0;
            if (variant === 1) extra = 0.6 * Math.sin(2 * t + ph);
            else if (variant === 2) extra = -0.5 * Math.cos(4 * t);
            const r = rEll + extra + (Math.random() - 0.5) * 0.36;
            out.push({ x: r * Math.cos(t), y: r * Math.sin(t) });
          }
          return out;
        };
        handle({
          type: 'trace_finished',
          side: msg.side, ok: true,
          measurement: {
            aL, bL, aR, bR,
            dbl: 17.5, circumferenceL: 165.4, circumferenceR: 165.3,
            pointsL: synth(aL, bL),
            pointsR: synth(aR, bR),
          },
        });
      }, 900);
      return;
    }
    if (msg.type === 'save_result') {
      handle({
        type: 'save_result_done',
        ok: true, path: '(mock)\\results.csv',
        model: msg.model, batch: msg.batch,
        passL: 'PASS', passR: 'PASS',
      });
    }
  }

  return {
    ports, log, tracing, tracingSide, catalog, lookup, lastResult, recentSaves,
    startTrace, reloadCsv, listPorts, lookupModel, saveResult,
    winDrag, winMinimize, winMaximize, winClose,
  };
}
