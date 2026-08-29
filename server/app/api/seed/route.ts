import { json } from "@/lib/http";
import { seedFake } from "@/lib/seed";

export async function POST() {
  const r = await seedFake();
  return json({ ok: true, ...r });
}
