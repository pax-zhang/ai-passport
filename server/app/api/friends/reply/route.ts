import { json, readAuth, readBody } from "@/lib/http";
import { linkFriends, takeReq } from "@/lib/store";

export async function POST(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const body = await readBody<{ id?: number; accept?: boolean }>(req);
  const id = Number(body?.id ?? 0);
  if (!(await takeReq(id, me.id))) return json({ ok: false, err: "none" }, 404);
  if (body?.accept) await linkFriends(me.id, id);
  return json({ ok: true });
}
