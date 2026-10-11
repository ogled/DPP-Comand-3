import { useStation } from "../store.js";

export default function Toasts() {
  const toasts = useStation((s) => s.toasts);
  return (
    <div className="toasts" aria-live="polite">
      {toasts.map((t) => (
        <div key={t.id} className={`toast ${t.error ? "err" : ""}`}>{t.text}</div>
      ))}
    </div>
  );
}
