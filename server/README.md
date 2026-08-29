# Farm server

Next.js API for FoloToy AI Passport farm sync, steal, friends, and rank. State lives in MySQL.

## 本机联调

```bash
cd server
cp .env.example .env
npm install
# 本机已有 MySQL（如 DBngin）时建库：
#   mysql -uroot -e "CREATE DATABASE farm; CREATE USER 'farm'@'localhost' IDENTIFIED BY 'farm'; GRANT ALL ON farm.* TO 'farm'@'localhost';"
# 没有 MySQL 且装了 Docker：npm run db:up
npm run dev
```

Listen on `0.0.0.0:3000`. The device firmware does not expose a server field; point it at this host by changing `APP_FARM_HOST_DEFAULT` and rebuilding.

## VPS（PM2）

一台 Ubuntu 22.04/24.04，安全组放行 **22** 和 **3000**。不要对公网开放 3306。

```bash
# 1. Node 22（不要用系统自带的 Node 10/12/14，否则 npm ci 会报 Cannot read property 'mysql2' of undefined）
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
sudo apt-get install -y nodejs
node -v && npm -v    # 期望 v22.x / 10.x

# 2. MySQL（5.7 / 8 都可以；JSON 列需要 ≥ 5.7.8）
# 已有本机 MySQL 时只需建库建用户，不要对公网开放 3306。
# MySQL 5.7：
#   mysql -uroot -p -e "CREATE DATABASE farm CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
#   mysql -uroot -p -e "CREATE USER 'farm'@'localhost' IDENTIFIED BY '强密码'; GRANT ALL ON farm.* TO 'farm'@'localhost'; FLUSH PRIVILEGES;"
# MySQL 8：
sudo apt-get install -y mysql-server
sudo mysql <<'SQL'
CREATE DATABASE IF NOT EXISTS farm CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'farm'@'localhost' IDENTIFIED BY '换成强密码';
GRANT ALL PRIVILEGES ON farm.* TO 'farm'@'localhost';
FLUSH PRIVILEGES;
SQL

# 3. PM2
sudo npm i -g pm2

# 4. 上传 server/（本机执行）
# rsync -av --exclude node_modules --exclude .next \
#   ./server/ user@VPS_IP:~/farm-server/

# 5. 在 VPS 上
cd ~/farm-server
cp .env.example .env
nano .env
# MYSQL_HOST=127.0.0.1
# MYSQL_USER=farm
# MYSQL_PASSWORD=换成强密码
# MYSQL_DATABASE=farm

chmod +x deploy/vps.sh
./deploy/vps.sh
pm2 startup          # 按提示执行那条 sudo 命令
pm2 save
curl -sS -X POST http://127.0.0.1:3000/api/seed
```

设备固定连 **`https://farm.netbiu.com`**。浏览器打开该地址应能看到农场列表。

更新后再 `rsync` 一次，然后 `./deploy/vps.sh`（会 `npm ci`、`build`、`pm2 reload farm`）。

常用：`pm2 status` · `pm2 logs farm` · `pm2 restart farm`。

有域名时用 Caddy / nginx 反代到 `127.0.0.1:3000`。设备固件里的地址在 `APP_FARM_HOST_DEFAULT`。

## 假数据

Open `http://主机:3000` and click **生成假农场** (or `npm run seed` while the app is up). This writes 24 dummy farms (`900001`–`900024`). Real device farms are kept. Fake farms are not friended. Re-running replaces only the fake rows.

Tables are created on first request (`lib/schema.sql`). If `data/farm.json` still exists and the database is empty, it is imported once.

Device ID is FNV-1a of the Wi-Fi STA MAC, mapped to `100000–999999`. The same algorithm lives in `main/app_farm_logic.c` and `lib/id.ts`.
