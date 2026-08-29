import { NextResponse } from "next/server";
import { authFarm, type Farm } from "./store";

export function json(data: unknown, status = 200) {
  const body = JSON.stringify(data);
  return new NextResponse(body, {
    status,
    headers: {
      "Content-Type": "application/json",
      "Content-Length": String(Buffer.byteLength(body)),
      Connection: "close",
    },
  });
}

export async function readAuth(req: Request): Promise<Farm | undefined> {
  return authFarm(req.headers.get("authorization"));
}

export async function readBody<T>(req: Request): Promise<T | null> {
  try {
    return (await req.json()) as T;
  } catch {
    return null;
  }
}
