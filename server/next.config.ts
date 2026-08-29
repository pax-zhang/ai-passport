import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  output: "standalone",
  serverExternalPackages: ["mysql2", "iconv-lite"],
};

export default nextConfig;
