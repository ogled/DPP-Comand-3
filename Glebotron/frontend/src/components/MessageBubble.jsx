import { fmtTime, plural, STATUS } from "../format.js";
import Icon from "./Icon.jsx";

export default function MessageBubble({ m }) {
  const st = STATUS[m.status] || { label: m.status, icon: "warn" };
  const radio = m.via === "radio";
  const title = [
    `создано ${fmtTime(m.created_at)}`,
    m.updated_at !== m.created_at ? `${st.label} — ${fmtTime(m.updated_at)}` : st.label,
    radio && m.frames ? `${m.frames} ${plural(m.frames, "радиокадр", "радиокадра", "радиокадров")}` : null,
  ].filter(Boolean).join("\n");

  return (
    <li className={`msg ${m.out ? "out" : "in"} ${radio ? "radio" : ""} st-${m.status}`}>
      <div className="bubble" title={title}>
        <p className="msg-text">{m.text}</p>
        <span className="msg-meta">
          {radio && <span className="msg-radio" title="через спутник"><Icon name="sat" size={12} /></span>}
          <time dateTime={new Date(m.created_at * 1000).toISOString()}>{fmtTime(m.created_at)}</time>
          {m.out && (
            <span className={`st st-${m.status}`}>
              <Icon name={st.icon} size={14} title={st.label} />
            </span>
          )}
        </span>
      </div>
      {m.out && radio && m.status !== "delivered" && <span className="msg-status">{st.label}</span>}
    </li>
  );
}
