import { json, readAuth } from "@/lib/http";
import { farmsByIds, listMail } from "@/lib/store";

export async function GET(req: Request) {
  const me = await readAuth(req);
  if (!me) return json({ ok: false, err: "auth" }, 401);
  const rows = await listMail(me.id);
  const farms = await farmsByIds([...new Set(rows.map((r) => r.from))]);
  const names = new Map(farms.map((f) => [f.id, f.name]));
  return json({
    ok: true,
    list: rows.map((r) => ({
      kind: r.kind,
      from: r.from,
      name: names.get(r.from) ?? "",
      got: r.got,
    })),
  });
}
