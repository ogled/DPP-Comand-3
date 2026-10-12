import { useState } from "react";
import { signIn, useStore } from "../store.js";
import Logo from "./Logo.jsx";

export default function AuthView() {
  const [mode, setMode] = useState("login");
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const online = useStore((s) => s.online);

  const submit = async (e) => {
    e.preventDefault();
    setBusy(true);
    setError("");
    const err = await signIn(mode, username.trim().toLowerCase(), password);
    setBusy(false);
    if (err) setError(err);
  };

  const reg = mode === "register";
  return (
    <main className="auth">
      <form className="auth-card" onSubmit={submit}>
        <div className="auth-brand">
          <Logo size={40} />
          <div>
            <h1>Глеботрон</h1>
            <p>мессенджер через кубсат</p>
          </div>
        </div>

        <div className="seg full" role="tablist">
          <button type="button" role="tab" aria-selected={!reg} className={reg ? "" : "on"}
            onClick={() => setMode("login")}>Вход</button>
          <button type="button" role="tab" aria-selected={reg} className={reg ? "on" : ""}
            onClick={() => setMode("register")}>Регистрация</button>
        </div>

        <label className="field">
          <span>Имя</span>
          <input type="text" autoComplete="username" autoCapitalize="none" spellCheck="false" required
            value={username} onChange={(e) => setUsername(e.target.value)} maxLength={20}
            placeholder={reg ? "латиница, цифры и _, от 3 символов" : ""} />
        </label>
        <label className="field">
          <span>Пароль</span>
          <input type="password" autoComplete={reg ? "new-password" : "current-password"} required
            value={password} onChange={(e) => setPassword(e.target.value)} maxLength={128}
            minLength={reg ? 8 : undefined} placeholder={reg ? "от 8 символов" : ""} />
        </label>

        {error && <p className="form-error" role="alert">{error}</p>}
        {!online && !error && <p className="form-error" role="alert">Нет связи с сервером</p>}

        <button className="btn primary wide" disabled={busy}>
          {busy ? "Подождите…" : reg ? "Зарегистрироваться" : "Войти"}
        </button>
        {reg && <p className="auth-hint">После регистрации вы получите свой номер: его можно продиктовать владельцу терминала, чтобы он написал вам.</p>}

        <p className="auth-note">
          Сообщения на терминалы идут через спутник: доставка занимает часы, текст передаётся открыто, без шифрования.
        </p>
      </form>
    </main>
  );
}
