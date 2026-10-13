/* Состояние станции: опрос app.py и действия. Компоненты читают его через useStation(). */
import { useEffect, useState, useSyncExternalStore } from "react";

const MAX_RECORDS = 5000;
const SERIES_MAX = 3600; // точек на параметр (час при 1 Гц)
const POLL_MS = 400;

let state = {
  online: true,
  records: [],
  last: 0,
  connected: false,
  port: null,
  baud: null,
  parity: "N",
  connectedAt: null,
  stats: { rx: 0, tx: 0, rx_bytes: 0, tx_bytes: 0, errors: 0, tm_unmatched: 0 },
  telemetry: [],
  clockOffset: 0,
  protoVersion: null,
  protoError: "",
  commands: [],
  links: [],
  link: null,
  ports: [],
  seriesRev: 0,
  toasts: [],
};

/* Ряды значений копятся на клиенте из приходящих пакетов (для графиков). */
const series = new Map(); // "frame:device:поле" → { t: [], v: [] }
const arrivals = new Map(); // "frame:device" → [ts, ...] — для оценки периода

const listeners = new Set();
function set(patch) {
  state = { ...state, ...patch };
  listeners.forEach((l) => l());
}
const subscribe = (l) => (listeners.add(l), () => listeners.delete(l));

export function useStation(selector) {
  return useSyncExternalStore(subscribe, () => selector(state));
}
export const getState = () => state;

/** Серверное «сейчас» в секундах (Unix). */
export const nowSec = () => Date.now() / 1000 + state.clockOffset;

/** Перерисовка раз в interval мс — для часов и «N с назад». */
export function useTick(interval = 1000) {
  const [, setT] = useState(0);
  useEffect(() => {
    const id = setInterval(() => setT((t) => t + 1), interval);
    return () => clearInterval(id);
  }, [interval]);
}

export const unitKey = (frame, device) => `${frame}:${device}`;

export function getSeries(unit, field) {
  return series.get(`${unit}:${field}`) || { t: [], v: [] };
}

/** Медианный интервал между пакетами устройства, с. */
export function getPeriod(unit) {
  const a = arrivals.get(unit);
  if (!a || a.length < 3) return null;
  const d = [];
  for (let i = 1; i < a.length; i++) d.push(a[i] - a[i - 1]);
  d.sort((x, y) => x - y);
  return d[Math.floor(d.length / 2)];
}

function ingest(recs) {
  let touched = false;
  for (const r of recs) {
    if (r.kind !== "rx" || !r.telemetry || !r.ts) continue;
    const unit = unitKey(r.telemetry.card ?? r.telemetry.frame, r.device_id);
    const arr = arrivals.get(unit) || [];
    arr.push(r.ts);
    if (arr.length > 30) arr.splice(0, arr.length - 30);
    arrivals.set(unit, arr);
    for (const f of r.telemetry.fields) {
      if (typeof f.value !== "number") continue;
      const key = `${unit}:${f.name}`;
      let s = series.get(key);
      if (!s) series.set(key, (s = { t: [], v: [] }));
      s.t.push(r.ts);
      s.v.push(f.value);
      if (s.t.length > SERIES_MAX + 200) {
        s.t.splice(0, s.t.length - SERIES_MAX);
        s.v.splice(0, s.v.length - SERIES_MAX);
      }
    }
    touched = true;
  }
  return touched;
}

/* ---------- сеть ---------- */
async function post(path, body = {}) {
  try {
    const r = await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    return await r.json();
  } catch {
    return { ok: false, error: "Сервер не отвечает. Запустите python app.py" };
  }
}

let busy = false;
export async function poll() {
  if (busy) return;
  busy = true;
  try {
    const d = await fetch(`/api/state?since=${state.last}`).then((r) => r.json());
    let records = state.records;
    if (d.reset) {
      records = [];
      series.clear();
      arrivals.clear();
    }
    const patch = {
      online: true,
      connected: d.connected,
      port: d.port,
      baud: d.baud,
      parity: d.parity || "N",
      connectedAt: d.connected_at ?? null,
      link: d.link ?? null,
      stats: d.stats,
      telemetry: d.telemetry || [],
      clockOffset: d.now ? d.now - Date.now() / 1000 : 0,
      protoError: d.proto_error || "",
    };
    if (d.records.length || d.reset) {
      records = records.concat(d.records);
      if (records.length > MAX_RECORDS) records = records.slice(records.length - MAX_RECORDS);
      patch.records = records;
      patch.last = d.last;
      if (ingest(d.records)) patch.seriesRev = state.seriesRev + 1;
    }
    if (d.proto_version !== state.protoVersion) {
      patch.protoVersion = d.proto_version;
      loadProtocol();
    }
    set(patch);
  } catch {
    if (state.online) set({ online: false });
  }
  busy = false;
}

let pollTimer = null;
export function startPolling() {
  if (pollTimer) return () => {};
  loadPorts();
  poll();
  pollTimer = setInterval(poll, POLL_MS);
  return () => {
    clearInterval(pollTimer);
    pollTimer = null;
  };
}

export async function loadPorts() {
  try {
    const ports = await fetch("/api/ports").then((r) => r.json());
    set({ ports });
  } catch {
    set({ online: false });
  }
}

export async function loadProtocol() {
  try {
    const d = await fetch("/api/protocol").then((r) => r.json());
    set({ commands: d.commands || [], links: d.links || [], protoError: d.error || "" });
    return d;
  } catch {
    return null;
  }
}

export async function saveProtocol(text) {
  const res = await post("/api/protocol", { text });
  if (res.ok) poll();
  return res;
}

export async function connect(port, baud, parity, link) {
  const res = await post("/api/connect", { port, baud, parity, link });
  if (!res.ok) toast(res.error || "Не удалось открыть порт", true);
  poll();
}

export async function disconnect() {
  await post("/api/disconnect");
  poll();
}

export async function sendText(text) {
  const res = await post("/api/send", { text });
  if (!res.ok) toast(res.error || "Строка не отправлена", true);
  poll();
  return res.ok;
}

export async function sendAlarm() {
  const res = await post("/api/alarm");
  if (res.ok) toast("Тревога отправлена");
  else toast(res.error || "Тревога не отправлена", true);
  poll();
}

export async function sendCommand(cmd, params) {
  const res = await post("/api/command", { id: cmd.id, params });
  if (res.ok) toast(`Команда «${cmd.name}» отправлена`);
  else toast(res.error || `Команда «${cmd.name}» не отправлена`, true);
  poll();
  return res.ok;
}

export async function clearJournal() {
  await post("/api/clear");
  set({ records: [] });
}

/* ---------- уведомления ---------- */
let toastId = 0;
export function toast(text, error = false) {
  const id = ++toastId;
  set({ toasts: [...state.toasts, { id, text, error }] });
  setTimeout(() => set({ toasts: state.toasts.filter((t) => t.id !== id) }), error ? 6000 : 3500);
}

/* ---------- настройки интерфейса ---------- */
export function usePersistent(key, initial) {
  const [value, setValue] = useState(() => {
    try {
      const v = localStorage.getItem(key);
      return v === null ? initial : JSON.parse(v);
    } catch {
      return initial;
    }
  });
  const update = (v) => {
    setValue(v);
    try {
      localStorage.setItem(key, JSON.stringify(v));
    } catch {
      /* хранилище недоступно — настройка просто не запомнится */
    }
  };
  return [value, update];
}
