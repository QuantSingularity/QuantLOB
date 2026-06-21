import { defineConfig, loadEnv } from "vite";
import react from "@vitejs/plugin-react";

// In development Vite proxies /api to the C++ server; in production the same
// server serves these built assets, so the relative /api paths just work.
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, ".", "");
  return {
    plugins: [react()],
    server: {
      port: 5173,
      proxy: {
        "/api": {
          target: env.QUANTLOB_API || "http://127.0.0.1:8080",
          changeOrigin: true,
        },
      },
    },
    build: { outDir: "dist", sourcemap: false },
  };
});
