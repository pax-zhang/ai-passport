const { join } = require("node:path");

module.exports = {
  apps: [
    {
      name: "farm",
      cwd: __dirname,
      script: join(__dirname, "node_modules", "next", "dist", "bin", "next"),
      args: "start -p 3000 --hostname 0.0.0.0",
      instances: 1,
      exec_mode: "fork",
      autorestart: true,
      max_memory_restart: "300M",
      env: {
        NODE_ENV: "production",
      },
    },
  ],
};
