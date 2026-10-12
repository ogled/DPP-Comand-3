import { useEffect, useRef, useState } from "react";
import { cleanText, measure, plural } from "../format.js";
import { send, useStore } from "../store.js";
import Icon from "./Icon.jsx";

const touch = matchMedia("(pointer: coarse)").matches;

export default function Composer({ peer }) {
  const limits = useStore((s) => s.me.limits);
  const radioLeft = useStore((s) => s.radioLeft);
  const drafts = useRef({});
  const [text, setText] = useState("");
  const [busy, setBusy] = useState(false);
  const area = useRef(null);

  useEffect(() => {
    setText(drafts.current[peer.address] || "");
    if (!touch) area.current?.focus();
  }, [peer.address]);

  useEffect(() => {
    const el = area.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(el.scrollHeight, 140)}px`;
  }, [text]);

  const radio = peer.kind === "terminal";
  const clean = cleanText(text);
  const { bytes, frames } = measure(clean, limits.frame_text_bytes);
  const over = bytes > limits.msg_max_bytes;
  const noQuota = radio && radioLeft === 0;
  const canSend = clean && !over && !noQuota && !busy;

  const change = (v) => {
    drafts.current[peer.address] = v;
    setText(v);
  };

  const submit = async (e) => {
    e?.preventDefault();
    if (!canSend) return;
    setBusy(true);
    const ok = await send(peer.address, clean);
    setBusy(false);
    if (ok) change("");
    area.current?.focus();
  };

  const onKey = (e) => {
    if (e.key === "Enter" && !e.shiftKey && !touch && !e.nativeEvent.isComposing) {
      e.preventDefault();
      submit();
    }
  };

  return (
    <form className="composer" onSubmit={submit}>
      {radio && (
        <p className="radio-note">
          <Icon name="sat" size={14} />
          <span>
            Через спутник: доставка займёт часы, текст передаётся открыто.
            {radioLeft != null && <> Сегодня можно ещё <b>{radioLeft}</b> из {limits.radio_per_day}.</>}
          </span>
        </p>
      )}
      <div className="composer-row">
        <textarea ref={area} rows={1} value={text} onChange={(e) => change(e.target.value)} onKeyDown={onKey}
          placeholder={radio ? "Сообщение на терминал…" : "Сообщение…"} aria-label="Текст сообщения" />
        <button className="btn primary send" disabled={!canSend} title={touch ? "Отправить" : "Отправить (Enter)"}>
          <Icon name="send" />
        </button>
      </div>
      <div className={`composer-count ${over ? "over" : ""}`}>
        <span>{bytes} / {limits.msg_max_bytes} байт</span>
        {radio && clean && <span>{frames} {plural(frames, "радиокадр", "радиокадра", "радиокадров")}</span>}
        {!touch && <span className="composer-hint">Shift+Enter — новая строка</span>}
      </div>
    </form>
  );
}
