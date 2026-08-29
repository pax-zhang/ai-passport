import { plotN } from "@/lib/game";
import { json, readAuth, readBody } from "@/lib/http";
import {
  addMail,
  farmOf,
  helpGate,
  MAIL_PEST,
  MAIL_WATER,
  MAIL_WEED,
  publicFarm,
  putFarm,
  quotaOf,
} from "@/lib/store";

export async function POST(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const body = await readBody<{ targetId?: number; plot?: number; act?: number }>(req);
  const targetId = Number(body?.targetId ?? 0);
  const plot = Number(body?.plot ?? 0);
  const act = Number(body?.act ?? 0);
  if (!targetId) return json({ ok: false, err: "target" }, 400);
  if (targetId === me.id) return json({ ok: false, err: "self" }, 400);
  if (act < 1 || act > 3) return json({ ok: false, err: "act" }, 400);
  const you = await farmOf(targetId);
  if (!you) return json({ ok: false, err: "none" }, 404);
  if (plot < 0 || plot >= plotN(you.level)) {
    return json({ ok: false, err: "plot" }, 409);
  }
  const p = you.plots[plot];
  if (!p || p.s === 0 || p.s === 4) return json({ ok: false, err: "empty" }, 409);
  if (act === 1 && !p.d) return json({ ok: false, err: "dry" }, 409);
  if (act === 2 && !p.w) return json({ ok: false, err: "weed" }, 409);
  if (act === 3 && !p.p) return json({ ok: false, err: "pest" }, 409);
  const kind = act === 1 ? "w" : act === 2 ? "g" : "p";
  const gate = await helpGate(me.id, kind);
  if (gate === "limit") return json({ ok: false, err: "limit" }, 409);
  if (act === 1) p.d = 0;
  else if (act === 2) p.w = 0;
  else p.p = 0;
  you.updatedAt = Math.floor(Date.now() / 1000);
  await putFarm(you);
  await addMail(
    you.id,
    me.id,
    act === 1 ? MAIL_WATER : act === 2 ? MAIL_WEED : MAIL_PEST
  );
  const q = await quotaOf(me.id);
  return json({ ok: true, ...q, ...publicFarm(you) });
}
