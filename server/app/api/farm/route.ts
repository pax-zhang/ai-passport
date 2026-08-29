import { json, readAuth, readBody } from "@/lib/http";
import { emptyPlots, mergePlot, PLOT_N, type Plot } from "@/lib/game";
import { publicFarm, putFarm } from "@/lib/store";

type Body = {
  name?: string;
  level?: number;
  xp?: number;
  coins?: number;
  pick?: number;
  seeds?: number[];
  plots?: Plot[];
  friends?: number[];
};

export async function GET(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  return json({ ok: true, ...publicFarm(me) });
}

export async function PUT(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const body = await readBody<Body>(req);
  if (!body) return json({ ok: false, err: "body" }, 400);

  if (typeof body.name === "string") me.name = body.name.slice(0, 12);
  if (typeof body.level === "number" && body.level >= 1 && body.level <= 99) {
    me.level = Math.floor(body.level);
  }
  if (typeof body.xp === "number" && body.xp >= 0) me.xp = Math.floor(body.xp);
  if (typeof body.coins === "number" && body.coins >= 0) {
    me.coins = Math.floor(body.coins);
  }
  if (typeof body.pick === "number") me.pick = Math.max(0, Math.min(5, body.pick));
  if (Array.isArray(body.seeds) && body.seeds.length >= 6) {
    me.seeds = body.seeds.slice(0, 6).map((n) => Math.max(0, Math.min(999, n | 0)));
  }
  if (Array.isArray(body.plots)) {
    const next = emptyPlots();
    for (let i = 0; i < PLOT_N && i < body.plots.length; i++) {
      const p = body.plots[i];
      next[i] = {
        c: (p.c | 0) & 7,
        s: Math.max(0, Math.min(4, p.s | 0)),
        d: p.d ? 1 : 0,
        w: p.w ? 1 : 0,
        p: p.p ? 1 : 0,
        x: p.x ? 1 : 0,
        y: Math.max(0, Math.min(100, p.y != null ? (p.y | 0) : p.s ? 100 : 0)),
        g: Math.max(0, p.g | 0),
        n: Math.max(0, Math.min(255, p.n != null ? (p.n | 0) : 0)),
      };
      next[i] = mergePlot(me.plots[i] ?? emptyPlots()[i], next[i]);
    }
    me.plots = next;
  }
  if (Array.isArray(body.friends)) {
    const incoming = body.friends.filter((id) => id >= 100000 && id <= 999999);
    me.friends = [...new Set([...me.friends, ...incoming])];
  }
  me.updatedAt = Math.floor(Date.now() / 1000);
  await putFarm(me);
  return json({ ok: true, ...publicFarm(me) });
}
