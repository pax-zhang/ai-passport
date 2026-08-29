export async function register() {
  if (process.env.NEXT_RUNTIME !== "nodejs") return;
  const { startDailySeed } = await import("./lib/seed");
  startDailySeed();
}
