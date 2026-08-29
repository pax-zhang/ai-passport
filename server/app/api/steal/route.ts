import { canSteal, stealCoins, stealTakePct, yieldOf } from "@/lib/game";
import { json, readAuth, readBody } from "@/lib/http";
import { addMail, farmOf, MAIL_STEAL, publicFarm, putFarm, quotaOf, stealGate } from "@/lib/store";

export async function POST(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const body = await readBody<{ targetId?: number; plot?: number }>(req);
  const targetId = Number(body?.targetId ?? 0);
  const plot = Number(body?.plot ?? 0);
  if (!targetId) return json({ ok: false, err: "target" }, 400);
  if (targetId === me.id) return json({ ok: false, err: "self" }, 400);
  const you = await farmOf(targetId);
  if (!you) return json({ ok: false, err: "none" }, 404);
  const p = you.plots[plot];
  if (!p || !canSteal(p, you.level, plot)) {
    return json({ ok: false, err: "ripe" }, 409);
  }
  const gate = await stealGate(me.id, you.id);
  if (gate === "limit") return json({ ok: false, err: "limit" }, 409);
  if (gate === "cool") return json({ ok: false, err: "cool" }, 409);
  const take = stealTakePct(yieldOf(p));
  const coins = stealCoins(p);
  p.y = yieldOf(p) - take;
  p.x = 1;
  you.updatedAt = Math.floor(Date.now() / 1000);
  await putFarm(you);
  await addMail(you.id, me.id, MAIL_STEAL, coins);
  const q = await quotaOf(me.id);
  return json({ ok: true, ...q, got: coins, ...publicFarm(you) });
}
