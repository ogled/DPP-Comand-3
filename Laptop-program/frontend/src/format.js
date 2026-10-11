export const MAX_PAYLOAD = 52;

export const num = (n) => Number(n ?? 0).toLocaleString("ru-RU");

export function bytesWord(n) {
  const a = Math.abs(n) % 100, b = a % 10;
  if (a > 10 && a < 20) return "байт";
  if (b >= 2 && b <= 4) return "байта";
  return "байт";
}

export const pad = (n, w = 2) => String(n).padStart(w, "0");
export const devName = (id) => `DEV-${pad(id ?? 0, 3)}`;
export const msgId = (id) => pad(id, 6);

/** Возраст в секундах → «0.6 с», «14 с», «3 мин», «2 ч». */
export function fmtAge(sec) {
  if (sec == null || !isFinite(sec)) return "—";
  if (sec < 0) sec = 0;
  if (sec < 10) return `${sec.toFixed(1)} с`;
  if (sec < 90) return `${Math.round(sec)} с`;
  if (sec < 5400) return `${Math.round(sec / 60)} мин`;
  return `${Math.round(sec / 3600)} ч`;
}

export function fmtDuration(sec) {
  if (sec == null || !isFinite(sec) || sec < 0) return "—";
  const s = Math.floor(sec);
  return `${pad(Math.floor(s / 3600))}:${pad(Math.floor(s / 60) % 60)}:${pad(s % 60)}`;
}

export function fmtClock(date, utc = false) {
  const h = utc ? date.getUTCHours() : date.getHours();
  const m = utc ? date.getUTCMinutes() : date.getMinutes();
  const s = utc ? date.getUTCSeconds() : date.getSeconds();
  return `${pad(h)}:${pad(m)}:${pad(s)}`;
}

export function fmtNumber(v) {
  if (typeof v !== "number" || !isFinite(v)) return String(v ?? "—");
  if (Number.isInteger(v)) return String(v);
  const a = Math.abs(v);
  return a >= 1000 ? v.toFixed(0) : a >= 10 ? v.toFixed(1) : v.toFixed(2);
}

/** «Гироскоп X» в группе «Гироскоп» → «X». */
export function shortName(name, group) {
  if (group && name.startsWith(group)) {
    const n = name.slice(group.length).replace(/^[\s:·-]+/, "");
    if (n) return n;
  }
  return name;
}

export function worstState(fields) {
  if (fields.some((f) => f.state === "bad")) return "bad";
  if (fields.some((f) => f.state === "warn")) return "warn";
  return "ok";
}
