import { readFileSync } from "node:fs";
import { join } from "node:path";
import mysql, { type Pool, type PoolConnection, type ResultSetHeader, type RowDataPacket } from "mysql2/promise";

const g = globalThis as typeof globalThis & { __farmPool?: Pool; __farmReady?: Promise<void> };

function env(name: string, fallback: string): string {
  const v = process.env[name];
  return v !== undefined ? v : fallback;
}

export function pool(): Pool {
  if (!g.__farmPool) {
    g.__farmPool = mysql.createPool({
      host: env("MYSQL_HOST", "127.0.0.1"),
      port: Number(env("MYSQL_PORT", "3306")),
      user: env("MYSQL_USER", "farm"),
      password: env("MYSQL_PASSWORD", "farm"),
      database: env("MYSQL_DATABASE", "farm"),
      charset: "utf8mb4",
      waitForConnections: true,
      connectionLimit: 8,
      enableKeepAlive: true,
    });
  }
  return g.__farmPool;
}

export async function readyDb(): Promise<void> {
  if (!g.__farmReady) g.__farmReady = initDb();
  await g.__farmReady;
}

async function initDb(): Promise<void> {
  const sql = readFileSync(join(process.cwd(), "lib", "schema.sql"), "utf8");
  const stmts = sql
    .split(";")
    .map((s) => s.trim())
    .filter((s) => s.length > 0);
  for (const s of stmts) await pool().query(s);
  await migrateFarms();
  const tables = await query<RowDataPacket>("SHOW TABLES");
  console.log(
    "[farm] schema ready",
    tables.map((r) => String(Object.values(r)[0])).join(", ")
  );
}

async function migrateFarms(): Promise<void> {
  const db = env("MYSQL_DATABASE", "farm");
  const cols = await query<RowDataPacket>(
    `SELECT COLUMN_NAME AS n FROM information_schema.COLUMNS
     WHERE TABLE_SCHEMA = ? AND TABLE_NAME = 'farms'`,
    [db]
  );
  const have = new Set(cols.map((r) => String(r.n)));
  if (have.size === 0) return;
  if (!have.has("friends")) {
    await pool().query(
      "ALTER TABLE farms ADD COLUMN friends JSON NOT NULL DEFAULT (JSON_ARRAY())"
    );
  }
}

export type QueryConn = Pool | PoolConnection;

export async function query<T extends RowDataPacket>(
  sql: string,
  params: unknown[] = [],
  conn: QueryConn = pool()
): Promise<T[]> {
  const [rows] = await conn.query<T[]>(sql, params);
  return rows;
}

export async function exec(
  sql: string,
  params: unknown[] = [],
  conn: QueryConn = pool()
): Promise<ResultSetHeader> {
  const [ret] = await conn.query<ResultSetHeader>(sql, params);
  return ret;
}
