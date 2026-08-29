import { json, readAuth } from "@/lib/http";
import { ensureDailySeed } from "@/lib/seed";
import { farmOf, publicFarm, quotaOf } from "@/lib/store";

export async function GET(
  req: Request,
  ctx: { params: Promise<{ id: string }> }
) {
  await ensureDailySeed();
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const { id } = await ctx.params;
  const farm = await farmOf(Number(id));
  if (!farm) return json({ ok: false, err: "none" }, 404);
  const q = await quotaOf(me.id);
  return json({ ok: true, ...q, ...publicFarm(farm) });
}
