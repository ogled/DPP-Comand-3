import { useEffect, useLayoutEffect, useRef, useState } from "react";
import { fmtAgo } from "../format.js";
import { loadThread, openChat, useStore, useTick } from "../store.js";
import Composer from "./Composer.jsx";
import Icon from "./Icon.jsx";
import MessageBubble from "./MessageBubble.jsx";

function PeerInfo({ peer, directory }) {
  useTick(60000);
  if (peer.kind !== "terminal") return <span>пользователь · #{peer.address}</span>;
  const t = directory.find((d) => d.address === peer.address);
  return (
    <span>
      терминал #{peer.address} · {t ? `был в эфире ${fmtAgo(t.last_seen)}` : "ещё не появлялся в эфире"}
    </span>
  );
}

export default function ChatView() {
  const active = useStore((s) => s.active);
  const peer = useStore((s) => s.peers[s.active]);
  const thread = useStore((s) => s.threads[s.active]);
  const directory = useStore((s) => s.directory);
  const me = useStore((s) => s.me);
  const scroller = useRef(null);
  const stick = useRef(true);
  const prevHeight = useRef(null);
  const [loadingOlder, setLoadingOlder] = useState(false);

  const items = thread?.items || [];
  const lastId = items.length ? items[items.length - 1].id : 0;

  useEffect(() => {
    stick.current = true;
  }, [active]);

  useLayoutEffect(() => {
    const el = scroller.current;
    if (!el) return;
    if (prevHeight.current != null) {
      el.scrollTop += el.scrollHeight - prevHeight.current; // подгрузили старые — остаёмся на месте
      prevHeight.current = null;
    } else if (stick.current) {
      el.scrollTop = el.scrollHeight;
    }
  }, [items, active, lastId]);

  const onScroll = () => {
    const el = scroller.current;
    stick.current = el.scrollHeight - el.scrollTop - el.clientHeight < 60;
  };

  const older = async () => {
    setLoadingOlder(true);
    prevHeight.current = scroller.current?.scrollHeight ?? null;
    await loadThread(active, true);
    setLoadingOlder(false);
  };

  if (active == null) {
    return (
      <section className="chat chat-empty">
        <div className="empty-chat">
          <p className="empty-title">Выберите диалог слева</p>
          <p>Ваш номер — <b>#{me.address}</b>. Продиктуйте его владельцу терминала, чтобы он написал вам.</p>
          <p>Сообщения на терминалы идут через спутник: ЦУП передаёт их во время пролёта, а терминал получает на одном из следующих.</p>
        </div>
      </section>
    );
  }

  const shown = peer || { address: active, name: `#${active}`, kind: active < 1000 ? "terminal" : "user" };
  return (
    <section className="chat">
      <header className="chat-head">
        <button className="btn quiet icon back" onClick={() => openChat(null)} title="К диалогам">
          <Icon name="back" />
        </button>
        <div className="chat-title">
          <b>{shown.name}</b>
          <PeerInfo peer={shown} directory={directory} />
        </div>
      </header>

      <div className="messages" ref={scroller} onScroll={onScroll}>
        {thread?.more && (
          <button className="btn small older" onClick={older} disabled={loadingOlder}>
            {loadingOlder ? "Загрузка…" : "Показать раньше"}
          </button>
        )}
        {thread && !items.length && (
          <p className="thread-empty">
            {shown.kind === "terminal"
              ? "Сообщений пока нет. Напишите — сообщение встанет в очередь и уйдёт на спутник в ближайший сеанс ЦУП."
              : "Сообщений пока нет."}
          </p>
        )}
        <ul className="msg-list">
          {items.map((m) => <MessageBubble key={m.id} m={m} />)}
        </ul>
      </div>

      <Composer peer={shown} />
    </section>
  );
}
