const pad = (n) => String(n).padStart(2, "0");
const enc = new TextEncoder();

/** Время сообщения: «14:32» сегодня, «вчера 14:32», «12.10 14:32». */
export function fmtTime(ts) {
  if (!ts) return "";
  const d = new Date(ts * 1000);
  const now = new Date();
  const hm = `${pad(d.getHours())}:${pad(d.getMinutes())}`;
  const day = (x) => new Date(x.getFullYear(), x.getMonth(), x.getDate()).getTime();
  const diff = Math.round((day(now) - day(d)) / 86400000);
  if (diff === 0) return hm;
  if (diff === 1) return `вчера ${hm}`;
  return `${pad(d.getDate())}.${pad(d.getMonth() + 1)} ${hm}`;
}

/** «только что», «12 мин назад», «3 ч назад», «12.10 14:32». */
export function fmtAgo(ts, now = Date.now() / 1000) {
  if (!ts) return "—";
  const s = Math.max(0, now - ts);
  if (s < 60) return "только что";
  if (s < 3600) return `${Math.round(s / 60)} мин назад`;
  if (s < 86400) return `${Math.round(s / 3600)} ч назад`;
  return fmtTime(ts);
}

/** Байты UTF-8 и число радиокадров — так же, как codec.split_utf8 на сервере. */
export function measure(text, frameText) {
  let bytes = 0, frames = 0, cur = 0;
  for (const ch of text) {
    const n = enc.encode(ch).length;
    bytes += n;
    if (cur + n > frameText) {
      frames++;
      cur = 0;
    }
    cur += n;
  }
  if (cur) frames++;
  return { bytes, frames };
}

/** Текст так, как его сохранит сервер (app.clean_text). */
export function cleanText(text) {
  return text
    .replace(/\r\n?/g, "\n")
    .replace(/[\u0000-\u0008\u000B-\u001F\u007F]/g, "")
    .trim();
}

export function plural(n, one, few, many) {
  const a = Math.abs(n) % 100, b = a % 10;
  if (a > 10 && a < 20) return many;
  if (b === 1) return one;
  if (b >= 2 && b <= 4) return few;
  return many;
}

export const STATUS = {
  sent: { label: "отправлено", icon: "check" },
  queued: { label: "в очереди на спутник", icon: "clock" },
  uplinked: { label: "ЦУП передаёт на спутник", icon: "antenna" },
  on_satellite: { label: "на спутнике, ждёт пролёта над терминалом", icon: "sat" },
  delivered: { label: "доставлено, терминал подтвердил", icon: "check2" },
  received: { label: "получено через спутник", icon: "antenna" },
  expired: { label: "не доставлено за 7 дней", icon: "warn" },
  failed: { label: "не удалось передать", icon: "warn" },
};
