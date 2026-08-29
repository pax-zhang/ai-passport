import { readFileSync } from "node:fs";
import { join } from "node:path";
import type { RowDataPacket } from "mysql2/promise";
import {
  COOL_SEC,
  HELP_DAY_MAX,
  STEAL_DAY_MAX,
  STEAL_MAX,
  emptyPlots,
  shanghaiYmd,
  type Plot,
} from "./game";
import { exec, pool, query, readyDb, type QueryConn } from "./db";

export type Farm = {
  id: number;
  mac: string;
  token: string;
  name: string;
  level: number;
  xp: number;
  coins: number;
  pick: number;
  seeds: number[];
  plots: Plot[];
  friends: number[];
  updatedAt: number;
};

export type FriendReq = { from: number; to: number; at: number };

export const MAIL_FRIEND = 0;
export const MAIL_STEAL = 1;
export const MAIL_WATER = 2;
export const MAIL_WEED = 3;
export const MAIL_PEST = 4;
export const MAIL_MAX = 16;

export type Mail = { from: number; kind: number; got: number; at: number };

type MailRow = RowDataPacket & {
  from_id: number;
  kind: number;
  got: number;
  at_sec: number;
};

type FarmRow = RowDataPacket & {
  id: number;
  mac: string;
  token: string;
  name: string;
  level: number;
  xp: number;
  coins: number;
  pick: number;
  seeds: unknown;
  plots: unknown;
  friends: unknown;
  updated_at: number;
};

type ReqRow = RowDataPacket & { from_id: number; to_id: number; at_sec: number };
type CoolRow = RowDataPacket & { at_sec: number; cool_n: number };

function asJson<T>(v: unknown, fallback: T): T {
  if (v == null) return fallback;
  if (typeof v === "string") {
    try {
      return JSON.parse(v) as T;
    } catch {
      return fallback;
    }
  }
  return v as T;
}

function rowFarm(r: FarmRow): Farm {
  return {
    id: r.id,
    mac: r.mac,
    token: r.token,
    name: r.name,
    level: r.level,
    xp: r.xp,
    coins: r.coins,
    pick: r.pick,
    seeds: asJson(r.seeds, [4, 0, 0, 0, 0, 0]),
    plots: asJson(r.plots, emptyPlots()),
    friends: asJson(r.friends, []),
    updatedAt: r.updated_at,
  };
}

export function isFakeFarm(f: Farm): boolean {
  return f.mac.startsWith("FA:KE:") || f.token.startsWith("fake-");
}

export function newFarm(id: number, mac: string, token: string): Farm {
  return {
    id,
    mac,
    token,
    name: "",
    level: 1,
    xp: 0,
    coins: 80,
    pick: 0,
    seeds: [4, 0, 0, 0, 0, 0],
    plots: emptyPlots(),
    friends: [],
    updatedAt: Math.floor(Date.now() / 1000),
  };
}

export function publicFarm(f: Farm) {
  return {
    id: f.id,
    name: f.name,
    level: f.level,
    xp: f.xp,
    coins: f.coins,
    pick: f.pick,
    seeds: f.seeds,
    plots: f.plots,
    friends: f.friends,
  };
}

export function peerOf(f: Farm) {
  return { id: f.id, name: f.name, level: f.level, coins: f.coins };
}

let migrated = false;

async function boot(): Promise<void> {
  await readyDb();
  if (migrated) return;
  migrated = true;
  await migrateJsonOnce();
}

async function farmOfConn(id: number, conn: QueryConn): Promise<Farm | undefined> {
  const rows = await query<FarmRow>("SELECT * FROM farms WHERE id = ? LIMIT 1", [id], conn);
  return rows[0] ? rowFarm(rows[0]) : undefined;
}

async function putFarmConn(farm: Farm, conn: QueryConn): Promise<void> {
  await exec(
    `INSERT INTO farms
      (id, mac, token, name, level, xp, coins, pick, seeds, plots, friends, updated_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
     ON DUPLICATE KEY UPDATE
      mac = VALUES(mac), token = VALUES(token), name = VALUES(name),
      level = VALUES(level), xp = VALUES(xp), coins = VALUES(coins),
      pick = VALUES(pick), seeds = VALUES(seeds), plots = VALUES(plots),
      friends = VALUES(friends), updated_at = VALUES(updated_at)`,
    [
      farm.id,
      farm.mac,
      farm.token,
      farm.name,
      farm.level,
      farm.xp,
      farm.coins,
      farm.pick,
      JSON.stringify(farm.seeds),
      JSON.stringify(farm.plots),
      JSON.stringify(farm.friends),
      farm.updatedAt,
    ],
    conn
  );
}

export async function farmOf(id: number): Promise<Farm | undefined> {
  await boot();
  return farmOfConn(id, pool());
}

export async function putFarm(farm: Farm): Promise<void> {
  await boot();
  await putFarmConn(farm, pool());
}

export async function listFarms(): Promise<Farm[]> {
  await boot();
  const rows = await query<FarmRow>("SELECT * FROM farms");
  return rows.map(rowFarm);
}

export async function listRecent(exceptId: number, since: number): Promise<Farm[]> {
  await boot();
  const rows = await query<FarmRow>(
    "SELECT * FROM farms WHERE id <> ? AND updated_at >= ?",
    [exceptId, since]
  );
  return rows.map(rowFarm);
}

export async function listRank(limit = 16): Promise<Farm[]> {
  await boot();
  const rows = await query<FarmRow>(
    "SELECT * FROM farms ORDER BY coins DESC, level DESC LIMIT ?",
    [limit]
  );
  return rows.map(rowFarm);
}

export async function farmsByIds(ids: number[]): Promise<Farm[]> {
  await boot();
  if (ids.length === 0) return [];
  const ph = ids.map(() => "?").join(",");
  const rows = await query<FarmRow>(`SELECT * FROM farms WHERE id IN (${ph})`, ids);
  const map = new Map(rows.map((r) => [r.id, rowFarm(r)]));
  return ids.map((id) => map.get(id)).filter((f): f is Farm => !!f);
}

export async function authFarm(header: string | null): Promise<Farm | undefined> {
  if (!header) return undefined;
  const tok = header.startsWith("Bearer ") ? header.slice(7).trim() : header.trim();
  if (!tok) return undefined;
  await boot();
  const rows = await query<FarmRow>("SELECT * FROM farms WHERE token = ? LIMIT 1", [tok]);
  return rows[0] ? rowFarm(rows[0]) : undefined;
}

export async function addFriend(meId: number, friendId: number): Promise<void> {
  await boot();
  const me = await farmOf(meId);
  if (!me) return;
  if (me.friends.includes(friendId)) return;
  me.friends.push(friendId);
  await putFarm(me);
}

export async function removeFriend(meId: number, friendId: number): Promise<void> {
  await boot();
  const me = await farmOf(meId);
  if (!me) return;
  me.friends = me.friends.filter((id) => id !== friendId);
  await putFarm(me);
}

export async function addMail(
  to: number,
  from: number,
  kind: number,
  got = 0
): Promise<void> {
  await boot();
  const at = Math.floor(Date.now() / 1000);
  await exec(
    "INSERT INTO mail (to_id, from_id, kind, got, at_sec) VALUES (?, ?, ?, ?, ?)",
    [to, from, kind, got, at]
  );
  if (kind === MAIL_FRIEND) return;
  await exec(
    `DELETE FROM mail WHERE to_id = ? AND kind <> 0 AND id NOT IN (
       SELECT id FROM (
         SELECT id FROM mail WHERE to_id = ? AND kind <> 0 ORDER BY id DESC LIMIT ?
       ) t
     )`,
    [to, to, MAIL_MAX]
  );
}

export async function listMail(to: number): Promise<Mail[]> {
  await boot();
  const rows = await query<MailRow>(
    "SELECT from_id, kind, got, at_sec FROM mail WHERE to_id = ? ORDER BY id DESC LIMIT ?",
    [to, MAIL_MAX]
  );
  const out: Mail[] = rows.map((r) => ({
    from: r.from_id,
    kind: r.kind,
    got: r.got,
    at: r.at_sec,
  }));
  const reqs = await inbox(to);
  const have = new Set(out.filter((m) => m.kind === MAIL_FRIEND).map((m) => m.from));
  for (const r of reqs) {
    if (!have.has(r.from)) {
      out.push({ from: r.from, kind: MAIL_FRIEND, got: 0, at: r.at });
    }
  }
  out.sort((a, b) => b.at - a.at);
  return out.slice(0, MAIL_MAX);
}

export async function addReq(from: number, to: number): Promise<void> {
  await boot();
  const r = await exec(
    `INSERT IGNORE INTO friend_reqs (from_id, to_id, at_sec) VALUES (?, ?, ?)`,
    [from, to, Math.floor(Date.now() / 1000)]
  );
  if (r.affectedRows) await addMail(to, from, MAIL_FRIEND);
}

export async function takeReq(from: number, to: number): Promise<FriendReq | undefined> {
  await boot();
  const rows = await query<ReqRow>(
    "SELECT from_id, to_id, at_sec FROM friend_reqs WHERE from_id = ? AND to_id = ? LIMIT 1",
    [from, to]
  );
  if (!rows[0]) return undefined;
  await exec("DELETE FROM friend_reqs WHERE from_id = ? AND to_id = ?", [from, to]);
  await exec("DELETE FROM mail WHERE from_id = ? AND to_id = ? AND kind = ?", [
    from,
    to,
    MAIL_FRIEND,
  ]);
  return { from: rows[0].from_id, to: rows[0].to_id, at: rows[0].at_sec };
}

export async function inbox(to: number): Promise<FriendReq[]> {
  await boot();
  const rows = await query<ReqRow>(
    "SELECT from_id, to_id, at_sec FROM friend_reqs WHERE to_id = ?",
    [to]
  );
  return rows.map((r) => ({ from: r.from_id, to: r.to_id, at: r.at_sec }));
}

export async function linkFriends(a: number, b: number): Promise<void> {
  await boot();
  const conn = await pool().getConnection();
  try {
    await conn.beginTransaction();
    const fa = await farmOfConn(a, conn);
    const fb = await farmOfConn(b, conn);
    if (!fa || !fb) {
      await conn.rollback();
      return;
    }
    if (!fa.friends.includes(b)) fa.friends.push(b);
    if (!fb.friends.includes(a)) fb.friends.push(a);
    await putFarmConn(fa, conn);
    await putFarmConn(fb, conn);
    await conn.commit();
  } catch (e) {
    await conn.rollback();
    throw e;
  } finally {
    conn.release();
  }
}

export async function unlinkFriends(a: number, b: number): Promise<void> {
  await boot();
  const conn = await pool().getConnection();
  try {
    await conn.beginTransaction();
    const fa = await farmOfConn(a, conn);
    const fb = await farmOfConn(b, conn);
    if (fa) {
      fa.friends = fa.friends.filter((id) => id !== b);
      await putFarmConn(fa, conn);
    }
    if (fb) {
      fb.friends = fb.friends.filter((id) => id !== a);
      await putFarmConn(fb, conn);
    }
    await conn.commit();
  } catch (e) {
    await conn.rollback();
    throw e;
  } finally {
    conn.release();
  }
}

async function getCool(conn: QueryConn, key: string): Promise<{ at: number; n: number } | undefined> {
  const rows = await query<CoolRow>(
    "SELECT at_sec, cool_n FROM steal_cool WHERE cool_key = ? FOR UPDATE",
    [key],
    conn
  );
  return rows[0] ? { at: Number(rows[0].at_sec), n: Number(rows[0].cool_n) || 0 } : undefined;
}

async function putCool(conn: QueryConn, key: string, at: number, n: number): Promise<void> {
  await exec(
    `INSERT INTO steal_cool (cool_key, at_sec, cool_n) VALUES (?, ?, ?)
     ON DUPLICATE KEY UPDATE at_sec = VALUES(at_sec), cool_n = VALUES(cool_n)`,
    [key, at, n],
    conn
  );
}

async function dayUsed(key: string, day: number): Promise<number> {
  const rows = await query<CoolRow>(
    "SELECT at_sec, cool_n FROM steal_cool WHERE cool_key = ? LIMIT 1",
    [key]
  );
  if (!rows[0] || Number(rows[0].at_sec) !== day) return 0;
  return Number(rows[0].cool_n) || 0;
}

export async function quotaOf(id: number): Promise<{ qs: number; qw: number; qg: number; qp: number }> {
  await boot();
  const day = shanghaiYmd();
  return {
    qs: Math.max(0, STEAL_DAY_MAX - (await dayUsed(`day:${id}`, day))),
    qw: Math.max(0, HELP_DAY_MAX - (await dayUsed(`day:${id}:w`, day))),
    qg: Math.max(0, HELP_DAY_MAX - (await dayUsed(`day:${id}:g`, day))),
    qp: Math.max(0, HELP_DAY_MAX - (await dayUsed(`day:${id}:p`, day))),
  };
}

export async function helpGate(id: number, kind: "w" | "g" | "p"): Promise<"ok" | "limit"> {
  await boot();
  const conn = await pool().getConnection();
  try {
    await conn.beginTransaction();
    const day = shanghaiYmd();
    const dayKey = `day:${id}:${kind}`;
    await exec(
      "INSERT IGNORE INTO steal_cool (cool_key, at_sec, cool_n) VALUES (?, ?, 0)",
      [dayKey, day],
      conn
    );
    const dayRow = await getCool(conn, dayKey);
    if (dayRow && dayRow.at === day && dayRow.n >= HELP_DAY_MAX) {
      await conn.rollback();
      return "limit";
    }
    if (!dayRow || dayRow.at !== day) await putCool(conn, dayKey, day, 1);
    else await putCool(conn, dayKey, day, dayRow.n + 1);
    await conn.commit();
    return "ok";
  } catch (e) {
    await conn.rollback();
    throw e;
  } finally {
    conn.release();
  }
}

export async function stealGate(thief: number, target: number): Promise<"ok" | "cool" | "limit"> {
  await boot();
  const conn = await pool().getConnection();
  try {
    await conn.beginTransaction();
    const now = Math.floor(Date.now() / 1000);
    const day = shanghaiYmd();
    const dayKey = `day:${thief}`;
    await exec(
      "INSERT IGNORE INTO steal_cool (cool_key, at_sec, cool_n) VALUES (?, ?, 0)",
      [dayKey, day],
      conn
    );
    const dayRow = await getCool(conn, dayKey);
    if (dayRow && dayRow.at === day && dayRow.n >= STEAL_DAY_MAX) {
      await conn.rollback();
      return "limit";
    }

    const key = `${thief}:${target}`;
    await exec(
      "INSERT IGNORE INTO steal_cool (cool_key, at_sec, cool_n) VALUES (?, ?, 0)",
      [key, 0],
      conn
    );
    const row = await getCool(conn, key);
    if (row && now - row.at < COOL_SEC && row.n >= STEAL_MAX) {
      await conn.rollback();
      return "cool";
    }

    if (!row || now - row.at >= COOL_SEC) await putCool(conn, key, now, 1);
    else await putCool(conn, key, row.at, row.n + 1);

    if (!dayRow || dayRow.at !== day) await putCool(conn, dayKey, day, 1);
    else await putCool(conn, dayKey, day, dayRow.n + 1);

    await conn.commit();
    return "ok";
  } catch (e) {
    await conn.rollback();
    throw e;
  } finally {
    conn.release();
  }
}

export async function deleteFakeFarms(): Promise<void> {
  await boot();
  await exec("DELETE FROM farms WHERE mac LIKE 'FA:KE:%' OR token LIKE 'fake-%'");
  await exec(
    `DELETE r FROM friend_reqs r
     LEFT JOIN farms a ON a.id = r.from_id
     LEFT JOIN farms b ON b.id = r.to_id
     WHERE a.id IS NULL OR b.id IS NULL`
  );
  await exec(
    `DELETE m FROM mail m
     LEFT JOIN farms a ON a.id = m.from_id
     LEFT JOIN farms b ON b.id = m.to_id
     WHERE a.id IS NULL OR b.id IS NULL`
  );
}

export async function migrateJsonOnce(): Promise<number> {
  const n = await query<RowDataPacket>("SELECT COUNT(*) AS n FROM farms");
  if (Number(n[0]?.n ?? 0) > 0) return 0;
  try {
    const raw = readFileSync(join(process.cwd(), "data", "farm.json"), "utf8");
    const db = JSON.parse(raw) as {
      farms?: Record<string, Farm>;
      reqs?: FriendReq[];
      cool?: { key: string; at: number; n: number }[];
    };
    const farms = Object.values(db.farms ?? {});
    for (const f of farms) await putFarm(f);
    for (const r of db.reqs ?? []) {
      await exec(
        "INSERT IGNORE INTO friend_reqs (from_id, to_id, at_sec) VALUES (?, ?, ?)",
        [r.from, r.to, r.at]
      );
    }
    for (const c of db.cool ?? []) {
      await exec(
        `INSERT INTO steal_cool (cool_key, at_sec, cool_n) VALUES (?, ?, ?)
         ON DUPLICATE KEY UPDATE at_sec = VALUES(at_sec), cool_n = VALUES(cool_n)`,
        [c.key, c.at, c.n]
      );
    }
    return farms.length;
  } catch {
    return 0;
  }
}
