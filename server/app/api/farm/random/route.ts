import { canSteal } from "@/lib/game";
import { json, readAuth } from "@/lib/http";
import { ensureDailySeed } from "@/lib/seed";
import { listRecent, publicFarm, quotaOf } from "@/lib/store";

export async function GET(req: Request) {
  await ensureDailySeed();
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const now = Math.floor(Date.now() / 1000);
  const list = await listRecent(me.id, now - 7 * 24 * 3600);
  if (list.length === 0) return json({ ok: false, err: "none" }, 404);
  const ripe = list.filter((f) =>
    f.plots.some((p, i) => canSteal(p, f.level, i))
  );
  const pool = ripe.length ? ripe : list;
  const pick = pool[Math.floor(Math.random() * pool.length)];
  const q = await quotaOf(me.id);
  return json({ ok: true, ...q, ...publicFarm(pick) });
}
