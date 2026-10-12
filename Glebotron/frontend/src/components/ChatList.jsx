import { useState } from "react";
import { fmtAgo, fmtTime, STATUS } from "../format.js";
import { lookup, openChat, useStore, useTick } from "../store.js";
import Icon from "./Icon.jsx";

function NewChat() {
  const [q, setQ] = useState("");
  const [busy, setBusy] = useState(false);
  const submit = async (e) => {
    e.preventDefault();
    if (!q.trim()) return;
    setBusy(true);
    const peer = await lookup(q.trim());
    setBusy(false);
    if (peer) {
      setQ("");
      openChat(peer.address);
    }
  };
  return (
    <form className="newchat" onSubmit={submit}>
      <input type="search" value={q} onChange={(e) => setQ(e.target.value)} placeholder="Ник или номер: alice, #7"
        aria-label="Новый чат: ник пользователя или номер терминала" autoCapitalize="none" spellCheck="false" />
      <button className="btn icon" disabled={busy || !q.trim()} title="Открыть чат">
        <Icon name="plus" />
      </button>
    </form>
  );
}

function PeerIcon({ peer }) {
  return (
    <span className={`peer-icon ${peer.kind}`} aria-hidden="true">
      <Icon name={peer.kind === "terminal" ? "terminal" : "user"} size={15} />
    </span>
  );
}

function ConvItem({ conv, active }) {
  const { peer, last } = conv;
  const st = last && last.out ? STATUS[last.status] : null;
  return (
    <li>
      <button className={`conv ${active ? "on" : ""}`} onClick={() => openChat(peer.address)}>
        <PeerIcon peer={peer} />
        <span className="conv-body">
          <span className="conv-top">
            <span className="conv-name">{peer.name}</span>
            <span className="conv-time">{last ? fmtTime(last.created_at) : ""}</span>
          </span>
          <span className="conv-last">
            {st && <span className={`st st-${last.status}`}><Icon name={st.icon} size={13} title={st.label} /></span>}
            <span className="conv-text">{last ? last.text : `#${peer.address}`}</span>
          </span>
        </span>
      </button>
    </li>
  );
}

export default function ChatList() {
  useTick(60000);
  const convs = useStore((s) => s.convs);
  const directory = useStore((s) => s.directory);
  const active = useStore((s) => s.active);
  const known = new Set(convs.map((c) => c.peer.address));
  const fresh = directory.filter((t) => !known.has(t.address));

  return (
    <aside className="sidebar">
      <NewChat />
      <div className="sidebar-scroll">
        {convs.length > 0 ? (
          <ul className="convs" aria-label="Диалоги">
            {convs.map((c) => <ConvItem key={c.peer.address} conv={c} active={c.peer.address === active} />)}
          </ul>
        ) : (
          <div className="empty-side">
            <p><b>Диалогов пока нет.</b></p>
            <p>Напишите терминалу по его номеру (1–999) или другому пользователю по нику.</p>
          </div>
        )}
        {fresh.length > 0 && (
          <>
            <h3 className="side-h">Терминалы в эфире</h3>
            <ul className="convs">
              {fresh.map((t) => (
                <li key={t.address}>
                  <button className={`conv ${t.address === active ? "on" : ""}`} onClick={() => openChat(t.address)}>
                    <PeerIcon peer={t} />
                    <span className="conv-body">
                      <span className="conv-top">
                        <span className="conv-name">{t.name}</span>
                      </span>
                      <span className="conv-last"><span className="conv-text">был в эфире {fmtAgo(t.last_seen)}</span></span>
                    </span>
                  </button>
                </li>
              ))}
            </ul>
          </>
        )}
      </div>
    </aside>
  );
}
