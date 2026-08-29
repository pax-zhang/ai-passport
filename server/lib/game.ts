export const PLOT_N = 12;
export const CROP_N = 6;
export const STEAL_PCT = 60;
export const STEAL_KEEP = 20;
export const STEAL_MAX = 2;
export const STEAL_DAY_MAX = 10;
export const HELP_DAY_MAX = 10;
export const COOL_SEC = 600;
export const START_COIN = 80;
export const START_SEED = 4;

export const CROPS = [
  { grow: 1800, cost: 10, harvest: 18, lv: 1 },
  { grow: 3600, cost: 15, harvest: 28, lv: 1 },
  { grow: 7200, cost: 25, harvest: 48, lv: 3 },
  { grow: 14400, cost: 40, harvest: 80, lv: 5 },
  { grow: 21600, cost: 60, harvest: 130, lv: 8 },
  { grow: 28800, cost: 100, harvest: 220, lv: 12 },
] as const;

export type Plot = {
  c: number;
  s: number;
  d: number;
  w: number;
  p: number;
  x: number;
  y: number;
  g: number;
  n: number;
};

export function emptyPlot(): Plot {
  return { c: 0, s: 0, d: 0, w: 0, p: 0, x: 0, y: 0, g: 0, n: 0 };
}

export function emptyPlots(): Plot[] {
  return Array.from({ length: PLOT_N }, emptyPlot);
}

export function plotN(level: number): number {
  if (level >= 6) return 12;
  if (level >= 3) return 9;
  return 6;
}

export function yieldOf(plot: Plot): number {
  if (plot.s === 0 || plot.s === 4) return 0;
  if (plot.y == null) return 100;
  return Math.max(0, Math.min(100, plot.y | 0));
}

export function stealTakePct(yieldPct: number): number {
  if (yieldPct <= STEAL_KEEP) return 0;
  let take = Math.floor((yieldPct * STEAL_PCT) / 100);
  if (take < 1) take = 1;
  if (yieldPct - take < STEAL_KEEP) take = yieldPct - STEAL_KEEP;
  return take;
}

export function canSteal(plot: Plot, level: number, idx: number): boolean {
  if (idx < 0 || idx >= plotN(level)) return false;
  return plot.s === 3 && stealTakePct(yieldOf(plot)) > 0;
}

export function stealCoins(plot: Plot): number {
  const info = CROPS[plot.c];
  if (!info) return 1;
  const take = stealTakePct(yieldOf(plot));
  return Math.max(1, Math.floor((info.harvest * take) / 100));
}

export function nowSec(): number {
  return Math.floor(Date.now() / 1000);
}

export function shanghaiYmd(nowMs = Date.now()): number {
  const s = new Intl.DateTimeFormat("en-CA", {
    timeZone: "Asia/Shanghai",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  }).format(new Date(nowMs));
  return Number(s.replaceAll("-", ""));
}

export function msUntilShanghaiMidnight(nowMs = Date.now()): number {
  const into = (nowMs + 8 * 3600 * 1000) % 86400000;
  return into === 0 ? 86400000 : 86400000 - into;
}

export function mergePlot(server: Plot, local: Plot): Plot {
  const sg = server.n ?? 0;
  const lg = local.n ?? 0;
  const stolen = server.s === 0 && !!server.x;

  if (stolen) {
    if (lg && sg && lg > sg) return { ...local, x: 0 };
    if (!lg && !sg && local.s !== 3) return { ...local, x: 0 };
    return { ...server, n: lg || sg };
  }
  if (lg && sg && lg !== sg) return lg > sg ? { ...local, x: 0 } : { ...server };
  if (local.s === 0) return { ...local, n: lg || sg };
  if (server.s === 0) return { ...local, x: 0 };
  const y = Math.min(yieldOf(local), yieldOf(server) || 100);
  return {
    ...local,
    y,
    d: local.d && server.d ? 1 : 0,
    w: local.w && server.w ? 1 : 0,
    p: local.p && server.p ? 1 : 0,
    s: server.s === 4 ? 4 : local.s,
    x: 0,
    n: lg || sg,
  };
}
