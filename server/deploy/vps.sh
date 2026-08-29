#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ ! -f .env ]]; then
  echo "先复制 .env.example 为 .env，填好 MYSQL_*"
  exit 1
fi

if ! command -v node >/dev/null; then
  echo "没找到 node。请装 Node 20+："
  echo "  curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -"
  echo "  sudo apt-get install -y nodejs"
  exit 1
fi

if [[ ! -f package-lock.json ]]; then
  echo "缺少 package-lock.json，请从本机 server/ 一并上传（不要只传 package.json）"
  exit 1
fi

echo "node $(node -v)  npm $(npm -v)"

NODE_MAJOR=$(node -p "parseInt(process.versions.node, 10)")
NPM_MAJOR=$(npm -v | cut -d. -f1)
if (( NODE_MAJOR < 20 || NPM_MAJOR < 9 )); then
  echo
  echo "现在的 Node/npm 太旧，读不了这份 package-lock（lockfileVersion 3）。"
  echo "Cannot read property 'mysql2' of undefined 就是这个原因。"
  echo
  echo "请装 Node 22 后再跑本脚本："
  echo "  curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -"
  echo "  sudo apt-get install -y nodejs"
  echo "  node -v   # 应是 v22.x"
  echo "  npm -v    # 应是 10.x"
  exit 1
fi

npm ci
npm run build

if command -v pm2 >/dev/null; then
  PM2=pm2
else
  PM2="npx pm2"
fi

if $PM2 describe farm >/dev/null 2>&1; then
  $PM2 reload farm --update-env
else
  $PM2 start ecosystem.config.cjs
fi
$PM2 save

echo
echo "看日志:  $PM2 logs farm"
echo "假数据:  curl -sS -X POST http://127.0.0.1:3000/api/seed"
echo "开机自启（只做一次）:  $PM2 startup   然后执行它打印的 sudo 命令，再 $PM2 save"
