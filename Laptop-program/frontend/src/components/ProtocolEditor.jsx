import { useEffect, useMemo, useRef, useState } from "react";
import { loadProtocol, saveProtocol, toast } from "../store.js";

/** Проверка JSON в браузере: номер строки ошибки, чтобы не искать её глазами. */
function jsonCheck(text) {
  try {
    JSON.parse(text);
    return null;
  } catch (e) {
    const m = /position (\d+)/.exec(e.message);
    const lc = /line (\d+) column (\d+)/.exec(e.message);
    if (lc) return { line: Number(lc[1]), text: e.message };
    if (m) {
      const pos = Number(m[1]);
      return { line: text.slice(0, pos).split("\n").length, text: e.message };
    }
    return { line: null, text: e.message };
  }
}

export default function ProtocolEditor({ onClose }) {
  const [text, setText] = useState("");
  const [saved, setSaved] = useState("");
  const [path, setPath] = useState("");
  const [serverError, setServerError] = useState("");
  const [confirmClose, setConfirmClose] = useState(false);
  const [busy, setBusy] = useState(false);
  const area = useRef(null);
  const gutter = useRef(null);

  useEffect(() => {
    loadProtocol().then((d) => {
      if (!d) return toast("Протокол не загрузился: сервер не отвечает", true);
      setText(d.text);
      setSaved(d.text);
      setPath(d.path);
      setServerError(d.error || "");
      area.current?.focus();
    });
  }, []);

  const dirty = text !== saved;
  const check = useMemo(() => jsonCheck(text), [text]);
  const lines = useMemo(() => text.split("\n").length, [text]);

  const save = async () => {
    if (check) return;
    setBusy(true);
    const res = await saveProtocol(text);
    setBusy(false);
    if (!res.ok) return setServerError(res.error);
    setSaved(text);
    setServerError("");
    toast("Протокол сохранён и применён");
    onClose();
  };

  const close = () => {
    if (dirty && !confirmClose) return setConfirmClose(true);
    onClose();
  };

  useEffect(() => {
    const onKey = (e) => {
      if (e.key === "Escape") close();
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
        e.preventDefault();
        save();
      }
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  });

  const format = () => {
    try {
      setText(JSON.stringify(JSON.parse(text), null, 2) + "\n");
    } catch {
      /* ошибка уже показана внизу */
    }
  };

  const onTab = (e) => {
    if (e.key !== "Tab") return;
    e.preventDefault();
    e.currentTarget.setRangeText("  ", e.currentTarget.selectionStart, e.currentTarget.selectionEnd, "end");
    setText(e.currentTarget.value);
  };

  return (
    <div className="modal" onMouseDown={(e) => e.target === e.currentTarget && close()}>
      <div className="modal-box" role="dialog" aria-modal="true" aria-label="Протокол">
        <header className="modal-head">
          <div>
            <h2>Протокол</h2>
            <span className="panel-meta">{path}</span>
          </div>
          <button className="btn quiet icon" onClick={close} aria-label="Закрыть">
            <svg width="14" height="14" viewBox="0 0 14 14" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"><path d="M3 3l8 8M11 3l-8 8" /></svg>
          </button>
        </header>

        <details className="help">
          <summary>Справка по формату</summary>
          <div className="help-body">
            <p><b>framing</b> — как резать поток: <code>"mode": "ccsds"</code> (пакеты спутника с 6-байтовым заголовком) или <code>"markers"</code> (приёмник Earth: 0x01 — лог, 0x02 + длина — пакет). Для ccsds: <code>apids</code>, <code>min_packet</code>, <code>max_packet</code>.</p>
            <p><b>telemetry</b> — кадры. <code>match</code> выбирает кадр по <code>apid, device_id, type, type_input, size, min_size, prefix</code>. Побеждает первый подошедший.</p>
            <p><b>fields</b>: <code>name, type</code> (u8 i8 u16 i16 u32 i32 u64 i64 f32 f64 bool str hex regex), <code>offset, length, scale, add, unit, decimals, bit, mask, enum, regex</code>, пороги <code>warn_min warn_max crit_min crit_max</code>, <code>bad_values</code>. <code>group</code> собирает поля в прибор (поля X, Y, Z показываются стрелками). <code>kpi: true</code> выносит поле на табло; логические поля попадают на табло сами.</p>
            <p><b>commands</b>: <code>id, name, description</code>, шаблон <code>text</code> («LED {'{'}state{'}'}») или <code>bytes</code> («A5 01 {'{'}n{'}'}»), <code>params</code> (<code>name label type min max default options</code>), <code>suffix, confirm, danger, alarm</code>. Команды с <code>confirm</code> или <code>danger</code> отправляются в два шага: «Взвести», затем «Выполнить».</p>
            <p><code>byte_order</code> — little или big, можно задать в кадре или команде. Файл применяется сразу после сохранения; править его можно и в любом редакторе.</p>
          </div>
        </details>

        <div className="editor">
          <div className="gutter" ref={gutter} aria-hidden="true">
            {Array.from({ length: lines }, (_, i) => (
              <span key={i} className={check?.line === i + 1 ? "err" : ""}>{i + 1}</span>
            ))}
          </div>
          <textarea ref={area} value={text} spellCheck={false} onChange={(e) => { setText(e.target.value); setConfirmClose(false); }}
            onKeyDown={onTab} onScroll={(e) => { if (gutter.current) gutter.current.scrollTop = e.currentTarget.scrollTop; }}
            aria-label="Текст protocol.json" />
        </div>

        <footer className="modal-foot">
          <p className={`editor-status ${check || serverError ? "bad" : ""}`}>
            {check
              ? `Ошибка JSON${check.line ? ` в строке ${check.line}` : ""}: ${check.text}`
              : serverError || (dirty ? "Есть несохранённые изменения." : "Сохранено.")}
          </p>
          {confirmClose ? (
            <>
              <button className="btn" onClick={onClose}>Закрыть без сохранения</button>
              <button className="btn primary" onClick={save} disabled={!!check || busy}>Сохранить</button>
            </>
          ) : (
            <>
              <button className="btn" onClick={format} disabled={!!check}>Выровнять</button>
              <button className="btn primary" onClick={save} disabled={!!check || busy || !dirty}>Сохранить</button>
            </>
          )}
        </footer>
      </div>
    </div>
  );
}
