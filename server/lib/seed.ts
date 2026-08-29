import type { RowDataPacket } from "mysql2/promise";
import { exec, query, readyDb } from "./db";
import { emptyPlots, msUntilShanghaiMidnight, plotN, shanghaiYmd, type Plot } from "./game";
import {
  deleteFakeFarms,
  isFakeFarm,
  listFarms,
  putFarm,
  type Farm,
} from "./store";

const SEED_KEY = "seed:day";

const g = globalThis as typeof globalThis & {
  __farmSeedDay?: number;
  __farmSeedInflight?: Promise<{ ids: number[] } | undefined>;
  __farmSeedTimer?: ReturnType<typeof setTimeout>;
};

function rng(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (Math.imul(a, 1664525) + 1013904223) >>> 0;
    return a / 4294967296;
  };
}

function varySpec(spec: Spec, day: number): Spec {
  const r = rng(day * 1000003 + spec.id);
  const n = (a: number, b: number) => a + Math.floor(r() * (b - a + 1));
  const span = Math.max(3, Math.floor(spec.coins * 0.08));
  return {
    ...spec,
    coins: Math.max(0, spec.coins + n(-span, span)),
    xp: Math.max(0, Math.min(99, spec.xp + n(-6, 6))),
    seeds: spec.seeds.map((v) => (r() < 0.55 ? v : Math.max(0, Math.min(9, v + n(-1, 1))))),
    plots: spec.plots.map((pl) => varyPlot(pl, r, n)),
  };
}

function varyPlot(
  pl: Plot,
  r: () => number,
  n: (a: number, b: number) => number
): Plot {
  if (pl.s === 0) {
    if (r() >= 0.07) return { ...pl };
    return p(n(0, 2), 1, { g: n(2, 18), d: r() < 0.35 ? 1 : 0 });
  }
  const extra: Partial<Pick<Plot, "d" | "w" | "p" | "g" | "y" | "n">> = {
    d: pl.d,
    w: pl.w,
    p: pl.p,
    g: pl.g,
    y: pl.y,
    n: pl.n,
  };
  let s = pl.s;
  if (s === 1 || s === 2) {
    const span = Math.max(3, Math.floor((pl.g || 0) * 0.2));
    extra.g = Math.max(0, (pl.g || 0) + n(-span, span));
  }
  if (s === 3 && r() < 0.12) {
    s = 2;
    extra.g = n(8, 40);
  } else if (s === 2 && r() < 0.12) {
    s = 3;
    extra.g = 0;
  }
  if (r() < 0.12) extra.w = extra.w ? 0 : 1;
  if (r() < 0.1) extra.p = extra.p ? 0 : 1;
  if (r() < 0.1) extra.d = extra.d ? 0 : 1;
  return p(pl.c, s, extra);
}

async function lastSeedDay(): Promise<number> {
  await readyDb();
  const rows = await query<RowDataPacket & { at_sec: number }>(
    "SELECT at_sec FROM steal_cool WHERE cool_key = ? LIMIT 1",
    [SEED_KEY]
  );
  return rows[0] ? Number(rows[0].at_sec) : 0;
}

async function putSeedDay(day: number): Promise<void> {
  await readyDb();
  await exec(
    `INSERT INTO steal_cool (cool_key, at_sec, cool_n) VALUES (?, ?, 1)
     ON DUPLICATE KEY UPDATE at_sec = VALUES(at_sec), cool_n = 1`,
    [SEED_KEY, day]
  );
}

const FAKE_MAC = "FA:KE:00:00:00";

type Spec = {
  id: number;
  name: string;
  level: number;
  xp: number;
  coins: number;
  pick: number;
  seeds: number[];
  plots: Plot[];
};

function p(
  c: number,
  s: number,
  extra?: Partial<Pick<Plot, "d" | "w" | "p" | "g" | "y" | "n">>
): Plot {
  return {
    c,
    s,
    d: extra?.d ?? 0,
    w: extra?.w ?? 0,
    p: extra?.p ?? 0,
    x: 0,
    y: extra?.y ?? (s ? 100 : 0),
    g: extra?.g ?? 0,
    n: extra?.n ?? (s ? 1 : 0),
  };
}

function pad(plots: Plot[], level: number): Plot[] {
  const all = emptyPlots();
  const n = plotN(level);
  for (let i = 0; i < n && i < plots.length; i++) all[i] = plots[i];
  return all;
}

function specs(now: number): Spec[] {
  const day = shanghaiYmd(now * 1000);
  return [
    {
      id: 900001,
      name: "阿强",
      level: 1,
      xp: 12,
      coins: 96,
      pick: 0,
      seeds: [3, 1, 0, 0, 0, 0],
      plots: [
        p(0, 3),
        p(0, 3),
        p(1, 2, { g: 20 }),
        p(0, 1, { d: 1, g: 45 }),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900002,
      name: "路过看看",
      level: 3,
      xp: 8,
      coins: 240,
      pick: 2,
      seeds: [2, 2, 2, 0, 0, 0],
      plots: [
        p(2, 3),
        p(1, 3),
        p(0, 3, { w: 1 }),
        p(2, 2, { g: 40 }),
        p(1, 1, { d: 1, g: 75 }),
        p(0, 0),
        p(0, 0),
        p(2, 3),
        p(0, 0),
      ],
    },
    {
      id: 900003,
      name: "Lucky",
      level: 5,
      xp: 30,
      coins: 480,
      pick: 3,
      seeds: [1, 1, 1, 2, 0, 0],
      plots: [
        p(3, 3),
        p(2, 3),
        p(1, 3),
        p(3, 2, { g: 60 }),
        p(0, 1, { d: 1, g: 40 }),
        p(2, 3, { p: 1 }),
        p(0, 0),
        p(1, 2, { g: 15 }),
        p(0, 0),
      ],
    },
    {
      id: 900004,
      name: "王美丽",
      level: 8,
      xp: 44,
      coins: 860,
      pick: 4,
      seeds: [0, 1, 1, 1, 3, 0],
      plots: [
        p(4, 3),
        p(4, 3),
        p(3, 3),
        p(2, 3),
        p(4, 2, { g: 80 }),
        p(1, 3, { w: 1 }),
        p(0, 0),
        p(3, 1, { d: 1, g: 180 }),
        p(2, 2, { g: 30 }),
        p(0, 0),
        p(4, 3),
        p(0, 0),
      ],
    },
    {
      id: 900005,
      name: "不想种地",
      level: 12,
      xp: 20,
      coins: 2480,
      pick: 5,
      seeds: [1, 1, 1, 1, 1, 2],
      plots: [
        p(5, 3),
        p(5, 3),
        p(4, 3),
        p(3, 3),
        p(5, 2, { g: 120 }),
        p(2, 3),
        p(1, 3),
        p(0, 3),
        p(4, 1, { d: 1, g: 200 }),
        p(3, 2, { g: 50 }),
        p(5, 3, { p: 1 }),
        p(0, 0),
      ],
    },
    {
      id: 900006,
      name: "7号",
      level: 2,
      xp: 6,
      coins: 40,
      pick: 0,
      seeds: [2, 0, 0, 0, 0, 0],
      plots: [
        p(0, 1, { d: 1, g: 45 }),
        p(0, 1, { d: 1, g: 30 }),
        p(1, 2, { d: 1, g: 50 }),
        p(0, 0),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900007,
      name: "嘿嘿嘿",
      level: 4,
      xp: 18,
      coins: 110,
      pick: 1,
      seeds: [1, 2, 1, 0, 0, 0],
      plots: [
        p(2, 3, { w: 1 }),
        p(1, 3, { p: 1 }),
        p(0, 3, { w: 1, p: 1 }),
        p(2, 2, { g: 25, w: 1 }),
        p(1, 1, { d: 1, g: 60 }),
        p(0, 0),
        p(0, 0),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900008,
      name: "老陈头",
      level: 1,
      xp: 0,
      coins: 24,
      pick: 0,
      seeds: [4, 0, 0, 0, 0, 0],
      plots: [],
    },
    {
      id: 900009,
      name: "xiao",
      level: 1,
      xp: 18,
      coins: 60,
      pick: 0,
      seeds: [2, 0, 0, 0, 0, 0],
      plots: [p(0, 3), p(0, 3), p(0, 3), p(0, 3), p(0, 2, { g: 10 }), p(0, 0)],
    },
    {
      id: 900010,
      name: "TONY",
      level: 2,
      xp: 22,
      coins: 180,
      pick: 1,
      seeds: [1, 3, 0, 0, 0, 0],
      plots: [
        p(1, 3),
        p(1, 3),
        p(1, 3),
        p(0, 3),
        p(1, 2, { g: 35 }),
        p(0, 0),
      ],
    },
    {
      id: 900011,
      name: "吃饱了",
      level: 3,
      xp: 14,
      coins: 320,
      pick: 2,
      seeds: [1, 1, 3, 0, 0, 0],
      plots: [
        p(2, 3),
        p(2, 3),
        p(2, 3),
        p(1, 3),
        p(2, 2, { g: 50 }),
        p(0, 1, { g: 20 }),
        p(2, 3),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900012,
      name: "00后",
      level: 5,
      xp: 26,
      coins: 520,
      pick: 3,
      seeds: [0, 1, 1, 3, 0, 0],
      plots: [
        p(3, 3),
        p(3, 3),
        p(3, 3),
        p(2, 3),
        p(3, 2, { g: 80 }),
        p(1, 3),
        p(0, 3),
        p(3, 1, { d: 1, g: 90 }),
        p(0, 0),
      ],
    },
    {
      id: 900013,
      name: "一只羊",
      level: 8,
      xp: 10,
      coins: 980,
      pick: 4,
      seeds: [0, 0, 1, 1, 4, 0],
      plots: [
        p(4, 3),
        p(4, 3),
        p(4, 3),
        p(4, 3),
        p(3, 3),
        p(2, 3),
        p(4, 2, { g: 90 }),
        p(1, 3, { w: 1 }),
        p(0, 0),
        p(4, 1, { g: 160 }),
        p(4, 3),
        p(0, 0),
      ],
    },
    {
      id: 900014,
      name: "有点困",
      level: 12,
      xp: 8,
      coins: 1600,
      pick: 5,
      seeds: [0, 0, 0, 1, 1, 3],
      plots: [
        p(5, 3),
        p(5, 3),
        p(5, 3),
        p(4, 3),
        p(5, 2, { g: 140 }),
        p(3, 3),
        p(2, 3),
        p(5, 1, { d: 1, g: 220 }),
        p(4, 3),
        p(0, 0),
        p(5, 3),
        p(0, 0),
      ],
    },
    {
      id: 900015,
      name: "陈小鱼",
      level: 1,
      xp: 4,
      coins: 50,
      pick: 0,
      seeds: [2, 1, 0, 0, 0, 0],
      plots: [
        p(0, 1, { g: 5 }),
        p(0, 1, { g: 8 }),
        p(1, 1, { g: 12 }),
        p(0, 1, { g: 3 }),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900016,
      name: "别偷我",
      level: 2,
      xp: 2,
      coins: 70,
      pick: 0,
      seeds: [3, 1, 0, 0, 0, 0],
      plots: [
        p(0, 1, { g: 2 }),
        p(0, 1, { g: 4 }),
        p(0, 0),
        p(1, 1, { g: 6 }),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900017,
      name: "月光",
      level: 4,
      xp: 16,
      coins: 200,
      pick: 2,
      seeds: [1, 1, 2, 0, 0, 0],
      plots: [
        p(2, 2, { g: 40 }),
        p(1, 2, { g: 30 }),
        p(0, 2, { g: 15 }),
        p(2, 2, { g: 55 }),
        p(1, 2, { g: 22 }),
        p(0, 2, { g: 18 }),
        p(2, 2, { g: 48 }),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900018,
      name: "用户9527",
      level: 3,
      xp: 9,
      coins: 90,
      pick: 1,
      seeds: [1, 2, 1, 0, 0, 0],
      plots: [
        p(1, 3, { w: 1 }),
        p(0, 3, { w: 1 }),
        p(2, 3, { w: 1 }),
        p(1, 2, { w: 1, g: 28 }),
        p(0, 1, { w: 1, g: 10 }),
        p(0, 0),
        p(2, 3, { w: 1 }),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900019,
      name: "风继续吹",
      level: 6,
      xp: 12,
      coins: 55,
      pick: 2,
      seeds: [2, 1, 1, 1, 0, 0],
      plots: [
        p(2, 2, { d: 1, g: 70 }),
        p(1, 2, { d: 1, g: 40 }),
        p(3, 1, { d: 1, g: 100 }),
        p(0, 1, { d: 1, g: 20 }),
        p(2, 3, { d: 1 }),
        p(1, 1, { d: 1, g: 35 }),
        p(0, 0),
        p(3, 2, { d: 1, g: 90 }),
        p(0, 0),
        p(0, 0),
        p(2, 1, { d: 1, g: 55 }),
        p(0, 0),
      ],
    },
    {
      id: 900020,
      name: "大魔王",
      level: 10,
      xp: 36,
      coins: 5200,
      pick: 4,
      seeds: [1, 1, 1, 1, 2, 1],
      plots: [
        p(4, 3),
        p(5, 3),
        p(4, 3),
        p(3, 3),
        p(5, 2, { g: 100 }),
        p(2, 3),
        p(0, 0),
        p(4, 3),
        p(1, 3),
        p(0, 0),
        p(3, 2, { g: 40 }),
        p(0, 0),
      ],
    },
    {
      id: 900021,
      name: "浇浇水",
      level: 1,
      xp: 1,
      coins: 3,
      pick: 0,
      seeds: [1, 0, 0, 0, 0, 0],
      plots: [p(0, 3), p(0, 0), p(0, 0), p(0, 1, { d: 1, g: 40 }), p(0, 0), p(0, 0)],
    },
    {
      id: 900022,
      name: "Miki",
      level: 12,
      xp: 0,
      coins: 3100,
      pick: 5,
      seeds: [1, 1, 1, 1, 1, 1],
      plots: [
        p(5, 3),
        p(4, 3),
        p(3, 3),
        p(2, 3),
        p(1, 3),
        p(0, 3),
        p(5, 3),
        p(4, 3),
        p(3, 3),
        p(2, 3),
        p(1, 3),
        p(0, 3),
      ],
    },
    {
      id: 900023,
      name: "韩梅梅",
      level: 7,
      xp: 28,
      coins: 390,
      pick: 3,
      seeds: [1, 1, 2, 2, 0, 0],
      plots: [
        p(3, 3),
        p(2, 3),
        p(3, 2, { g: 70 }),
        p(1, 3, { p: 1 }),
        p(0, 3),
        p(2, 1, { d: 1, g: 85 }),
        p(3, 3, { w: 1 }),
        p(0, 0),
        p(1, 2, { g: 25 }),
        p(2, 3),
        p(0, 0),
        p(0, 0),
      ],
    },
    {
      id: 900024,
      name: "夜猫子",
      level: 4,
      xp: 20,
      coins: 150,
      pick: 1,
      seeds: [2, 2, 1, 0, 0, 0],
      plots: [
        p(1, 3),
        p(0, 3),
        p(2, 2, { g: 36 }),
        p(1, 3, { p: 1 }),
        p(0, 1, { d: 1, g: 28 }),
        p(2, 3),
        p(1, 2, { w: 1, g: 20 }),
        p(0, 0),
        p(0, 0),
      ],
    },
  ].map((s) => varySpec(s, day));
}

function toFarm(spec: Spec, now: number): Farm {
  return {
    id: spec.id,
    mac: `${FAKE_MAC}:${(spec.id % 256).toString(16).toUpperCase().padStart(2, "0")}`,
    token: `fake-${spec.id}`,
    name: spec.name,
    level: spec.level,
    xp: spec.xp,
    coins: spec.coins,
    pick: spec.pick,
    seeds: spec.seeds,
    plots: pad(spec.plots, spec.level),
    friends: [],
    updatedAt: now,
  };
}

export async function seedFake(): Promise<{ ids: number[] }> {
  const now = Math.floor(Date.now() / 1000);
  const day = shanghaiYmd();
  await deleteFakeFarms();
  const made = specs(now).map((s) => toFarm(s, now));
  for (const f of made) await putFarm(f);
  const real = (await listFarms()).filter((f) => !isFakeFarm(f));
  for (const r of real) {
    const friends = r.friends.filter((id) => id < 900001 || id > 900024);
    if (friends.length !== r.friends.length) await putFarm({ ...r, friends });
  }
  await putSeedDay(day);
  g.__farmSeedDay = day;
  return { ids: made.map((f) => f.id) };
}

export async function ensureDailySeed(): Promise<{ ids: number[] } | undefined> {
  const day = shanghaiYmd();
  if (g.__farmSeedDay === day) return;
  if (g.__farmSeedInflight) return g.__farmSeedInflight;
  g.__farmSeedInflight = (async () => {
    if ((await lastSeedDay()) === day) {
      g.__farmSeedDay = day;
      return;
    }
    return seedFake();
  })().finally(() => {
    g.__farmSeedInflight = undefined;
  });
  return g.__farmSeedInflight;
}

export function startDailySeed(): void {
  if (g.__farmSeedTimer) return;
  const loop = () => {
    g.__farmSeedTimer = setTimeout(() => {
      g.__farmSeedDay = undefined;
      void ensureDailySeed().finally(loop);
    }, msUntilShanghaiMidnight() + 800);
    g.__farmSeedTimer.unref?.();
  };
  void ensureDailySeed();
  loop();
}
