import { json, readAuth, readBody } from "@/lib/http";
import { addReq, farmOf, farmsByIds, linkFriends, peerOf, takeReq } from "@/lib/store";

export async function GET(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const list = (await farmsByIds(me.friends))
    .map(peerOf)
    .sort((a, b) => b.coins - a.coins || b.level - a.level);
  return json({ ok: true, list });
}

export async function POST(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const body = await readBody<{ id?: number }>(req);
  const id = Number(body?.id ?? 0);
  if (id < 100000 || id > 999999) return json({ ok: false, err: "id" }, 400);
  if (id === me.id) return json({ ok: false, err: "self" }, 400);
  const you = await farmOf(id);
  if (!you) return json({ ok: false, err: "none" }, 404);
  if (me.friends.includes(id)) return json({ ok: true, already: true });
  if (you.friends.includes(me.id) || (await takeReq(id, me.id))) {
    await linkFriends(me.id, id);
    return json({ ok: true, linked: true });
  }
  await addReq(me.id, id);
  return json({ ok: true, sent: true });
}
