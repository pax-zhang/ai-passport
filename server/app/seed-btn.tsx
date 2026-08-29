"use client";

import { useState } from "react";

export function SeedBtn() {
  const [msg, setMsg] = useState("");

  async function onClick() {
    setMsg("生成中…");
    const r = await fetch("/api/seed", { method: "POST" });
    const j = (await r.json()) as { ok?: boolean; ids?: number[] };
    if (!j.ok) {
      setMsg("失败");
      return;
    }
    setMsg(`已写入 ${j.ids?.length ?? 0} 个假农场`);
    location.reload();
  }

  return (
    <p>
      <button type="button" onClick={onClick}>
        生成假农场
      </button>
      {msg ? <span style={{ marginLeft: 12 }}>{msg}</span> : null}
    </p>
  );
}
