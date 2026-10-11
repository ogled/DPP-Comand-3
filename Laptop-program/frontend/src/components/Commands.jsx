import { useState } from "react";
import { useStation, sendCommand } from "../store.js";
import ArmButton from "./ArmButton.jsx";

function defaults(cmd) {
  const v = {};
  for (const p of cmd.params || []) v[p.name] = p.default ?? (p.type === "bool" ? false : "");
  return v;
}

function Param({ p, value, onChange }) {
  const label = p.label || p.name;
  if (p.options) {
    return (
      <label className="param">
        <span>{label}</span>
        <select value={String(value)} onChange={(e) => onChange(e.target.value)}>
          {p.options.map((o) => {
            const v = typeof o === "object" ? o.value : o;
            const l = typeof o === "object" ? o.label ?? o.value : o;
            return <option key={String(v)} value={String(v)}>{l}</option>;
          })}
        </select>
      </label>
    );
  }
  if (p.type === "bool") {
    return (
      <label className="param check">
        <input type="checkbox" checked={!!value} onChange={(e) => onChange(e.target.checked)} />
        <span>{label}</span>
      </label>
    );
  }
  const range = p.min !== undefined || p.max !== undefined ? `${p.min ?? ""}…${p.max ?? ""}` : "";
  return (
    <label className="param">
      <span>{label}</span>
      <input type="text" value={value} placeholder={range || (p.type && p.type !== "str" ? p.type : "")} autoComplete="off" spellCheck={false}
        onChange={(e) => onChange(e.target.value)} />
    </label>
  );
}

function CommandRow({ cmd, connected }) {
  const [values, setValues] = useState(() => defaults(cmd));
  const [armed, setArmed] = useState(false);
  const needsArm = !!(cmd.confirm || cmd.danger);
  const hint = typeof cmd.confirm === "string" ? cmd.confirm : "Нажмите «Выполнить», чтобы отправить.";

  return (
    <div className={`cmd ${cmd.danger ? "danger" : ""} ${armed ? "is-armed" : ""}`}>
      <div className="cmd-head">
        <div className="cmd-title">
          <b>{cmd.name}</b>
          {cmd.description && <span>{cmd.description}</span>}
        </div>
        <ArmButton armed={needsArm} danger={cmd.danger} disabled={!connected} onArmChange={setArmed}
          onFire={() => sendCommand(cmd, values)} />
      </div>
      {cmd.params?.length > 0 && (
        <div className="cmd-params">
          {cmd.params.map((p) => (
            <Param key={p.name} p={p} value={values[p.name]} onChange={(v) => setValues({ ...values, [p.name]: v })} />
          ))}
        </div>
      )}
      {armed && <p className="cmd-hint">{hint}</p>}
    </div>
  );
}

export default function Commands({ onProtocol }) {
  const commands = useStation((s) => s.commands);
  const connected = useStation((s) => s.connected);
  const protoVersion = useStation((s) => s.protoVersion);

  return (
    <section className="panel commands">
      <header className="panel-head">
        <h2>Команды</h2>
        <button className="btn quiet small" onClick={onProtocol}>Изменить</button>
      </header>
      <div className="panel-body">
        {!connected && commands.length > 0 && <p className="note">Откройте порт, чтобы отправлять команды.</p>}
        {commands.length === 0 ? (
          <p className="note">В протоколе нет команд. Добавьте их в раздел commands.</p>
        ) : (
          <div className="cmd-list">
            {commands.map((c) => <CommandRow key={`${protoVersion}:${c.id}`} cmd={c} connected={connected} />)}
          </div>
        )}
      </div>
    </section>
  );
}
