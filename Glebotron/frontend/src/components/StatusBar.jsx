import { useState } from "react";
import { fmtAgo, fmtTime } from "../format.js";
import { signOut, toast, useStore, useTick } from "../store.js";
import Icon from "./Icon.jsx";
import Logo from "./Logo.jsx";

function Session({ status }) {
  useTick(30000);
  if (!status) return null;
  const { last_aos: aos, last_los: los } = status;
  let text = "сеансов ещё не было";
  if (aos && (!los || los < aos)) text = `идёт сеанс с ${fmtTime(aos)}`;
  else if (aos && los) text = `последний сеанс ${fmtTime(aos)}–${fmtTime(los).split(" ").pop()}`;
  else if (los) text = `последний сеанс ${fmtAgo(los)}`;
  return <span className="chip-sub">{text}</span>;
}

export default function StatusBar() {
  const me = useStore((s) => s.me);
  const status = useStore((s) => s.status);
  const online = useStore((s) => s.online);
  const [theme, setTheme] = useState(() => document.documentElement.dataset.theme || "day");

  const toggleTheme = () => {
    const next = theme === "night" ? "day" : "night";
    document.documentElement.dataset.theme = next;
    try {
      localStorage.setItem("glebotron-theme", next);
    } catch {
      /* без localStorage тема просто не запомнится */
    }
    setTheme(next);
  };

  const copyAddress = async () => {
    try {
      await navigator.clipboard.writeText(String(me.address));
      toast(`Номер #${me.address} скопирован`, "info");
    } catch {
      toast(`Ваш номер: #${me.address}`, "info");
    }
  };

  const station = !online ? "bad" : status?.station_online ? "on" : "off";
  return (
    <header className="topbar">
      <div className="brand">
        <Logo />
        <b>Глеботрон</b>
      </div>

      <div className="topbar-chips">
        <div className={`chip ${station}`} role="status" title="ЦУП — наземная станция, которая говорит со спутником">
          <i className="dot" />
          <span>{!online ? "нет связи с сервером" : status?.station_online ? "ЦУП на связи" : "ЦУП не на связи"}</span>
          {online && <Session status={status} />}
        </div>
        {status && (
          <div className="chip" title="Сообщения, которые ждут передачи на спутник">
            <Icon name="clock" size={14} />
            <span>в очереди {status.queue}</span>
          </div>
        )}
      </div>

      <div className="topbar-end">
        <button className="btn me-chip" onClick={copyAddress} title="Скопировать свой номер — его можно продиктовать владельцу терминала">
          <span className="me-name">{me.username}</span>
          <span className="me-addr">#{me.address}</span>
          <Icon name="copy" size={14} />
        </button>
        <button className="btn quiet icon" onClick={toggleTheme} title={theme === "night" ? "Дневная тема" : "Ночная тема"}>
          <Icon name={theme === "night" ? "sun" : "moon"} />
        </button>
        <button className="btn quiet icon" onClick={signOut} title="Выйти">
          <Icon name="exit" />
        </button>
      </div>
    </header>
  );
}
