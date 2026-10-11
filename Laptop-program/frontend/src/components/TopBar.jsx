import { useState } from "react";
import { useStation, useTick, nowSec } from "../store.js";
import { fmtClock, fmtDuration } from "../format.js";

function Clocks() {
  useTick(1000);
  const connectedAt = useStation((s) => s.connectedAt);
  const connected = useStation((s) => s.connected);
  const d = new Date(nowSec() * 1000);
  return (
    <div className="clocks" aria-label="Время">
      <div className="clock">
        <span className="clock-val">{fmtClock(d)}</span>
        <span className="clock-cap">местное</span>
      </div>
      <div className="clock">
        <span className="clock-val">{fmtClock(d, true)}</span>
        <span className="clock-cap">UTC</span>
      </div>
      <div className="clock">
        <span className="clock-val">{connected && connectedAt ? fmtDuration(nowSec() - connectedAt) : "--:--:--"}</span>
        <span className="clock-cap">сеанс</span>
      </div>
    </div>
  );
}

function PortChip() {
  const online = useStation((s) => s.online);
  const connected = useStation((s) => s.connected);
  const port = useStation((s) => s.port);
  const baud = useStation((s) => s.baud);
  const parity = useStation((s) => s.parity);
  let cls = "off", title = "Порт закрыт", sub = null;
  if (!online) {
    cls = "bad";
    title = "Сервер не отвечает";
    sub = "запустите python app.py";
  } else if (connected) {
    cls = "on";
    title = port;
    sub = port === "DEMO" ? "имитация" : `${baud} 8${parity}1`;
  }
  return (
    <div className={`port-chip ${cls}`} role="status">
      <i className="dot" />
      <span className="port-chip-t">{title}</span>
      {sub && <span className="port-chip-s">{sub}</span>}
    </div>
  );
}

export default function TopBar({ mode, onMode, onProtocol }) {
  const [theme, setTheme] = useState(() => document.documentElement.dataset.theme || "day");
  const toggleTheme = () => {
    const next = theme === "night" ? "day" : "night";
    document.documentElement.dataset.theme = next;
    try {
      localStorage.setItem("theme", next);
    } catch {
      /* без хранилища тема просто не запомнится */
    }
    setTheme(next);
  };

  return (
    <header className="topbar">
      <div className="brand">
        <svg className="brand-mark" width="26" height="26" viewBox="0 0 26 26" aria-hidden="true">
          <circle cx="13" cy="13" r="11.5" fill="none" stroke="currentColor" strokeWidth="1.5" />
          <ellipse cx="13" cy="13" rx="11.5" ry="4.2" fill="none" stroke="currentColor" strokeWidth="1.1" opacity=".55" transform="rotate(-24 13 13)" />
          <circle cx="21.6" cy="9.2" r="2.2" fill="currentColor" />
        </svg>
        <div className="brand-text">
          <b>DPP</b>
          <span>Наземная станция</span>
        </div>
      </div>

      <nav className="modes" aria-label="Режим">
        <button className={mode === "mcc" ? "on" : ""} onClick={() => onMode("mcc")}>ЦУП</button>
        <button className={mode === "mon" ? "on" : ""} onClick={() => onMode("mon")}>Монитор</button>
      </nav>

      <Clocks />

      <div className="topbar-end">
        <PortChip />
        <button className="btn quiet" onClick={onProtocol}>Протокол</button>
        <a className="btn quiet" href="/api/export" download>Экспорт</a>
        <button className="btn quiet icon" onClick={toggleTheme} title={theme === "night" ? "Дневная тема" : "Ночная тема"} aria-label="Сменить тему">
          {theme === "night" ? (
            <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round"><circle cx="8" cy="8" r="3" /><path d="M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6M3.4 3.4l1.1 1.1M11.5 11.5l1.1 1.1M3.4 12.6l1.1-1.1M11.5 4.5l1.1-1.1" /></svg>
          ) : (
            <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinejoin="round"><path d="M13.3 9.2A5.6 5.6 0 0 1 6.8 2.7a5.6 5.6 0 1 0 6.5 6.5Z" /></svg>
          )}
        </button>
      </div>
    </header>
  );
}
