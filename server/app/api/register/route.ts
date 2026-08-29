import { idFromMac, macNorm, token } from "@/lib/id";
import { json, readBody } from "@/lib/http";
import { farmOf, newFarm, putFarm } from "@/lib/store";

export async function POST(req: Request) {
  try {
    const body = await readBody<{ mac?: string; id?: number }>(req);
    if (!body?.mac) return json({ ok: false, err: "mac" }, 400);
    const mac = macNorm(body.mac);
    if (!mac) return json({ ok: false, err: "mac" }, 400);
    const id = idFromMac(mac);
    if (!id) return json({ ok: false, err: "id" }, 400);

    const exist = await farmOf(id);
    if (exist && exist.mac !== mac) return json({ ok: false, err: "taken" }, 409);
    if (exist) {
      exist.updatedAt = Math.floor(Date.now() / 1000);
      await putFarm(exist);
      console.log("[farm] register hit", id, mac);
      return json({ ok: true, token: exist.token, id });
    }
    const farm = newFarm(id, mac, token());
    await putFarm(farm);
    console.log("[farm] register new", id, mac);
    return json({ ok: true, token: farm.token, id });
  } catch (e) {
    console.error("[farm] register", e);
    return json({ ok: false, err: "db" }, 500);
  }
}
