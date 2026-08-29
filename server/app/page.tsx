import { canSteal, plotN } from "@/lib/game";
import { ensureDailySeed } from "@/lib/seed";
import { isFakeFarm, listFarms, type Farm } from "@/lib/store";
import { SeedBtn } from "./seed-btn";

export const dynamic = "force-dynamic";

export default async function Page() {
  let farms: Farm[] = [];
  let dbErr = "";
  try {
    await ensureDailySeed();
    farms = (await listFarms()).sort((a, b) => b.coins - a.coins);
  } catch {
    dbErr = "连不上 MySQL。在 server 目录执行 docker compose up -d，再刷新。";
  }
  return (
    <main>
      <h1>FoloToy AI Passport Farm</h1>
      <p>设备在设置里填本机 <code>IP:3000</code>，用 Wi-Fi 注册后即可同步和偷菜。</p>
      {dbErr ? <p style={{ color: "#b00" }}>{dbErr}</p> : <p>已注册农场：{farms.length}</p>}
      <SeedBtn />
      <p style={{ color: "#666", fontSize: 14 }}>
        数据在 MySQL。假农场 ID 从 900001 起。随机做客会抽到有熟菜的；排行按金币。不挂好友。
      </p>
      {farms.length > 0 ? (
        <table cellPadding={6} style={{ borderCollapse: "collapse" }}>
          <thead>
            <tr>
              <th align="left">ID</th>
              <th align="left">名字</th>
              <th align="left">等级</th>
              <th align="left">金币</th>
              <th align="left">可偷</th>
              <th align="left"></th>
            </tr>
          </thead>
          <tbody>
            {farms.map((f) => {
              const ripe = f.plots.filter((p, i) => canSteal(p, f.level, i)).length;
              return (
                <tr key={f.id}>
                  <td><code>{f.id}</code></td>
                  <td>{f.name || "（未命名）"}</td>
                  <td>Lv{f.level}</td>
                  <td>${f.coins}</td>
                  <td>{ripe}/{plotN(f.level)}</td>
                  <td>{isFakeFarm(f) ? "假" : "真机"}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      ) : null}
      <ul>
        <li>POST /api/register</li>
        <li>GET/PUT /api/farm</li>
        <li>GET /api/farm/random</li>
        <li>GET /api/farm/:id</li>
        <li>POST /api/steal</li>
        <li>GET/POST /api/friends</li>
        <li>GET /api/inbox · GET /api/rank</li>
        <li>POST /api/seed</li>
      </ul>
    </main>
  );
}
