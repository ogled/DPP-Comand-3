# Интерфейс наземной станции (React)

Готовая сборка лежит в `../static` и раздаётся сервером: для работы достаточно `python app.py`, Node не нужен.

Менять интерфейс:

```
cd Laptop-program/frontend
npm install        # один раз
npm run dev        # http://localhost:5173, /api проксируется на app.py (он должен быть запущен)
npm run build      # пересобрать ../static перед коммитом
```

- `src/store.js` — опрос `/api/state`, действия и история значений для графиков.
- `src/components/MccView.jsx` — режим ЦУП: табло, приборы, связь, журнал.
- `src/components/MonitorView.jsx`, `Inspector.jsx` — режим «Монитор»: лента и разбор по байтам.
- `src/styles/tokens.css` — цвета дневной и ночной темы.

Что выводится на табло и в приборах, задаётся в `../protocol.json`: `kpi: true` — поле на табло, `group` — прибор, поля X/Y/Z группы рисуются как оси.
