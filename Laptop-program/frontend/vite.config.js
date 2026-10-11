import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Сборка кладётся в ../static — её раздаёт Flask (python app.py), Node для работы не нужен.
// npm run dev — режим разработки на http://localhost:5173 с проксированием /api на app.py.
export default defineConfig({
  plugins: [react()],
  base: "/static/",
  build: { outDir: "../static", emptyOutDir: true, chunkSizeWarningLimit: 900 },
  server: {
    proxy: {
      "/api": {
        target: "http://127.0.0.1:5000",
        changeOrigin: true,
        // app.py отклоняет POST с чужим Origin, а dev-сервер — другой origin
        configure: (proxy) => proxy.on("proxyReq", (req) => req.removeHeader("origin")),
      },
    },
  },
});
