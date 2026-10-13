import { useEffect, useMemo, useState } from "react";
import { useStation, useTick, usePersistent, nowSec, getPeriod, getSeries } from "../store.js";
import { fmtAge, fmtNumber, num, devName, shortName, worstState } from "../format.js";
import Commands from "./Commands.jsx";
import Trend from "./Trend.jsx";

/* ---------- связь ---------- */
function linkOf(unit, connected) {
  if (!connected) return { lamp: "off", age: unit ? nowSec() - unit.ts : null, period: unit ? getPeriod(unit.key) : null };
  if (!unit) return { lamp: "off", age: null, period: null };
  const age = nowSec() - unit.ts;
  const period = getPeriod(unit.key);
  const p = Math.max(period ?? 5, 0.5);
  const lamp = age <= p * 2.5 + 0.5 ? "green" : age <= p * 6 + 2 ? "amber" : "red";
  return { lamp, age, period };
}

function lampOf(f, stale) {
  if (stale || f.state === "err") return "off";
  if (f.type === "bool") return f.value ? "green" : "off";
  if (!f.rated) return "white";
  return { ok: "green", warn: "amber", bad: "red" }[f.state] || "off";
}

/* ---------- табло ---------- */
function Lamp({ id, lamp, name, value, unitText, title, selected, flash, onClick }) {
  return (
    <button className={`lamp is-${lamp} ${flash ? "flash" : ""} ${selected ? "sel" : ""}`} onClick={() => onClick?.(id)} title={title}>
      <span className="lamp-name">{name}</span>
      <span className="lamp-val">
        {value}
        {unitText && <small>{unitText}</small>}
      </span>
    </button>
  );
}

function Annunciator({ unit, link, connected, selected, onSelect }) {
  const stale = link.lamp === "red" || link.lamp === "off";
  const fields = unit ? unit.fields.filter((f) => f.kpi || f.type === "bool") : [];
  const tiles = fields.map((f) => ({ f, lamp: lampOf(f, stale) }));

  // красные лампы мигают, пока оператор их не квитирует щелчком
  const [acked, setAcked] = useState(() => new Set());
  const redNow = tiles.filter((t) => t.lamp === "red").map((t) => t.f.name);
  if (link.lamp === "red") redNow.push("__link");
  const redKey = redNow.join("|");
  useEffect(() => {
    setAcked((prev) => new Set([...prev].filter((k) => redNow.includes(k))));
  }, [redKey]); // eslint-disable-line react-hooks/exhaustive-deps

  const click = (id) => {
    if (redNow.includes(id) && !acked.has(id)) setAcked(new Set([...acked, id]));
    else if (id !== "__link") onSelect(id);
  };

  let linkValue = "порт закрыт";
  if (connected) linkValue = link.age == null ? "ждём пакет" : fmtAge(link.age);

  return (
    <section className="tablo" aria-label="Табло">
      <Lamp id="__link" lamp={link.lamp} name="Связь" value={linkValue} flash={link.lamp === "red" && !acked.has("__link")}
        title={link.lamp === "red" ? "Пакетов давно нет. Щелчок — квитировать" : undefined} onClick={click} />
      {tiles.map(({ f, lamp }) => (
        <Lamp key={f.name} id={f.name} lamp={lamp} name={f.name}
          value={f.type === "bool" ? (f.value ? "да" : "нет") : f.text} unitText={f.type === "bool" ? "" : f.unit}
          selected={selected === f.name} flash={lamp === "red" && !acked.has(f.name)}
          title={lamp === "red" && !acked.has(f.name) ? "Щелчок — квитировать" : f.note || "Показать на графике"}
          onClick={click} />
      ))}
      {!unit && (
        <div className="lamp is-off wide">
          <span className="lamp-name">Параметры</span>
          <span className="lamp-val small">появятся после первого пакета телеметрии</span>
        </div>
      )}
    </section>
  );
}

/* ---------- связь и устройство ---------- */
function LinkPanel({ units, unit, onUnit, link }) {
  const stats = useStation((s) => s.stats);
  const links = useStation((s) => s.links);
  const linkId = useStation((s) => s.link);
  const connected = useStation((s) => s.connected);
  const source = connected ? links.find((l) => l.id === linkId) : null;
  const hasCoords = unit && (unit.lon || unit.lat);
  return (
    <section className="panel">
      <header className="panel-head"><h2>Связь</h2></header>
      <div className="panel-body">
        <div className={`readout big st-${link.lamp}`}>
          <span className="readout-val">{link.age == null ? "—" : fmtAge(link.age)}</span>
          <span className="readout-cap">с последнего пакета</span>
        </div>
        <dl className="kv">
          {source && <div><dt>Источник</dt><dd className="text">{source.name}</dd></div>}
          <div><dt>Период пакетов</dt><dd>{link.period ? `${fmtNumber(link.period)} с` : "—"}</dd></div>
          <div><dt>Пакетов телеметрии</dt><dd>{unit ? num(unit.count) : 0}</dd></div>
          <div><dt>Не распознано</dt><dd className={stats.tm_unmatched ? "warn" : ""}>{num(stats.tm_unmatched)}</dd></div>
          <div><dt>Ошибки</dt><dd className={stats.errors ? "bad" : ""}>{num(stats.errors)}</dd></div>
          <div><dt>Принято байт</dt><dd>{num(stats.rx_bytes)}</dd></div>
          <div><dt>Отправлено команд</dt><dd>{num(stats.tx)}</dd></div>
          {hasCoords && (
            <div><dt>Координаты в пакете</dt><dd>{(unit.lon / 1e6).toFixed(6)}, {(unit.lat / 1e6).toFixed(6)}</dd></div>
          )}
        </dl>
        {units.length > 1 && (
          <div className="units" role="radiogroup" aria-label="Источник телеметрии">
            {units.map((u) => (
              <button key={u.key} role="radio" aria-checked={u.key === unit?.key} className={u.key === unit?.key ? "on" : ""} onClick={() => onUnit(u.key)}>
                <b>{u.name}</b>
                <span>{devName(u.device_id)}</span>
              </button>
            ))}
          </div>
        )}
      </div>
    </section>
  );
}

/* ---------- приборы ---------- */
function Spark({ values }) {
  if (values.length < 2) return <span className="spark" />;
  const lo = Math.min(...values), hi = Math.max(...values), span = hi - lo || 1;
  const pts = values.map((v, i) => `${((i / (values.length - 1)) * 100).toFixed(1)},${(15 - ((v - lo) / span) * 12).toFixed(1)}`).join(" ");
  return (
    <svg className="spark" viewBox="0 0 100 18" preserveAspectRatio="none" aria-hidden="true">
      <polyline points={pts} />
    </svg>
  );
}

function AxisModule({ unit, group, fields, selected, onSelect }) {
  const scale = Math.max(
    1,
    ...fields.map((f) => {
      const s = getSeries(unit.key, f.name).v;
      return Math.max(Math.abs(f.value) || 0, ...s.slice(-120).map(Math.abs));
    }),
  );
  return (
    <div className={`module axis st-${worstState(fields)}`}>
      <h3>{group}</h3>
      {fields.map((f) => {
        const frac = typeof f.value === "number" ? Math.max(-1, Math.min(1, f.value / scale)) : 0;
        return (
          <button key={f.name} className={`axis-row ${selected === f.name ? "sel" : ""}`} onClick={() => onSelect(f.name)}>
            <span className="axis-name">{shortName(f.name, group)}</span>
            <span className="axis-track">
              <i style={frac >= 0 ? { left: "50%", width: `${frac * 50}%` } : { right: "50%", width: `${-frac * 50}%` }} />
            </span>
            <span className={`val st-${f.state}`}>{f.text}{f.unit && <small>{f.unit}</small>}</span>
          </button>
        );
      })}
    </div>
  );
}

function ListModule({ unit, group, fields, selected, onSelect }) {
  return (
    <div className={`module st-${worstState(fields)}`}>
      <h3>{group}</h3>
      {fields.map((f) => (
        <button key={f.name} className={`list-row ${selected === f.name ? "sel" : ""}`} onClick={() => onSelect(f.name)} title={f.note || undefined}>
          <span className="list-name">{shortName(f.name, group)}</span>
          <Spark values={getSeries(unit.key, f.name).v.slice(-60)} />
          <span className={`val st-${f.state}`}>{f.text}{f.unit && <small>{f.unit}</small>}</span>
        </button>
      ))}
    </div>
  );
}

function Instruments({ unit, selected, onSelect }) {
  useStation((s) => s.seriesRev);
  if (!unit) {
    return (
      <section className="panel instruments">
        <header className="panel-head"><h2>Приборы</h2></header>
        <p className="empty-note">
          Телеметрии пока нет. Откройте порт приёмника — пакеты, совпавшие с описанием в протоколе, разложатся здесь по приборам.
        </p>
      </section>
    );
  }
  const groups = new Map();
  for (const f of unit.fields) {
    if (f.kpi || f.type === "bool") continue;
    const g = f.group || "Параметры";
    if (!groups.has(g)) groups.set(g, []);
    groups.get(g).push(f);
  }
  return (
    <section className="panel instruments">
      <header className="panel-head">
        <h2>Приборы</h2>
        <span className="panel-meta">{unit.name}, {devName(unit.device_id)}</span>
      </header>
      <div className="modules">
        {[...groups].map(([g, fs]) => {
          const isAxis = fs.length >= 2 && fs.every((f) => /^[XYZ]$/i.test(shortName(f.name, g)));
          const M = isAxis ? AxisModule : ListModule;
          return <M key={g} unit={unit} group={g} fields={fs} selected={selected} onSelect={onSelect} />;
        })}
      </div>
    </section>
  );
}

/* ---------- журнал событий ---------- */
function Journal() {
  const records = useStation((s) => s.records);
  const events = useMemo(() => {
    const out = [];
    for (let i = records.length - 1; i >= 0 && out.length < 200; i--) {
      const r = records[i];
      if (r.kind === "rx" && r.valid && r.telemetry && !r.alarm) continue; // штатная телеметрия — на табло
      out.push(r);
    }
    return out;
  }, [records]);

  return (
    <section className="panel journal">
      <header className="panel-head"><h2>Журнал</h2></header>
      {events.length === 0 ? (
        <p className="empty-note">Здесь появятся отправленные команды, ошибки и нераспознанные пакеты.</p>
      ) : (
        <ol className="events">
          {events.map((r) => {
            let cls = r.kind, text = r.preview;
            if (r.kind === "tx") text = r.command ? `${r.command}: ${r.text}` : `Строка: ${r.text}`;
            if (r.kind === "rx") {
              cls = r.alarm ? "err" : "warn";
              text = r.alarm ? "Получена тревога" : r.valid ? `Пакет не распознан: ${r.payload_hex || r.raw_hex}` : `Битый пакет: ${r.raw_hex}`;
            }
            if (r.alarm && r.kind === "tx") cls = "err";
            return (
              <li key={r.id} className={`ev ev-${cls}`}>
                <time>{r.time}</time>
                <span>{text}</span>
              </li>
            );
          })}
        </ol>
      )}
    </section>
  );
}

/* ---------- экран ---------- */
export default function MccView({ connection, onProtocol }) {
  useTick(500);
  const telemetry = useStation((s) => s.telemetry);
  const connected = useStation((s) => s.connected);
  const [unitKey, setUnitKey] = usePersistent("dpp.unit", null);
  const [fieldSel, setFieldSel] = usePersistent("dpp.trendField", null);

  const unit = telemetry.find((u) => u.key === unitKey) || telemetry[0] || null;
  const link = linkOf(unit, connected);
  // для графика годятся числа, а не значения из словаря («исправен», «Дежурный»)
  const numeric = unit
    ? unit.fields.filter((f) => typeof f.value === "number" && f.type !== "bool" && !Number.isNaN(parseFloat(f.text)))
    : [];
  const field = unit
    ? unit.fields.find((f) => f.name === fieldSel) || numeric.find((f) => f.kpi) || numeric[0] || null
    : null;

  return (
    <main className="mcc">
      <Annunciator unit={unit} link={link} connected={connected} selected={field?.name} onSelect={setFieldSel} />
      <aside className="col col-left">
        {connection}
        <LinkPanel units={telemetry} unit={unit} onUnit={setUnitKey} link={link} />
      </aside>
      <div className="col col-center">
        <Instruments unit={unit} selected={field?.name} onSelect={setFieldSel} />
        <Trend unit={unit} field={field} />
      </div>
      <aside className="col col-right">
        <Commands onProtocol={onProtocol} />
        <Journal />
      </aside>
    </main>
  );
}
