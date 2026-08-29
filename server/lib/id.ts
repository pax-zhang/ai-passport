/** FNV-1a 32-bit, same as firmware app_farm_id_from_mac().
 *  Vector: AA:BB:CC:DD:EE:FF → 239978
 */
export function idFromMac(mac: string): number {
  const parts = mac.trim().split(/[:\-]/);
  if (parts.length !== 6) return 0;
  const bytes = parts.map((p) => parseInt(p, 16));
  if (bytes.some((n) => Number.isNaN(n) || n < 0 || n > 255)) return 0;
  let h = 2166136261;
  for (const b of bytes) {
    h ^= b;
    h = Math.imul(h, 16777619) >>> 0;
  }
  return 100000 + (h % 900000);
}

export function macNorm(mac: string): string | null {
  const parts = mac.trim().split(/[:\-]/);
  if (parts.length !== 6) return null;
  const bytes = parts.map((p) => parseInt(p, 16));
  if (bytes.some((n) => Number.isNaN(n) || n < 0 || n > 255)) return null;
  return bytes
    .map((b) => b.toString(16).toUpperCase().padStart(2, "0"))
    .join(":");
}

export function token(): string {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}
