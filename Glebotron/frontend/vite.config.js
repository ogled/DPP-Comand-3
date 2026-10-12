import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

const BACKEND = "http://127.0.0.1:5001";
const DEV_ORIGINS = new Set(["http://localhost:5173", "http://127.0.0.1:5173"]);

// Сборка кладётся в ../static — её раздаёт Flask (python app.py) или nginx, Node на сервере не нужен.
// npm run dev — режим разработки на http://localhost:5173 с проксированием /api на app.py.
// Origin не удаляется: app.py отклоняет POST без Origin. Origin dev-сервера подменяется на адрес
// бэкенда, запрос с любого другого сайта получает 403 ещё в прокси.
const guardOrigin = (proxy) =>
  proxy.on("proxyReq", (proxyReq, req, res) => {
    const origin = req.headers.origin;
    if (origin && !DEV_ORIGINS.has(origin)) {
      res.writeHead(403, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ ok: false, error: "Запрос с чужого сайта отклонён" }));
      proxyReq.destroy();
      return;
    }
    if (origin) proxyReq.setHeader("origin", BACKEND);
  });

export default defineConfig({
  plugins: [react()],
  base: "/static/",
  build: { outDir: "../static", emptyOutDir: true },
  server: {
    proxy: {
      "/api": { target: BACKEND, changeOrigin: true, configure: guardOrigin },
    },
  },
});
