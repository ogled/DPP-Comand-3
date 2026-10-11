import { useEffect } from "react";
import { useStation, usePersistent, connect, disconnect, loadPorts } from "../store.js";

const BAUDS = [9600, 19200, 38400, 57600, 115200];
const PARITIES = [
  ["N", "Нет (8N1)"],
  ["E", "Чётная (8E1)"],
  ["O", "Нечётная (8O1)"],
];

export default function Connection() {
  const ports = useStation((s) => s.ports);
  const connected = useStation((s) => s.connected);
  const online = useStation((s) => s.online);
  const [port, setPort] = usePersistent("dpp.port", "");
  const [baud, setBaud] = usePersistent("dpp.baud", 115200);
  const [parity, setParity] = usePersistent("dpp.parity", "E");

  // если запомненного порта нет в списке — берём первый реальный (DEMO последний)
  useEffect(() => {
    if (ports.length && !ports.some((p) => p.device === port)) setPort(ports[0].device);
  }, [ports]); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <section className="panel">
      <header className="panel-head">
        <h2>Подключение</h2>
      </header>
      <div className="panel-body form">
        <label className="field">
          <span>Порт</span>
          <div className="field-row">
            <select value={port} onChange={(e) => setPort(e.target.value)} disabled={connected}>
              {ports.map((p) => (
                <option key={p.device} value={p.device}>
                  {p.device}
                  {p.description ? ` — ${p.description}` : ""}
                </option>
              ))}
            </select>
            <button className="btn icon" onClick={loadPorts} disabled={connected} title="Обновить список портов" aria-label="Обновить список портов">
              <svg width="15" height="15" viewBox="0 0 15 15" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round"><path d="M12.5 7.5a5 5 0 1 1-1.6-3.7M12.5 2v2.8H9.7" /></svg>
            </button>
          </div>
        </label>
        <div className="field-pair">
          <label className="field">
            <span>Скорость, бод</span>
            <select value={baud} onChange={(e) => setBaud(Number(e.target.value))} disabled={connected}>
              {BAUDS.map((b) => <option key={b} value={b}>{b}</option>)}
            </select>
          </label>
          <label className="field">
            <span>Чётность</span>
            <select value={parity} onChange={(e) => setParity(e.target.value)} disabled={connected}>
              {PARITIES.map(([v, l]) => <option key={v} value={v}>{l}</option>)}
            </select>
          </label>
        </div>
        {connected ? (
          <button className="btn wide" onClick={disconnect}>Закрыть порт</button>
        ) : (
          <button className="btn primary wide" onClick={() => connect(port, baud, parity)} disabled={!online || !port}>
            Открыть порт
          </button>
        )}
      </div>
    </section>
  );
}
