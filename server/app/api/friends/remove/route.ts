import { json, readAuth, readBody } from "@/lib/http";
import { removeFriend } from "@/lib/store";

export async function POST(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const body = await readBody<{ id?: number }>(req);
  const id = Number(body?.id ?? 0);
  await removeFriend(me.id, id);
  return json({ ok: true });
}
