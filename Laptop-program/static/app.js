(() => {
  const $ = (id) => document.getElementById(id);
  const el = {
    status: $("status"), statusText: $("statusText"),
    port: $("port"), baud: $("baud"), parity: $("parity"), refresh: $("refreshBtn"), connect: $("connectBtn"),
    pause: $("pauseBtn"), pauseText: $("pauseText"), clear: $("clearBtn"),
    search: $("search"), dir: $("dirFilter"), type: $("typeFilter"),
    feed: $("feed"), empty: $("empty"), emptyT: $("emptyT"), emptyS: $("emptyS"), pill: $("newPill"),
    composer: $("composer"), msg: $("msg"), count: $("count"), send: $("sendBtn"), alarm: $("alarmBtn"),
    inspEmpty: $("inspEmpty"), inspBody: $("inspBody"), toasts: $("toasts"),
    sRx: $("sRx"), sTx: $("sTx"), sRxB: $("sRxB"), sTxB: $("sTxB"), sErr: $("sErr"), sUn: $("sUn"),
  };

  const MAX_PAYLOAD = 52;
  const COLORS = {
    dev: "--s-dev", lon: "--s-lon", lat: "--s-lat", type: "--s-type", pack: "--s-pack",
    size: "--s-size", payload: "--s-payload", tail: "--s-tail", raw: "--s-raw",
  };

  const state = {
    records: [], byId: new Map(), last: 0, connected: false,
    paused: false, held: 0, selected: null,
    dir: "all", type: -1, query: "", stick: true,
    mode: "mon", protoVersion: null, commands: [], tm: [], now: 0, nowLocal: 0,
  };

  /* ---------- утилиты ---------- */
  const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const num = (n) => Number(n).toLocaleString("ru-RU");
  const bytesWord = (n) => {
    const a = Math.abs(n) % 100, b = a % 10;
    if (a > 10 && a < 20) return "байт";
    if (b === 1) return "байт";
    if (b >= 2 && b <= 4) return "байта";
    return "байт";
  };

  function toast(text, err) {
    const t = document.createElement("div");
    t.className = "toast" + (err ? " err" : "");
    t.textContent = text;
    el.toasts.appendChild(t);
    setTimeout(() => t.remove(), 3800);
  }

  async function api(path, body) {
    try {
      const r = await fetch(path, body === undefined ? {} : {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
      });
      const data = await r.json();
      return data;
    } catch (e) {
      return { ok: false, error: "Сервер не отвечает. Проверьте, что app.py запущен." };
    }
  }

  /* ---------- порты ---------- */
  async function loadPorts() {
    const keep = el.port.value;
    const ports = await fetch("/api/ports").then((r) => r.json()).catch(() => []);
    el.port.innerHTML = ports.map((p) =>
      `<option value="${esc(p.device)}">${esc(p.device)}${p.description ? " — " + esc(p.description) : ""}</option>`).join("");
    if (keep && ports.some((p) => p.device === keep)) el.port.value = keep;
    else if (ports.length > 1) el.port.value = ports[0].device;
  }

  el.refresh.addEventListener("click", loadPorts);
  el.connect.addEventListener("click", async () => {
    if (state.connected) { await api("/api/disconnect", {}); return poll(); }
    const res = await api("/api/connect", { port: el.port.value, baud: el.baud.value, parity: el.parity.value });
    if (!res.ok) toast(res.error || "Не удалось подключиться", true);
    poll();
  });

  /* ---------- фильтры ---------- */
  function matches(r) {
    if (state.dir !== "all") {
      if (state.dir === "sys" ? !(r.kind === "sys" || r.kind === "err") : r.kind !== state.dir) return false;
    }
    if (state.type !== -1 && r.type !== state.type) return false;
    if (state.query) {
      const hay = `${r.id} ${r.device_id ?? ""} ${r.type_name ?? ""} ${r.text ?? ""} ${r.raw_hex ?? ""}`.toLowerCase();
      if (!hay.includes(state.query)) return false;
    }
    return true;
  }

  el.search.addEventListener("input", () => { state.query = el.search.value.trim().toLowerCase(); renderAll(); });
  el.type.addEventListener("change", () => { state.type = Number(el.type.value); renderAll(); });
  el.dir.addEventListener("click", (e) => {
    const b = e.target.closest("button");
    if (!b) return;
    state.dir = b.dataset.v;
    el.dir.querySelectorAll("button").forEach((x) => x.classList.toggle("on", x === b));
    renderAll();
  });

  /* ---------- лента ---------- */
  const ICON_RX = '<svg width="10" height="10" viewBox="0 0 10 10" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 1.5v7M2.3 6 5 8.7 7.7 6"/></svg>';
  const ICON_TX = '<svg width="10" height="10" viewBox="0 0 10 10" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 8.5v-7M2.3 4 5 1.3 7.7 4"/></svg>';

  function rowHtml(r) {
    const cls = ["row", r.kind];
    if (r.kind === "rx" && !r.valid) cls.push("bad");
    if (r.alarm) cls.push("alarm");
    if (r.id === state.selected) cls.push("sel");
    let dir = "", dev = "", type = "", msg;
    if (r.kind === "rx") {
      dir = `<span class="dir rx">${ICON_RX}</span>`;
      if (r.valid) {
        dev = `DEV-${String(r.device_id).padStart(3, "0")}`;
        type = esc(r.type_name);
        msg = r.alarm ? `<span class="tag">Тревога</span>ALERT`
          : (state.mode === "mcc" && r.tm_summary) ? `<span class="tm">${esc(r.telemetry.name)}</span> · ${esc(r.tm_summary)}`
          : esc(r.preview.replace(/\n/g, " ↵ "));
      } else {
        dev = "—"; type = "Некорректный";
        msg = esc(r.preview);
      }
    } else if (r.kind === "tx") {
      dir = `<span class="dir tx">${ICON_TX}</span>`;
      dev = "Земля"; type = r.command ? "Команда" : "UART / Serial";
      msg = r.alarm ? `<span class="tag">Тревога</span>ALERT` : esc(r.preview);
    } else {
      msg = esc(r.preview);
    }
    const mono = r.kind === "rx" && !r.valid ? " mono" : "";
    return `<div class="${cls.join(" ")}" data-id="${r.id}">
      <span class="c-time">${r.time}</span><span class="c-dir">${dir}</span>
      <span class="c-dev">${dev}</span><span class="c-type">${type}</span><span class="c-msg${mono}">${msg}</span></div>`;
  }

  function renderAll() {
    const list = state.records.filter(matches);
    el.feed.innerHTML = list.map(rowHtml).join("");
    updateEmpty(list.length);
    if (state.stick) el.feed.scrollTop = el.feed.scrollHeight;
  }

  function appendRecords(recs) {
    const shown = recs.filter(matches);
    if (shown.length) {
      el.feed.insertAdjacentHTML("beforeend", shown.map(rowHtml).join(""));
      if (state.stick) el.feed.scrollTop = el.feed.scrollHeight;
    }
    updateEmpty(el.feed.childElementCount);
  }

  function updateEmpty(shownCount) {
    const none = shownCount === 0;
    el.empty.hidden = !none;
    if (!none) return;
    if (state.records.length) {
      el.emptyT.textContent = "Ничего не найдено";
      el.emptyS.textContent = "Измените поиск или сбросьте фильтры слева.";
    } else if (state.connected) {
      el.emptyT.textContent = "Ждём первый пакет";
      el.emptyS.textContent = "Порт открыт. Пакеты появятся здесь, как только спутник выйдет на связь.";
    } else {
      el.emptyT.textContent = "Пакетов пока нет";
      el.emptyS.textContent = "Выберите порт слева и нажмите «Подключить». Для проверки интерфейса без оборудования выберите порт DEMO.";
    }
  }

  el.feed.addEventListener("scroll", () => {
    state.stick = el.feed.scrollHeight - el.feed.scrollTop - el.feed.clientHeight < 40;
  });
  el.feed.addEventListener("click", (e) => {
    const row = e.target.closest(".row");
    if (row) select(Number(row.dataset.id));
  });
  el.feed.addEventListener("keydown", (e) => {
    if (e.key !== "ArrowDown" && e.key !== "ArrowUp") return;
    e.preventDefault();
    const rows = [...el.feed.querySelectorAll(".row")];
    if (!rows.length) return;
    let i = rows.findIndex((r) => Number(r.dataset.id) === state.selected);
    i = e.key === "ArrowDown" ? Math.min(rows.length - 1, i + 1) : Math.max(0, i < 0 ? rows.length - 1 : i - 1);
    select(Number(rows[i].dataset.id));
    rows[i].scrollIntoView({ block: "nearest" });
  });

  /* ---------- инспектор ---------- */
  function select(id) {
    state.selected = id;
    el.feed.querySelectorAll(".row.sel").forEach((r) => r.classList.remove("sel"));
    const row = el.feed.querySelector(`.row[data-id="${id}"]`);
    if (row) row.classList.add("sel");
    renderInspector(state.byId.get(id));
  }

  function segValue(r, key) {
    const h = (a, b) => r.raw_hex.split(" ").slice(a, b).join(" ");
    switch (key) {
      case "dev": return [`DEV-${String(r.device_id).padStart(3, "0")}`, `ID ${r.device_id}`];
      case "lon": return [num(r.longitude), "знаковое, 4 байта"];
      case "lat": return [num(r.latitude), "знаковое, 4 байта"];
      case "type": return [r.type, r.type_name.split(" / ")[0]];
      case "pack": return [`№ ${r.packet_number}, вход ${r.type_input}`, "старшие 4 бита — номер"];
      case "size": return [`${r.size} ${bytesWord(r.size)}`, "длина данных"];
      case "payload": return [r.size ? `${r.size} ${bytesWord(r.size)}` : "пусто", "см. ниже"];
      case "tail": return [`${r.tail_hex.split(" ").length} ${bytesWord(r.tail_hex.split(" ").length)}`, "лишнее после данных"];
      default: return [h(0, 99), ""];
    }
  }

  function renderInspector(r) {
    if (!r) { el.inspBody.hidden = true; el.inspEmpty.hidden = false; return; }
    el.inspEmpty.hidden = true; el.inspBody.hidden = false;

    if (r.kind === "sys" || r.kind === "err") {
      el.inspBody.innerHTML = `
        <div class="insp-head"><div><div class="t">Сообщение ${String(r.id).padStart(6, "0")}</div><div class="s">${r.time}</div></div>
        <span class="pill ${r.kind === "err" ? "bad" : ""}">${r.kind === "err" ? "Ошибка" : "Система"}</span></div>
        <div class="isec"><pre class="payload ${r.kind === "err" ? "alarm" : ""}">${esc(r.text)}</pre></div>`;
      return;
    }

    const rx = r.kind === "rx";
    const pill = rx ? (r.valid ? '<span class="pill rx">Приём</span>' : '<span class="pill bad">Ошибка пакета</span>') : '<span class="pill tx">Отправка</span>';
    let html = `<div class="insp-head"><div><div class="t">Пакет ${String(r.id).padStart(6, "0")}</div>
      <div class="s">${r.time} · ${r.raw_hex.split(" ").length} ${bytesWord(r.raw_hex.split(" ").length)}</div></div>${pill}</div>`;

    if (rx) {
      const bytes = r.raw_hex.split(" ");
      const owner = new Array(bytes.length).fill("raw");
      r.segments.forEach((s) => { for (let i = s.start; i < s.start + s.len && i < bytes.length; i++) owner[i] = s.key; });
      const cells = bytes.map((b, i) => {
        const k = owner[i], v = COLORS[k] || COLORS.raw;
        return `<span class="b" data-k="${k}" style="--c:var(${v});--ci:var(${v}-i)">${b}</span>`;
      }).join("");
      html += `<div class="isec"><h3>Байты пакета</h3><div class="bytes" id="bytemap">${cells}</div>`;
      if (r.valid) {
        html += `<dl class="fields">` + r.segments.map((s) => {
          const [v, hint] = s.value !== undefined ? [s.value, s.hint || ""] : segValue(r, s.key);
          const bg = COLORS[s.key];
          return `<div class="f" data-k="${s.key}"><span class="sw" style="--c:var(${bg})"></span><dt>${s.name}</dt><dd>${esc(v)}${hint ? `<small>${esc(hint)}</small>` : ""}</dd></div>`;
        }).join("") + `</dl>`;
      } else {
        html += `<p class="s" style="color:var(--bad);margin:0">Пакет короче заголовка (12 байт) или размер данных не совпадает с длиной. Разобрать поля невозможно.</p>`;
      }
      html += `</div>`;
    }

    if (rx && r.telemetry) {
      html += `<div class="isec"><h3>Телеметрия · ${esc(r.telemetry.name)}</h3><dl class="fields">` +
        r.telemetry.fields.map((f) => `<div class="f" style="grid-template-columns:0 1fr auto"><span></span><dt>${esc(f.name)}</dt><dd>${esc(f.text)}${f.unit ? `<small>${esc(f.unit)}</small>` : ""}</dd></div>`).join("") +
        `</dl></div>`;
    }

    if (!rx || r.valid) {
      html += `<div class="isec"><h3>Данные</h3><pre class="payload ${r.alarm ? "alarm" : ""}">${esc(r.text) || "—"}</pre></div>`;
    }

    html += `<div class="isec"><h3>Копировать</h3>
      <div class="hexlabel">Пакет</div><div class="hexbox">${r.raw_hex}</div>
      <div class="hexlabel">Кадр порта</div><div class="hexbox">${r.frame_hex}</div>
      <div style="display:flex;gap:6px;margin-top:10px">
        <button class="btn" data-copy="${r.raw_hex}">Копировать пакет</button>
        <button class="btn" data-copy="${r.frame_hex}">Копировать кадр</button></div></div>`;

    el.inspBody.innerHTML = html;
  }

  el.inspBody.addEventListener("mouseover", (e) => {
    const f = e.target.closest(".f");
    const map = $("bytemap");
    if (!map) return;
    map.classList.toggle("hl", !!f);
    map.querySelectorAll(".b").forEach((b) => b.classList.toggle("on", !!f && b.dataset.k === f.dataset.k));
  });
  el.inspBody.addEventListener("mouseleave", () => {
    const map = $("bytemap");
    if (map) { map.classList.remove("hl"); map.querySelectorAll(".b.on").forEach((b) => b.classList.remove("on")); }
  });
  el.inspBody.addEventListener("click", async (e) => {
    const b = e.target.closest("[data-copy]");
    if (!b) return;
    try { await navigator.clipboard.writeText(b.dataset.copy); toast("Скопировано"); }
    catch { toast("Не удалось скопировать", true); }
  });

  /* ---------- отправка ---------- */
  function updateComposer() {
    const bytes = new TextEncoder().encode(el.msg.value).length;
    el.count.textContent = `${bytes} / ${MAX_PAYLOAD}`;
    el.count.classList.toggle("over", bytes > MAX_PAYLOAD);
    el.send.disabled = !state.connected || !el.msg.value.trim() || bytes > MAX_PAYLOAD;
    el.alarm.disabled = !state.connected;
  }
  el.msg.addEventListener("input", updateComposer);
  el.composer.addEventListener("submit", async (e) => {
    e.preventDefault();
    if (el.send.disabled) return;
    const res = await api("/api/send", { text: el.msg.value });
    if (res.ok) { el.msg.value = ""; updateComposer(); state.stick = true; }
    else toast(res.error || "Не удалось отправить", true);
    poll();
  });
  el.alarm.addEventListener("click", async () => {
    if (!confirm("Отправить на спутник сигнал тревоги?")) return;
    const res = await api("/api/alarm", {});
    if (res.ok) toast("Сигнал тревоги отправлен"); else toast(res.error || "Не удалось отправить", true);
    state.stick = true;
    poll();
  });

  /* ---------- пауза и очистка ---------- */
  el.pause.addEventListener("click", () => {
    state.paused = !state.paused;
    el.pauseText.textContent = state.paused ? "Продолжить" : "Пауза";
    if (!state.paused) { state.held = 0; el.pill.hidden = true; renderAll(); }
  });
  el.pill.addEventListener("click", () => el.pause.click());
  el.clear.addEventListener("click", async () => {
    await api("/api/clear", {});
    state.records = []; state.byId.clear(); state.held = 0; state.selected = null;
    el.pill.hidden = true;
    renderAll(); renderInspector(null);
  });

  /* ---------- режим ЦУП ---------- */
  const mccEl = {
    modes: $("modes"), cards: $("cards"), tmInfo: $("tmInfo"), cmdList: $("cmdList"),
    modal: $("protoModal"), text: $("protoText"), err: $("protoErr"), path: $("protoPath"),
  };

  function setMode(m) {
    state.mode = m;
    document.body.classList.toggle("mode-mcc", m === "mcc");
    mccEl.modes.querySelectorAll("button").forEach((b) => b.classList.toggle("on", b.dataset.m === m));
    try { localStorage.setItem("mode", m); } catch {}
    renderAll();
    renderCards();
  }
  mccEl.modes.addEventListener("click", (e) => { const b = e.target.closest("button"); if (b) setMode(b.dataset.m); });

  function spark(h) {
    if (!h || h.length < 2) return "";
    const lo = Math.min(...h), hi = Math.max(...h), span = hi - lo || 1;
    const pts = h.map((v, i) => `${(i / (h.length - 1) * 100).toFixed(1)},${(16 - (v - lo) / span * 14).toFixed(1)}`).join(" ");
    return `<svg class="spark" viewBox="0 0 100 18" preserveAspectRatio="none"><polyline points="${pts}"/></svg>`;
  }

  function ago(sec) {
    if (sec < 2) return "сейчас";
    if (sec < 60) return `${Math.floor(sec)} с назад`;
    return `${Math.floor(sec / 60)} мин назад`;
  }

  function fieldVal(f) {
    return `${esc(f.text)}${f.unit ? `<small>${esc(f.unit)}</small>` : ""}`;
  }

  function shortName(f, group) {
    if (group && f.name.startsWith(group)) {
      const n = f.name.slice(group.length).replace(/^[\s:·-]+/, "");
      if (n) return n;
    }
    return f.name;
  }

  function renderCards() {
    if (state.mode !== "mcc") return;
    const un = Number(el.sUn.textContent) || 0;
    mccEl.tmInfo.textContent = un ? `не распознано пакетов: ${un}` : "";
    const scroll = mccEl.cards.scrollTop;
    if (!state.tm.length) {
      mccEl.cards.innerHTML = `<div class="cards-empty">Совпадающих с протоколом пакетов пока нет. Описание кадров — кнопка «Протокол» слева.</div>`;
      return;
    }
    const age = (c) => state.now - c.ts + (Date.now() - state.nowLocal) / 1000;
    const worst = (fs) => (fs.some((f) => f.state === "bad") ? "bad" : fs.some((f) => f.state === "warn") ? "warn" : "");
    mccEl.cards.innerHTML = state.tm.map((c) => {
      const a = age(c), stale = a > 15;
      const kpis = c.fields.filter((f) => f.kpi);
      const groups = new Map();
      c.fields.filter((f) => !f.kpi).forEach((f) => {
        const g = f.group || "Параметры";
        if (!groups.has(g)) groups.set(g, []);
        groups.get(g).push(f);
      });
      return `<section class="unit ${stale ? "stale" : ""}">
        <div class="unit-head">
          <b>${esc(c.name)}</b>
          <span class="link ${stale ? "lost" : ""}"><i></i>${stale ? "Нет связи" : "На связи"} · ${ago(a)}</span>
          <span class="muted">DEV-${String(c.device_id).padStart(3, "0")} · пакетов ${num(c.count)}</span>
        </div>
        ${kpis.length ? `<div class="kpis">${kpis.map((f) => `<div class="kpi ${f.state}" title="${esc(f.note || "")}">
          <span class="kpi-name">${esc(f.name)}</span><span class="kpi-val">${fieldVal(f)}</span></div>`).join("")}</div>` : ""}
        <div class="groups">${[...groups].map(([g, fs]) => `<div class="grp ${worst(fs)}"><h4>${esc(g)}</h4>${fs.map((f) =>
          `<div class="tf ${f.state}" title="${esc(f.note || "")}"><span class="tf-name">${esc(shortName(f, g))}</span>${spark(f.history) || "<span></span>"}<span class="tf-val">${fieldVal(f)}</span></div>`).join("")}</div>`).join("")}</div>
      </section>`;
    }).join("");
    mccEl.cards.scrollTop = scroll;
  }
  setInterval(renderCards, 1000);

  /* команды */
  function cmdParamHtml(p) {
    const label = esc(p.label || p.name);
    const def = p.default ?? "";
    if (p.options) {
      return `<label>${label}<select data-p="${esc(p.name)}">${p.options.map((o) => {
        const v = typeof o === "object" ? o.value : o, l = typeof o === "object" ? (o.label ?? o.value) : o;
        return `<option value="${esc(v)}"${String(v) === String(def) ? " selected" : ""}>${esc(l)}</option>`;
      }).join("")}</select></label>`;
    }
    if (p.type === "bool") return `<label>${label}<input type="checkbox" data-p="${esc(p.name)}"${def ? " checked" : ""}></label>`;
    const hint = p.min !== undefined || p.max !== undefined ? `${p.min ?? ""}…${p.max ?? ""}` : (p.type || "");
    return `<label>${label}<input type="text" data-p="${esc(p.name)}" value="${esc(def)}" placeholder="${esc(hint)}" autocomplete="off"></label>`;
  }

  function renderCommands() {
    if (!state.commands.length) { mccEl.cmdList.innerHTML = `<div class="cmd-empty">Команды не описаны в протоколе.</div>`; return; }
    mccEl.cmdList.innerHTML = state.commands.map((c) => `
      <div class="cmd ${c.danger ? "danger" : ""}" data-id="${esc(c.id)}">
        <div class="cmd-top"><span class="cmd-name">${esc(c.name)}</span>
          <button class="btn ${c.danger ? "danger" : "primary"}" data-run ${state.connected ? "" : "disabled"}>Отправить</button></div>
        ${c.description ? `<div class="cmd-desc">${esc(c.description)}</div>` : ""}
        ${c.params.length ? `<div class="cmd-params">${c.params.map(cmdParamHtml).join("")}</div>` : ""}
      </div>`).join("");
  }

  mccEl.cmdList.addEventListener("click", async (e) => {
    const btn = e.target.closest("[data-run]");
    if (!btn) return;
    const box = btn.closest(".cmd");
    const cmd = state.commands.find((c) => c.id === box.dataset.id);
    if (!cmd) return;
    if (cmd.confirm && !confirm(typeof cmd.confirm === "string" ? cmd.confirm : `Отправить команду «${cmd.name}»?`)) return;
    const params = {};
    box.querySelectorAll("[data-p]").forEach((i) => { params[i.dataset.p] = i.type === "checkbox" ? i.checked : i.value; });
    const res = await api("/api/command", { id: cmd.id, params });
    if (res.ok) toast(`Команда «${cmd.name}» отправлена`); else toast(res.error || "Не удалось отправить", true);
    state.stick = true;
    poll();
  });

  async function loadProtocol() {
    const d = await fetch("/api/protocol").then((r) => r.json()).catch(() => null);
    if (!d) return null;
    state.commands = d.commands || [];
    renderCommands();
    return d;
  }

  /* редактор протокола */
  async function openProto() {
    const d = await loadProtocol();
    if (!d) return toast("Не удалось загрузить протокол", true);
    mccEl.text.value = d.text;
    mccEl.path.textContent = d.path;
    mccEl.err.textContent = d.error || "";
    mccEl.modal.hidden = false;
    mccEl.text.focus();
  }
  const closeProto = () => { mccEl.modal.hidden = true; };
  $("protoBtn").addEventListener("click", openProto);
  $("protoClose").addEventListener("click", closeProto);
  mccEl.modal.addEventListener("mousedown", (e) => { if (e.target === mccEl.modal) closeProto(); });
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && !mccEl.modal.hidden) closeProto();
    if ((e.ctrlKey || e.metaKey) && e.key === "s" && !mccEl.modal.hidden) { e.preventDefault(); $("protoSave").click(); }
  });
  $("protoFmt").addEventListener("click", () => {
    try { mccEl.text.value = JSON.stringify(JSON.parse(mccEl.text.value), null, 2); mccEl.err.textContent = ""; }
    catch (e) { mccEl.err.textContent = "JSON: " + e.message; }
  });
  $("protoSave").addEventListener("click", async () => {
    const res = await api("/api/protocol", { text: mccEl.text.value });
    if (!res.ok) { mccEl.err.textContent = res.error; return; }
    mccEl.err.textContent = "";
    closeProto();
    toast("Протокол сохранён и применён");
    poll();
  });
  mccEl.text.addEventListener("keydown", (e) => {
    if (e.key !== "Tab") return;
    e.preventDefault();
    mccEl.text.setRangeText("  ", mccEl.text.selectionStart, mccEl.text.selectionEnd, "end");
  });

  /* ---------- опрос сервера ---------- */
  function setConnected(on, port, baud, parity) {
    const changed = state.connected !== on;
    state.connected = on;
    el.status.classList.toggle("on", on);
    el.statusText.textContent = on ? `Подключено · ${port}${port === "DEMO" ? "" : " · " + baud + " 8" + (parity || "N") + "1"}` : "Отключено";
    el.connect.textContent = on ? "Отключить" : "Подключить";
    el.connect.classList.toggle("stop", on);
    el.port.disabled = on; el.baud.disabled = on; el.parity.disabled = on; el.refresh.disabled = on;
    el.pause.disabled = !on && !state.paused;
    updateComposer();
    if (changed) { renderCommands(); if (!state.records.length) updateEmpty(0); }
  }

  let busy = false;
  async function poll() {
    if (busy) return;
    busy = true;
    try {
      const d = await fetch(`/api/state?since=${state.last}`).then((r) => r.json());
      if (d.reset) { state.records = []; state.byId.clear(); state.last = 0; el.feed.innerHTML = ""; }
      setConnected(d.connected, d.port, d.baud, d.parity);
      const s = d.stats;
      el.sRx.textContent = num(s.rx); el.sTx.textContent = num(s.tx);
      el.sRxB.textContent = num(s.rx_bytes); el.sTxB.textContent = num(s.tx_bytes);
      state.tm = d.telemetry || []; state.now = d.now; state.nowLocal = Date.now();
      if (d.proto_version !== state.protoVersion) { state.protoVersion = d.proto_version; loadProtocol(); }
      el.sUn.textContent = s.tm_unmatched;
      renderCards();
      el.sErr.textContent = num(s.errors); el.sErr.classList.toggle("has-err", s.errors > 0);
      if (d.records.length) {
        d.records.forEach((r) => { state.records.push(r); state.byId.set(r.id, r); });
        if (state.records.length > 5000) {
          state.records.splice(0, state.records.length - 5000);
        }
        state.last = d.last;
        if (state.paused) {
          state.held += d.records.length;
          el.pill.textContent = `Новых пакетов: ${state.held}. Показать`;
          el.pill.hidden = false;
        } else appendRecords(d.records);
      }
    } catch { /* сервер временно недоступен */ }
    busy = false;
  }

  $("themeBtn").addEventListener("click", () => {
    const next = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem("theme", next); } catch {}
  });

  try { if (localStorage.getItem("mode") === "mcc") setMode("mcc"); } catch {}
  loadPorts().then(() => { poll(); renderAll(); });
  setInterval(poll, 400);
})();
