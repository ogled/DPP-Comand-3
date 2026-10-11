import { memo, useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { useStation, usePersistent, clearJournal, sendText, sendAlarm } from "../store.js";
import { MAX_PAYLOAD, devName, num } from "../format.js";
import Inspector from "./Inspector.jsx";
import ArmButton from "./ArmButton.jsx";

const SHOW_LAST = 1500;
const DIRS = [
  ["all", "Все"],
  ["rx", "Приём"],
  ["tx", "Отправка"],
  ["sys", "Система"],
];

function matches(r, f) {
  if (f.dir !== "all") {
    if (f.dir === "sys" ? !(r.kind === "sys" || r.kind === "err") : r.kind !== f.dir) return false;
  }
  if (f.type !== "any") {
    const t = r.proto === "ccsds" ? "ccsds" : String(r.type);
    if (r.kind !== "rx" || t !== f.type) return false;
  }
  if (f.query) {
    const hay = `${r.id} ${r.device_id ?? ""} ${r.type_name ?? ""} ${r.text ?? ""} ${r.tm_summary ?? ""} ${r.raw_hex ?? ""}`.toLowerCase();
    if (!hay.includes(f.query)) return false;
  }
  return true;
}

const Row = memo(function Row({ r, selected }) {
  const cls = ["row", `k-${r.kind}`];
  if (r.kind === "rx" && !r.valid) cls.push("bad");
  if (r.alarm) cls.push("alarm");
  if (selected) cls.push("sel");
  let dev = "", type = "", msg = r.preview;
  if (r.kind === "rx") {
    dev = r.valid ? devName(r.device_id) : "—";
    type = r.valid ? r.type_name : "Битый пакет";
    if (r.tm_summary) msg = r.tm_summary;
    if (r.alarm) msg = "Тревога";
  } else if (r.kind === "tx") {
    dev = "Земля";
    type = r.command ? "Команда" : "Строка";
    msg = r.alarm ? "Тревога" : r.preview;
  }
  return (
    <div className={cls.join(" ")} data-id={r.id}>
      <span className="c-time">{r.time}</span>
      <span className="c-dir">{r.kind === "rx" ? "↓" : r.kind === "tx" ? "↑" : ""}</span>
      <span className="c-dev">{dev}</span>
      <span className="c-type">{type}</span>
      <span className={`c-msg ${r.kind === "rx" && !r.tm_summary ? "mono" : ""}`}>{String(msg).replace(/\n/g, " ↵ ")}</span>
    </div>
  );
});

function Composer() {
  const connected = useStation((s) => s.connected);
  const [text, setText] = useState("");
  const bytes = new TextEncoder().encode(text).length;
  const over = bytes > MAX_PAYLOAD;
  const submit = async (e) => {
    e.preventDefault();
    if (!connected || !text.trim() || over) return;
    if (await sendText(text)) setText("");
  };
  return (
    <form className="composer" onSubmit={submit} autoComplete="off">
      <div className="composer-input">
        <input type="text" value={text} onChange={(e) => setText(e.target.value)} placeholder="Строка на спутник" aria-label="Строка на спутник" spellCheck={false} />
        <span className={`count ${over ? "over" : ""}`}>{bytes} из {MAX_PAYLOAD} байт</span>
      </div>
      <button className="btn primary" type="submit" disabled={!connected || !text.trim() || over}>Отправить</button>
      <ArmButton armed danger armLabel="Тревога" disabled={!connected} onFire={sendAlarm} />
    </form>
  );
}

function Traffic() {
  const s = useStation((st) => st.stats);
  return (
    <section className="panel">
      <header className="panel-head"><h2>Трафик</h2></header>
      <dl className="kv panel-body">
        <div><dt>Принято пакетов</dt><dd>{num(s.rx)}</dd></div>
        <div><dt>Отправлено</dt><dd>{num(s.tx)}</dd></div>
        <div><dt>Принято байт</dt><dd>{num(s.rx_bytes)}</dd></div>
        <div><dt>Отправлено байт</dt><dd>{num(s.tx_bytes)}</dd></div>
        <div><dt>Не распознано</dt><dd className={s.tm_unmatched ? "warn" : ""}>{num(s.tm_unmatched)}</dd></div>
        <div><dt>Ошибки</dt><dd className={s.errors ? "bad" : ""}>{num(s.errors)}</dd></div>
      </dl>
    </section>
  );
}

export default function MonitorView({ connection }) {
  const records = useStation((s) => s.records);
  const connected = useStation((s) => s.connected);
  const [dir, setDir] = usePersistent("dpp.dir", "all");
  const [type, setType] = usePersistent("dpp.type", "any");
  const [query, setQuery] = useState("");
  const [selected, setSelected] = useState(null);
  const [pausedAt, setPausedAt] = useState(null);
  const feed = useRef(null);
  const stick = useRef(true);

  const filter = useMemo(() => ({ dir, type, query: query.trim().toLowerCase() }), [dir, type, query]);
  const visible = useMemo(() => {
    const src = pausedAt == null ? records : records.filter((r) => r.id <= pausedAt);
    const list = src.filter((r) => matches(r, filter));
    return list.length > SHOW_LAST ? list.slice(-SHOW_LAST) : list;
  }, [records, filter, pausedAt]);
  const held = pausedAt == null ? 0 : records.filter((r) => r.id > pausedAt).length;
  const selRec = selected == null ? null : records.find((r) => r.id === selected) || null;

  useLayoutEffect(() => {
    if (stick.current && feed.current) feed.current.scrollTop = feed.current.scrollHeight;
  }, [visible]);

  useEffect(() => {
    const el = feed.current;
    const onScroll = () => { stick.current = el.scrollHeight - el.scrollTop - el.clientHeight < 40; };
    el.addEventListener("scroll", onScroll);
    return () => el.removeEventListener("scroll", onScroll);
  }, []);

  const onKey = (e) => {
    if (e.key !== "ArrowDown" && e.key !== "ArrowUp") return;
    e.preventDefault();
    if (!visible.length) return;
    let i = visible.findIndex((r) => r.id === selected);
    i = e.key === "ArrowDown" ? Math.min(visible.length - 1, i + 1) : Math.max(0, i < 0 ? visible.length - 1 : i - 1);
    setSelected(visible[i].id);
    feed.current.querySelector(`[data-id="${visible[i].id}"]`)?.scrollIntoView({ block: "nearest" });
  };

  let empty = null;
  if (!visible.length) {
    if (records.length) empty = ["Ничего не найдено", "Измените поиск или сбросьте фильтры слева."];
    else if (connected) empty = ["Ждём первый пакет", "Порт открыт. Пакеты появятся здесь, как только приёмник что-то передаст."];
    else empty = ["Пакетов пока нет", "Выберите порт слева и откройте его. Чтобы проверить интерфейс без оборудования, выберите порт DEMO."];
  }

  return (
    <main className="monitor">
      <aside className="col col-left">
        {connection}
        <section className="panel">
          <header className="panel-head"><h2>Фильтр</h2></header>
          <div className="panel-body form">
            <input type="search" value={query} onChange={(e) => setQuery(e.target.value)} placeholder="Устройство, текст, HEX" aria-label="Поиск" />
            <div className="seg full" role="group" aria-label="Направление">
              {DIRS.map(([v, l]) => <button key={v} className={dir === v ? "on" : ""} onClick={() => setDir(v)}>{l}</button>)}
            </div>
            <label className="field">
              <span>Тип пакета</span>
              <select value={type} onChange={(e) => setType(e.target.value)}>
                <option value="any">Любой</option>
                <option value="ccsds">Спутник (CCSDS)</option>
                <option value="0">I2C</option>
                <option value="1">SPI</option>
                <option value="2">UART</option>
                <option value="3">Цифровой</option>
                <option value="4">Аналоговый</option>
              </select>
            </label>
          </div>
        </section>
        <Traffic />
      </aside>

      <section className="col col-center feed-panel panel">
        <header className="panel-head">
          <h2>Лента</h2>
          <div className="panel-actions">
            <button className="btn quiet small" onClick={() => setPausedAt(pausedAt == null ? records.at(-1)?.id ?? 0 : null)}>
              {pausedAt == null ? "Пауза" : "Продолжить"}
            </button>
            <button className="btn quiet small" onClick={() => { clearJournal(); setSelected(null); }}>Очистить</button>
          </div>
        </header>
        <div className="feed-head" aria-hidden="true">
          <span>Время</span><span /><span>Устройство</span><span>Тип</span><span>Содержимое</span>
        </div>
        <div className="feed" ref={feed} tabIndex={0} onKeyDown={onKey} aria-label="Лента пакетов"
          onClick={(e) => { const row = e.target.closest(".row"); if (row) setSelected(Number(row.dataset.id)); }}>
          {visible.map((r) => <Row key={r.id} r={r} selected={r.id === selected} />)}
        </div>
        {empty && (
          <div className="feed-empty">
            <p className="t">{empty[0]}</p>
            <p>{empty[1]}</p>
          </div>
        )}
        {held > 0 && (
          <button className="newpill" onClick={() => setPausedAt(null)}>Новых записей: {held}. Показать</button>
        )}
        <Composer />
      </section>

      <aside className="col col-right">
        <Inspector r={selRec} />
      </aside>
    </main>
  );
}
