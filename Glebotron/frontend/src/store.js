/* Состояние клиента: вход, диалоги, сообщения и опрос /api/updates?since=. Компоненты читают его через useStore(). */
import { useEffect, useState, useSyncExternalStore } from "react";
import { get, post } from "./api.js";

const POLL_VISIBLE_MS = 5000;
const POLL_HIDDEN_MS = 30000;

const initial = {
  phase: "loading", // loading | auth | ready
  me: null,
  status: null,
  seq: 0,
  online: true,
  radioLeft: null,
  convs: [], // [{ peer, last }], свежие сверху
  threads: {}, // address → { items: [] по возрастанию id, more, loaded }
  directory: [],
  active: null, // address открытого диалога
  peers: {}, // address → { address, name, kind, seen }
  toasts: [],
};
let state = initial;

const listeners = new Set();
function set(patch) {
  state = { ...state, ...(typeof patch === "function" ? patch(state) : patch) };
  listeners.forEach((l) => l());
}
const subscribe = (l) => (listeners.add(l), () => listeners.delete(l));

export function useStore(selector) {
  return useSyncExternalStore(subscribe, () => selector(state));
}
export const getState = () => state;

/** Перерисовка раз в interval мс — для «N мин назад». */
export function useTick(interval = 30000) {
  const [, setT] = useState(0);
  useEffect(() => {
    const id = setInterval(() => setT((t) => t + 1), interval);
    return () => clearInterval(id);
  }, [interval]);
}

/* ---------- уведомления ---------- */
let toastId = 0;
export function toast(text, kind = "error") {
  const id = ++toastId;
  set((s) => ({ toasts: [...s.toasts, { id, text, kind }] }));
  setTimeout(() => set((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) })), 5000);
}

/* ---------- слияние сообщений ---------- */
function mergeMessages(s, msgs) {
  if (!msgs.length) return {};
  const threads = { ...s.threads };
  const peers = { ...s.peers };
  const lastByPeer = new Map(s.convs.map((c) => [c.peer.address, c]));
  for (const m of msgs) {
    const peer = m.out ? m.to : m.from;
    peers[peer.address] = { ...peers[peer.address], ...peer };
    const th = threads[m.peer];
    if (th) {
      const items = th.items.slice();
      const i = items.findIndex((x) => x.id === m.id);
      if (i >= 0) {
        if (m.updated_at >= items[i].updated_at) items[i] = m;
      } else {
        items.push(m);
        items.sort((a, b) => a.id - b.id);
      }
      threads[m.peer] = { ...th, items };
    }
    const conv = lastByPeer.get(m.peer);
    if (!conv || !conv.last || conv.last.id < m.id || (conv.last.id === m.id && m.updated_at >= conv.last.updated_at)) {
      lastByPeer.set(m.peer, { peer: peers[m.peer] || peer, last: m });
    }
  }
  const convs = [...lastByPeer.values()].sort((a, b) => (b.last?.id ?? 0) - (a.last?.id ?? 0));
  return { threads, peers, convs };
}

/* ---------- маршрут: #123 — открытый диалог (кнопка «назад» на телефоне работает сама) ---------- */
function readHash() {
  const m = location.hash.match(/^#(\d{1,5})$/);
  return m ? Number(m[1]) : null;
}

export function openChat(address) {
  if (address == null) {
    if (location.hash) history.pushState(null, "", location.pathname);
    set({ active: null });
    return;
  }
  if (readHash() !== address) history.pushState(null, "", `#${address}`);
  set({ active: address });
  if (!state.threads[address]) loadThread(address);
}

window.addEventListener("popstate", () => {
  const a = readHash();
  set({ active: a });
  if (a != null && !state.threads[a]) loadThread(a);
});

/* ---------- загрузка ---------- */
function signedOut() {
  stopPolling();
  set({ ...initial, phase: "auth", toasts: state.toasts });
}

function handleAuthFail(r) {
  if (r.httpStatus === 401 || r.httpStatus === 403) {
    signedOut();
    return true;
  }
  return false;
}

export async function boot() {
  const r = await get("/api/me");
  if (!r.ok) {
    if (r.offline) {
      set({ phase: "auth", online: false });
      return;
    }
    set({ phase: "auth" });
    return;
  }
  await start(r);
}

async function start(r) {
  set({
    phase: "ready", me: r.me, status: r.status, seq: r.me.seq,
    radioLeft: r.me.limits.radio_left_today, online: true,
  });
  await Promise.all([loadConversations(), loadDirectory()]);
  const a = readHash();
  if (a != null) openChat(a);
  startPolling();
}

async function loadConversations() {
  const r = await get("/api/conversations");
  if (!r.ok) return handleAuthFail(r);
  const peers = { ...state.peers };
  for (const c of r.items) peers[c.peer.address] = c.peer;
  set((s) => ({ peers, ...mergeMessages({ ...s, peers, convs: [] }, r.items.map((c) => c.last).reverse()) }));
}

export async function loadDirectory() {
  const r = await get("/api/directory");
  if (r.ok) set({ directory: r.items });
}

export async function loadThread(address, older = false) {
  const th = state.threads[address];
  const before = older && th?.items.length ? `&before=${th.items[0].id}` : "";
  const r = await get(`/api/messages?peer=${address}${before}`);
  if (!r.ok) {
    if (!handleAuthFail(r)) toast(r.error);
    return;
  }
  set((s) => {
    const cur = s.threads[address];
    const known = new Map((cur?.items || []).map((m) => [m.id, m]));
    for (const m of r.items) if (!known.has(m.id) || m.updated_at >= known.get(m.id).updated_at) known.set(m.id, m);
    const items = [...known.values()].sort((a, b) => a.id - b.id);
    return {
      threads: { ...s.threads, [address]: { items, more: older || !cur ? r.more : cur.more, loaded: true } },
      peers: { ...s.peers, [address]: { ...s.peers[address], ...r.peer } },
    };
  });
}

/* ---------- опрос ---------- */
let timer = null;
let busy = false;

async function poll() {
  if (busy || state.phase !== "ready") return;
  busy = true;
  let again = false;
  try {
    const r = await get(`/api/updates?since=${state.seq}`);
    if (!r.ok) {
      if (!handleAuthFail(r)) set({ online: !r.offline });
      return;
    }
    if (r.reset) {
      // база на сервере пересоздана: номера изменений начались заново, перечитываем всё
      set({ seq: r.seq, status: r.status, convs: [], threads: {}, peers: {} });
      await Promise.all([loadConversations(), loadDirectory()]);
      if (state.active != null) loadThread(state.active);
      return;
    }
    set((s) => ({
      online: true, seq: r.seq, status: r.status, radioLeft: r.radio_left_today,
      ...mergeMessages(s, r.messages),
    }));
    // кадры от терминала (сообщение или ACK) обновляют «был в эфире» в справочнике
    if (r.messages.some((m) => m.via === "radio" && (!m.out || m.status === "delivered"))) loadDirectory();
    again = r.more;
  } finally {
    busy = false;
  }
  if (again) poll();
}

function schedule() {
  clearTimeout(timer);
  timer = setTimeout(async () => {
    await poll();
    schedule();
  }, document.hidden ? POLL_HIDDEN_MS : POLL_VISIBLE_MS);
}

function startPolling() {
  schedule();
}

function stopPolling() {
  clearTimeout(timer);
  timer = null;
}

document.addEventListener("visibilitychange", () => {
  if (state.phase !== "ready") return;
  if (!document.hidden) poll();
  schedule();
});

/* ---------- действия ---------- */
export async function signIn(mode, username, password) {
  const r = await post(mode === "register" ? "/api/register" : "/api/login", { username, password });
  if (!r.ok) return r.error || "Ошибка входа";
  await start(r);
  return null;
}

export async function signOut() {
  await post("/api/logout");
  if (location.hash) history.replaceState(null, "", location.pathname);
  signedOut();
}

export async function send(address, text) {
  const r = await post("/api/messages", { to: address, text });
  if (!r.ok) {
    if (!handleAuthFail(r)) toast(r.error);
    return false;
  }
  set((s) => {
    const patch = mergeMessages(s, [r.message]);
    if (r.message.via === "radio" && s.radioLeft != null) patch.radioLeft = Math.max(0, s.radioLeft - 1);
    if (r.queue_position != null && s.status) patch.status = { ...s.status, queue: Math.max(s.status.queue, r.queue_position) };
    return patch;
  });
  return true;
}

export async function lookup(q) {
  const r = await get(`/api/lookup?q=${encodeURIComponent(q)}`);
  if (!r.ok) {
    if (!handleAuthFail(r)) toast(r.error);
    return null;
  }
  set((s) => ({ peers: { ...s.peers, [r.peer.address]: r.peer } }));
  return r.peer;
}
