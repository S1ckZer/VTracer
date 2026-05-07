export interface TracePoint { x: number; y: number; }

export interface Measurement {
  aL: number | null;
  bL: number | null;
  aR: number | null;
  bR: number | null;
  dbl: number | null;
  circumferenceL: number | null;
  circumferenceR: number | null;
  pointsL?: TracePoint[];
  pointsR?: TracePoint[];
}

export interface FrameSize {
  aL: number;
  bL: number;
  aR: number;
  bR: number;
}

export type LogLevel = 'info' | 'warn' | 'error' | 'debug';
export interface LogEntry {
  time: string;
  level: LogLevel;
  message: string;
}

export interface CatalogStats {
  total: number;
  accepted: number;
  flagged: number;
  skipped: number;
}
export interface CatalogFile {
  path: string;
  brand: string;
  accepted: number;
  flagged: number;
  skipped: number;
  error: string;
}
export interface CatalogState {
  dir: string;
  error: string;
  stats: CatalogStats;
  files: CatalogFile[];
  flagged: { brand: string; model: string; reason: string }[];
}

export interface ModelLookup {
  model: string;
  found: boolean;
  brand?: string;
  intro?: string;
  flagged?: boolean;
  flagReason?: string;
  expected?: FrameSize;
}

export type TraceSide = 'left' | 'right' | 'center' | 'both';

export interface TraceFinished {
  side: TraceSide;
  ok: boolean;
  error?: string;
  measurement: Measurement;
}

export interface SaveResult {
  ok: boolean;
  path: string;
  model: string;
  batch: string;
  passL: 'PASS' | 'FAIL' | '-';
  passR: 'PASS' | 'FAIL' | '-';
}

export interface RecentSave {
  time: string;
  brand: string;
  model: string;
  batch: string;
  passL: 'PASS' | 'FAIL' | '-';
  passR: 'PASS' | 'FAIL' | '-';
}
