import { json } from "@/lib/http";
import { ensureDailySeed } from "@/lib/seed";
import { listRank, peerOf } from "@/lib/store";

export async function GET() {
  await ensureDailySeed();
  const list = (await listRank(10)).map(peerOf);
  return json({ ok: true, list });
}
