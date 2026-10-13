import { useState } from "react";
import { toast } from "../store.js";
import { bytesWord, devName, msgId, num } from "../format.js";

function segValue(r, s) {
  if (s.value !== undefined) return [s.value, s.hint || ""];
  switch (s.key) {
    case "dev": return [devName(r.device_id), `ID ${r.device_id}`];
    case "lon": return [`${(r.longitude / 1e6).toFixed(6)}°`, `${num(r.longitude)} × 10⁻⁶`];
    case "lat": return [`${(r.latitude / 1e6).toFixed(6)}°`, `${num(r.latitude)} × 10⁻⁶`];
    case "type": return [r.type, r.type_name?.split(" / ")[0]];
    case "pack": return [`№ ${r.packet_number}, вход ${r.type_input}`, "старшие 4 бита — номер"];
    case "size": return [`${r.size} ${bytesWord(r.size)}`, "длина данных"];
    case "payload": return [r.size ? `${r.size} ${bytesWord(r.size)}` : "пусто", ""];
    case "tail": {
      const n = r.tail_hex ? r.tail_hex.split(" ").length : 0;
      return [`${n} ${bytesWord(n)}`, "лишнее после данных"];
    }
    default: return ["", ""];
  }
}

async function copy(text) {
  try {
    await navigator.clipboard.writeText(text);
    toast("Скопировано");
  } catch {
    toast("Буфер обмена недоступен", true);
  }
}

export default function Inspector({ r }) {
  const [hl, setHl] = useState(null);

  if (!r) {
    return (
      <section className="panel inspector">
        <header className="panel-head"><h2>Разбор</h2></header>
        <p className="empty-note">Выберите запись в ленте — здесь появится её разбор по байтам. Стрелки ↑ ↓ листают ленту.</p>
      </section>
    );
  }

  const head = (
    <header className="panel-head">
      <h2>
        Запись {msgId(r.id)}
        <span className="panel-meta">{r.time}</span>
      </h2>
      <span className={`tag t-${r.kind === "rx" && !r.valid ? "bad" : r.kind}`}>
        {{ rx: r.valid ? "Приём" : "Битый пакет", tx: "Отправка", sys: "Система", err: "Ошибка" }[r.kind]}
      </span>
    </header>
  );

  if (r.kind === "sys" || r.kind === "err") {
    return (
      <section className="panel inspector">
        {head}
        <div className="panel-body"><pre className={`payload ${r.kind === "err" ? "alarm" : ""}`}>{r.text}</pre></div>
      </section>
    );
  }

  const bytes = (r.raw_hex || "").split(" ").filter(Boolean);
  const owner = new Array(bytes.length).fill("raw");
  (r.segments || []).forEach((s) => {
    for (let i = s.start; i < s.start + s.len && i < bytes.length; i++) owner[i] = s.key;
  });

  return (
    <section className="panel inspector">
      {head}
      {r.kind === "rx" && (
        <div className="isec">
          <h3>Байты пакета <span className="panel-meta">{bytes.length} {bytesWord(bytes.length)}</span></h3>
          <div className={`bytes ${hl ? "hl" : ""}`}>
            {bytes.map((b, i) => (
              <span key={i} className={`b s-${owner[i]} ${hl === owner[i] ? "on" : ""}`} title={`байт ${i}`}>{b}</span>
            ))}
          </div>
          {r.valid ? (
            <dl className="segs" onMouseLeave={() => setHl(null)}>
              {(r.segments || []).map((s) => {
                const [v, hint] = segValue(r, s);
                return (
                  <div key={s.key} className="seg-row" onMouseEnter={() => setHl(s.key)}>
                    <i className={`sw s-${s.key}`} />
                    <dt>{s.name}</dt>
                    <dd>{v}{hint && <small>{hint}</small>}</dd>
                  </div>
                );
              })}
            </dl>
          ) : (
            <p className="note bad">Пакет короче заголовка или длина данных не совпадает. Поля разобрать нельзя.</p>
          )}
        </div>
      )}

      {r.telemetry && (
        <div className="isec">
          <h3>Телеметрия <span className="panel-meta">{r.telemetry.name}</span></h3>
          <dl className="tm-list">
            {r.telemetry.fields.map((f) => (
              <div key={f.name} title={f.note || undefined}>
                <dt>{f.name}</dt>
                <dd className={`st-${f.state}`}>{f.text}{f.unit && <small>{f.unit}</small>}</dd>
              </div>
            ))}
          </dl>
        </div>
      )}

      {(r.kind === "tx" || (r.valid && !r.telemetry)) && (
        <div className="isec">
          <h3>Данные</h3>
          <pre className={`payload ${r.alarm ? "alarm" : ""}`}>{r.text || "—"}</pre>
        </div>
      )}

      <div className="isec">
        <h3>HEX</h3>
        <div className="hexbox">{r.raw_hex}</div>
        <div className="isec-actions">
          <button className="btn small" onClick={() => copy(r.raw_hex)}>Копировать пакет</button>
          {r.frame_hex && r.frame_hex !== r.raw_hex && (
            <button className="btn small" onClick={() => copy(r.frame_hex)}>Копировать кадр порта</button>
          )}
        </div>
      </div>
    </section>
  );
}
