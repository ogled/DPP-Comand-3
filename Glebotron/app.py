"""Глеботрон — сервер мессенджера через кубсат.

Локально:   python app.py  →  http://127.0.0.1:5001
На сервере: gunicorn wsgi:app за nginx (см. deploy/).
Команды админа: flask --app app <команда>, на сервере — sudo glebotron <команда>.

Веб-клиент ходит в /api/* (cookie-сессия), ЦУП — в /gs/v1/* (Bearer-токен станции).
"""
import contextlib
import glob
import hashlib
import hmac
import os
import random
import re
import secrets
import sqlite3
import sys
import time
from datetime import datetime, timedelta
from urllib.parse import urlsplit

import click
from flask import Blueprint, Flask, current_app, g, jsonify, request, send_from_directory, session
from werkzeug.exceptions import HTTPException
from werkzeug.middleware.proxy_fix import ProxyFix
from werkzeug.security import check_password_hash, generate_password_hash

import codec
import db
import quotas

HERE = os.path.dirname(os.path.abspath(__file__))
STATIC = os.path.join(HERE, "static")

USERNAME_RE = re.compile(r"[a-z0-9_]{3,20}")
ADDRESS_RE = re.compile(r"#?(\d{1,5})")
RESERVED = {"admin", "root", "glebotron", "cup", "server", "system", "support"}
TERM_MIN, TERM_MAX = 1, 999
USER_MIN, USER_MAX = 1000, 65000
STATION_ONLINE_SEC = 120
HOUSEKEEP_SEC = 300
DUP_WINDOW = 7 * 24 * 3600
FRAMES_KEEP = 10_000
DOWNLINK_MAX = 200
UPDATES_MAX = 500
DUMMY_HASH = generate_password_hash("glebotron-timing")  # чтобы вход с чужим именем длился столько же

bp = Blueprint("glebotron", __name__, cli_group=None)


def clock():
    """Текущее время, с. Тесты подменяют эту функцию."""
    return time.time()


def default_config():
    env = os.environ.get
    return {
        "DB_PATH": env("GLEBOTRON_DB", os.path.join(HERE, "glebotron.db")),
        "SECRET_KEY": env("GLEBOTRON_SECRET_KEY", ""),
        "ORIGIN": env("GLEBOTRON_ORIGIN", "http://127.0.0.1:5001").rstrip("/"),
        "FRAME_MAX": int(env("GLEBOTRON_FRAME_MAX", 39)),
        "MSG_MAX": int(env("GLEBOTRON_MSG_MAX", 160)),
        "RADIO_PER_DAY": int(env("GLEBOTRON_RADIO_PER_DAY", 10)),
        "QUEUE_MAX": int(env("GLEBOTRON_QUEUE_MAX", 200)),
        "WEB_PER_MIN": int(env("GLEBOTRON_WEB_PER_MIN", 30)),
        "LEASE_SEC": int(env("GLEBOTRON_LEASE_SEC", 1800)),
        "EXPIRE_SEC": int(env("GLEBOTRON_EXPIRE_SEC", 7 * 24 * 3600)),
        "MAX_ATTEMPTS": int(env("GLEBOTRON_MAX_ATTEMPTS", 5)),
        "BACKUP_DIR": env("GLEBOTRON_BACKUP_DIR", ""),
    }


class ApiError(Exception):
    def __init__(self, status, error, **extra):
        super().__init__(error)
        self.status, self.error, self.extra = status, error, extra


# ---------- общее ----------
def conn():
    if "db" not in g:
        g.db = db.connect(current_app.config["DB_PATH"])
    return g.db


@bp.teardown_app_request
def close_db(_exc):
    c = g.pop("db", None)
    if c is not None:
        c.close()


def body():
    data = request.get_json(silent=True)
    if not isinstance(data, dict):
        raise ApiError(400, "Ожидается JSON-объект")
    return data


def int_value(raw, default, lo, hi, name):
    if raw is None or raw == "":
        return default
    try:
        value = int(raw)
    except (TypeError, ValueError):
        raise ApiError(400, f"{name}: ожидается целое число")
    return min(max(value, lo), hi)


def touch(c, mid, **fields):
    """Меняет поля сообщения и выдаёт ему новый seq, чтобы клиенты увидели изменение."""
    fields["seq"] = db.next_seq(c)
    fields["updated_at"] = clock()
    sets = ", ".join(f"{k} = ?" for k in fields)
    c.execute(f"UPDATE messages SET {sets} WHERE id = ?", (*fields.values(), mid))


def insert_message(c, src, dst, text, via, status, radio_id=None):
    t = clock()
    cur = c.execute("INSERT INTO messages(src, dst, text, via, radio_id, status, seq, created_at, updated_at) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (src, dst, text, via, radio_id, status, db.next_seq(c), t, t))
    return cur.lastrowid


def is_terminal(address):
    return TERM_MIN <= address <= TERM_MAX


def is_user(address):
    return USER_MIN <= address <= USER_MAX


def describe(c, addresses):
    """Подписи адресов: {address: {address, name, kind[, seen]}}."""
    addrs = sorted(set(addresses))
    marks = ",".join("?" * len(addrs))
    users, terms = {}, {}
    if addrs:
        users = {r["address"]: r["username"] for r in
                 c.execute(f"SELECT address, username FROM users WHERE address IN ({marks})", addrs)}
        terms = {r["address"]: r for r in
                 c.execute(f"SELECT address, name FROM terminals WHERE address IN ({marks})", addrs)}
    out = {}
    for a in addrs:
        if is_terminal(a):
            t = terms.get(a)
            out[a] = {"address": a, "name": (t["name"] if t and t["name"] else f"Терминал #{a}"),
                      "kind": "terminal", "seen": t is not None}
        elif a == 0:
            out[a] = {"address": 0, "name": "Сервер", "kind": "server"}
        else:
            out[a] = {"address": a, "name": users.get(a, f"#{a}"), "kind": "user"}
    return out


def messages_json(c, rows, me):
    names = describe(c, [r["src"] for r in rows] + [r["dst"] for r in rows])
    frame_max = current_app.config["FRAME_MAX"]
    return [{
        "id": r["id"], "from": names[r["src"]], "to": names[r["dst"]],
        "peer": r["dst"] if r["src"] == me else r["src"], "out": r["src"] == me,
        "text": r["text"], "via": r["via"], "status": r["status"],
        "frames": codec.frames_needed(r["text"], frame_max) if r["via"] == "radio" else 0,
        "created_at": r["created_at"], "updated_at": r["updated_at"],
    } for r in rows]


def status_json(c):
    st = c.execute("SELECT MAX(last_seen) AS seen, MAX(last_aos) AS aos, MAX(last_los) AS los "
                   "FROM stations").fetchone()
    t = clock()
    return {"queue": quotas.queue_size(c), "station_online": bool(st["seen"] and t - st["seen"] < STATION_ONLINE_SEC),
            "last_aos": st["aos"], "last_los": st["los"], "now": t}


def housekeeping(c, force=False):
    """Возвращает просроченные выдачи в очередь, гасит старые сообщения, чистит журналы."""
    state = current_app.extensions["glebotron"]
    t = clock()
    if not force and t - state["housekeep_at"] < HOUSEKEEP_SEC:
        return
    state["housekeep_at"] = t
    cfg = current_app.config
    with db.transaction(c):
        for r in c.execute("SELECT id FROM messages WHERE status = 'uplinked' AND leased_until < ?", (t,)).fetchall():
            touch(c, r["id"], status="queued", station_id=None, leased_until=None)
        for r in c.execute("SELECT id FROM messages WHERE status IN ('queued', 'uplinked', 'on_satellite') "
                           "AND created_at < ?", (t - cfg["EXPIRE_SEC"],)).fetchall():
            touch(c, r["id"], status="expired", station_id=None, leased_until=None)
        c.execute("DELETE FROM fragments WHERE received_at < ?", (t - DUP_WINDOW,))
        c.execute("DELETE FROM frames WHERE id <= (SELECT MAX(id) FROM frames) - ?", (FRAMES_KEEP,))
        c.execute("DELETE FROM login_failures WHERE ts < ?", (t - 86400,))


# ---------- защита и ошибки ----------
@bp.before_app_request
def guard():
    """Защита от чужих сайтов (DNS rebinding и CSRF), как в Laptop-program/app.py, но строже:
    POST на /api/* без заголовка Origin тоже отклоняется."""
    cfg = current_app.config
    if request.host not in cfg["ALLOWED_HOSTS"]:
        return jsonify(ok=False, error="Недопустимый адрес сервера"), 403
    if request.method == "POST":
        if request.path.startswith("/api/") and request.headers.get("Origin") not in cfg["ALLOWED_ORIGINS"]:
            return jsonify(ok=False, error="Запрос с чужого сайта отклонён"), 403
        if not request.is_json:
            return jsonify(ok=False, error="Нужен Content-Type: application/json"), 415
    return None


@bp.after_app_request
def security_headers(resp):
    h = resp.headers
    h["Content-Security-Policy"] = ("default-src 'self'; img-src 'self' data:; connect-src 'self'; "
                                    "frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
    h["X-Content-Type-Options"] = "nosniff"
    h["Referrer-Policy"] = "same-origin"
    if request.path.startswith(("/api/", "/gs/")):
        h["Cache-Control"] = "no-store"
    return resp


@bp.app_errorhandler(ApiError)
def api_error(exc):
    return jsonify(ok=False, error=exc.error, **exc.extra), exc.status


@bp.app_errorhandler(HTTPException)
def http_error(exc):
    return jsonify(ok=False, error=exc.description or exc.name), exc.code


# ---------- пользователи ----------
def login_session(user):
    session.clear()
    session["uid"] = user["id"]
    session["ver"] = user["session_version"]
    session.permanent = True


def current_user():
    c = conn()
    uid = session.get("uid")
    user = c.execute("SELECT * FROM users WHERE id = ?", (uid,)).fetchone() if uid else None
    if user is None or user["session_version"] != session.get("ver"):
        session.clear()
        raise ApiError(401, "Нужно войти")
    if user["banned"]:
        raise ApiError(403, "Аккаунт заблокирован")
    t = clock()
    if not user["last_seen"] or t - user["last_seen"] > 60:
        with db.transaction(c):
            c.execute("UPDATE users SET last_seen = ? WHERE id = ?", (t, user["id"]))
    return user


def me_json(c, user):
    cfg = current_app.config
    used = quotas.radio_used_today(c, user["address"], clock())
    return {
        "username": user["username"], "address": user["address"], "is_admin": bool(user["is_admin"]),
        "seq": db.get_meta(c, "seq"),
        "limits": {"radio_per_day": cfg["RADIO_PER_DAY"], "radio_left_today": max(0, cfg["RADIO_PER_DAY"] - used),
                   "msg_max_bytes": cfg["MSG_MAX"], "frame_max": cfg["FRAME_MAX"],
                   "frame_text_bytes": cfg["FRAME_MAX"] - codec.HEADER_SIZE},
    }


def free_address(c):
    rng = random.SystemRandom()
    for _ in range(200):
        a = rng.randint(USER_MIN, USER_MAX)
        if not c.execute("SELECT 1 FROM users WHERE address = ?", (a,)).fetchone():
            return a
    row = c.execute("WITH RECURSIVE n(a) AS (SELECT ? UNION ALL SELECT a + 1 FROM n WHERE a < ?) "
                    "SELECT a FROM n WHERE a NOT IN (SELECT address FROM users) LIMIT 1",
                    (USER_MIN, USER_MAX)).fetchone()
    if row is None:
        raise ApiError(503, "Свободные адреса закончились")
    return row[0]


@bp.post("/api/register")
def register():
    b = body()
    username = str(b.get("username", "")).strip().lower()
    password = b.get("password")
    if not USERNAME_RE.fullmatch(username):
        raise ApiError(400, "Имя: от 3 до 20 символов — латиница, цифры и _")
    if username in RESERVED:
        raise ApiError(400, "Это имя зарезервировано")
    if not isinstance(password, str) or not 8 <= len(password) <= 128:
        raise ApiError(400, "Пароль: от 8 до 128 символов")
    c, t, ip = conn(), clock(), request.remote_addr or ""
    wait = quotas.register_wait(c, ip, t)
    if wait:
        raise ApiError(429, "Слишком много регистраций с вашего адреса, попробуйте позже", retry_after=wait)
    pw_hash = generate_password_hash(password)
    with db.transaction(c):
        if c.execute("SELECT 1 FROM users WHERE username = ?", (username,)).fetchone():
            raise ApiError(409, "Это имя уже занято")
        uid = c.execute("INSERT INTO users(username, password_hash, address, created_ip, created_at, last_seen) "
                        "VALUES (?, ?, ?, ?, ?, ?)",
                        (username, pw_hash, free_address(c), ip, t, t)).lastrowid
    user = c.execute("SELECT * FROM users WHERE id = ?", (uid,)).fetchone()
    login_session(user)
    return jsonify(ok=True, me=me_json(c, user), status=status_json(c)), 201


@bp.post("/api/login")
def login():
    b = body()
    username = str(b.get("username", "")).strip().lower()
    password = b.get("password")
    password = password if isinstance(password, str) else ""
    c, t, ip = conn(), clock(), request.remote_addr or ""
    wait = quotas.login_wait(c, ip, username, t)
    if wait:
        raise ApiError(429, "Слишком много неудачных попыток входа, попробуйте позже", retry_after=wait)
    user = c.execute("SELECT * FROM users WHERE username = ?", (username,)).fetchone()
    valid = check_password_hash(user["password_hash"] if user else DUMMY_HASH, password)
    if not (user and valid):
        with db.transaction(c):
            c.execute("INSERT INTO login_failures(ip, username, ts) VALUES (?, ?, ?)", (ip, username, t))
        raise ApiError(401, "Неверное имя или пароль")
    if user["banned"]:
        raise ApiError(403, "Аккаунт заблокирован")
    login_session(user)
    return jsonify(ok=True, me=me_json(c, user), status=status_json(c))


@bp.post("/api/logout")
def logout():
    session.clear()
    return jsonify(ok=True)


@bp.get("/api/me")
def me():
    user = current_user()
    c = conn()
    housekeeping(c)
    return jsonify(ok=True, me=me_json(c, user), status=status_json(c))


# ---------- переписка ----------
def parse_address(raw):
    """Число, «#123» или None, если это не номер."""
    if isinstance(raw, bool):
        return None
    if isinstance(raw, int):
        return raw
    m = ADDRESS_RE.fullmatch(str(raw or "").strip())
    return int(m.group(1)) if m else None


def resolve(c, raw):
    """Адрес получателя по номеру или имени. Терминалы 1–999 принимаются всегда:
    их ID прошит в устройство, а сервер мог его ещё не видеть."""
    address = parse_address(raw)
    if address is not None:
        if is_terminal(address):
            return address
        if is_user(address):
            if c.execute("SELECT 1 FROM users WHERE address = ?", (address,)).fetchone():
                return address
            raise ApiError(404, f"Пользователя #{address} нет")
        raise ApiError(400, "Адрес вне диапазонов: терминалы 1–999, пользователи 1000–65000")
    name = str(raw or "").strip().lower().lstrip("@")
    row = c.execute("SELECT address FROM users WHERE username = ?", (name,)).fetchone() if name else None
    if row is None:
        raise ApiError(404, "Пользователь не найден")
    return row["address"]


def clean_text(raw):
    if not isinstance(raw, str):
        raise ApiError(400, "Текст сообщения должен быть строкой")
    text = raw.replace("\r\n", "\n").replace("\r", "\n")
    text = "".join(ch for ch in text if ch in "\n\t" or (ord(ch) >= 32 and ord(ch) != 127)).strip()
    if not text:
        raise ApiError(400, "Пустое сообщение")
    size, limit = len(text.encode("utf-8")), current_app.config["MSG_MAX"]
    if size > limit:
        raise ApiError(413, f"Слишком длинное сообщение: {size} из {limit} байт")
    return text


@bp.get("/api/conversations")
def conversations():
    user = current_user()
    c, me_addr = conn(), user["address"]
    rows = c.execute("""
        SELECT m.* FROM messages m
        JOIN (SELECT CASE WHEN src = :me THEN dst ELSE src END AS peer, MAX(id) AS mid
              FROM messages WHERE src = :me OR dst = :me GROUP BY peer) last ON m.id = last.mid
        ORDER BY m.id DESC LIMIT 200""", {"me": me_addr}).fetchall()
    msgs = messages_json(c, rows, me_addr)
    peers = describe(c, [m["peer"] for m in msgs])
    return jsonify(ok=True, items=[{"peer": peers[m["peer"]], "last": m} for m in msgs])


@bp.get("/api/directory")
def directory():
    current_user()
    rows = conn().execute("SELECT * FROM terminals ORDER BY last_seen DESC LIMIT 1000").fetchall()
    return jsonify(ok=True, items=[{"address": r["address"], "name": r["name"] or f"Терминал #{r['address']}",
                                    "kind": "terminal", "seen": True, "first_seen": r["first_seen"],
                                    "last_seen": r["last_seen"]} for r in rows])


@bp.get("/api/lookup")
def lookup():
    current_user()
    c = conn()
    address = resolve(c, request.args.get("q", ""))
    return jsonify(ok=True, peer=describe(c, [address])[address])


@bp.get("/api/messages")
def history():
    user = current_user()
    c, me_addr = conn(), user["address"]
    peer = parse_address(request.args.get("peer"))
    if peer is None:
        raise ApiError(400, "Не указан собеседник")
    before = int_value(request.args.get("before"), 2 ** 62, 1, 2 ** 62, "before")
    limit = int_value(request.args.get("limit"), 50, 1, 100, "limit")
    rows = c.execute("SELECT * FROM messages WHERE ((src = ? AND dst = ?) OR (src = ? AND dst = ?)) AND id < ? "
                     "ORDER BY id DESC LIMIT ?", (me_addr, peer, peer, me_addr, before, limit)).fetchall()
    return jsonify(ok=True, peer=describe(c, [peer])[peer], items=messages_json(c, rows, me_addr),
                   more=len(rows) == limit)


@bp.get("/api/updates")
def updates():
    user = current_user()
    c, me_addr = conn(), user["address"]
    housekeeping(c)
    since = int_value(request.args.get("since"), 0, 0, 2 ** 62, "since")
    seq = db.get_meta(c, "seq")  # сначала номер, потом выборка: изменения после него придут в следующий раз
    if since > seq:  # база пересоздана — клиенту нужно перечитать всё
        return jsonify(ok=True, reset=True, seq=seq, more=False, messages=[], status=status_json(c))
    rows = c.execute("SELECT * FROM messages WHERE seq > ? AND seq <= ? AND (src = ? OR dst = ?) "
                     "ORDER BY seq LIMIT ?", (since, seq, me_addr, me_addr, UPDATES_MAX)).fetchall()
    more = len(rows) == UPDATES_MAX
    if more:
        seq = rows[-1]["seq"]
    cfg = current_app.config
    left = max(0, cfg["RADIO_PER_DAY"] - quotas.radio_used_today(c, me_addr, clock()))
    return jsonify(ok=True, reset=False, seq=seq, more=more, messages=messages_json(c, rows, me_addr),
                   status=status_json(c), radio_left_today=left)


@bp.post("/api/messages")
def send():
    user = current_user()
    b = body()
    c, cfg, me_addr = conn(), current_app.config, user["address"]
    dst = resolve(c, b.get("to"))
    text = clean_text(b.get("text"))
    frames, position = 0, None
    with db.transaction(c):
        t = clock()
        if is_user(dst):
            wait = quotas.web_wait(c, me_addr, cfg["WEB_PER_MIN"], t)
            if wait:
                raise ApiError(429, "Не так быстро: подождите немного", retry_after=wait)
            mid = insert_message(c, me_addr, dst, text, "web", "sent")
        else:
            wait = quotas.radio_wait(c, me_addr, cfg["RADIO_PER_DAY"], t)
            if wait:
                raise ApiError(429, f"Через спутник можно отправить {cfg['RADIO_PER_DAY']} сообщений в сутки",
                               retry_after=wait)
            if quotas.queue_size(c) >= cfg["QUEUE_MAX"]:
                raise ApiError(503, "Очередь на спутник переполнена, попробуйте позже")
            frames = codec.frames_needed(text, cfg["FRAME_MAX"])
            if frames > codec.MAX_FRAGMENTS:
                raise ApiError(413, f"Сообщение не помещается в {codec.MAX_FRAGMENTS} радиокадров")
            mid = insert_message(c, me_addr, dst, text, "radio", "queued", db.next_radio_id(c))
            position = c.execute("SELECT COUNT(*) FROM messages WHERE status IN ('queued', 'uplinked') AND id <= ?",
                                 (mid,)).fetchone()[0]
    row = c.execute("SELECT * FROM messages WHERE id = ?", (mid,)).fetchone()
    seen = is_terminal(dst) and c.execute("SELECT 1 FROM terminals WHERE address = ?", (dst,)).fetchone() is not None
    return jsonify(ok=True, message=messages_json(c, [row], me_addr)[0], frames=frames,
                   queue_position=position, terminal_seen=seen if is_terminal(dst) else None), 201


@bp.get("/api/status")
def public_status():
    c = conn()
    housekeeping(c)
    return jsonify(ok=True, **status_json(c))


# ---------- ЦУП ----------
def station():
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        raise ApiError(401, "Нужен заголовок Authorization: Bearer <токен станции>")
    digest = hashlib.sha256(auth[7:].strip().encode("utf-8")).hexdigest()
    c = conn()
    found = None
    for row in c.execute("SELECT * FROM stations").fetchall():
        if hmac.compare_digest(row["token_hash"], digest):
            found = row
    if found is None:
        raise ApiError(401, "Неверный токен станции")
    with db.transaction(c):
        c.execute("UPDATE stations SET last_seen = ? WHERE id = ?", (clock(), found["id"]))
    return found


def log_frame(c, st_id, direction, raw_hex, result):
    c.execute("INSERT INTO frames(ts, station_id, direction, hex, result) VALUES (?, ?, ?, ?, ?)",
              (clock(), st_id, direction, raw_hex, result))


@bp.post("/gs/v1/uplink")
def gs_uplink():
    """Выдаёт очередь на передачу: по кругу между отправителями, не больше max_frames кадров."""
    st = station()
    b = body()
    limit = int_value(b.get("limit"), 10, 1, 100, "limit")
    budget = int_value(b.get("max_frames"), 40, 1, 1000, "max_frames")
    c, cfg = conn(), current_app.config
    housekeeping(c, force=True)
    lease = clock() + cfg["LEASE_SEC"]
    items = []
    with db.transaction(c):
        rows = c.execute("SELECT * FROM (SELECT m.*, ROW_NUMBER() OVER (PARTITION BY src ORDER BY id) AS rn "
                         "FROM messages m WHERE status = 'queued') ORDER BY rn, id").fetchall()
        for r in rows:
            if len(items) >= limit or budget <= 0:
                break
            try:
                frames = codec.encode_message(r["radio_id"], r["src"], r["dst"], r["text"], cfg["FRAME_MAX"])
            except ValueError:
                touch(c, r["id"], status="failed")
                continue
            if len(frames) > budget:
                continue
            budget -= len(frames)
            touch(c, r["id"], status="uplinked", station_id=st["id"], leased_until=lease)
            items.append({"message_id": r["id"], "radio_id": r["radio_id"], "src": r["src"], "dst": r["dst"],
                          "frames": [codec.to_hex(f) for f in frames]})
    return jsonify(ok=True, lease_until=lease, items=items)


@bp.post("/gs/v1/uplink/result")
def gs_uplink_result():
    st = station()
    results = body().get("results")
    if not isinstance(results, list) or len(results) > 500:
        raise ApiError(400, "results: ожидается список до 500 элементов")
    c, cfg = conn(), current_app.config
    updated, errors = 0, []
    with db.transaction(c):
        for i, item in enumerate(results):
            mid = item.get("message_id") if isinstance(item, dict) else None
            r = c.execute("SELECT * FROM messages WHERE id = ? AND station_id = ? AND status = 'uplinked'",
                          (mid, st["id"])).fetchone() if isinstance(mid, int) else None
            if r is None:
                errors.append({"index": i, "error": "Сообщение не выдавалось этой станции или уже обработано"})
                continue
            frames = codec.encode_message(r["radio_id"], r["src"], r["dst"], r["text"], cfg["FRAME_MAX"])
            if item.get("ok") is True:
                touch(c, r["id"], status="on_satellite", leased_until=None)
                for f in frames:
                    log_frame(c, st["id"], "up", codec.to_hex(f), "ok")
            else:
                attempts = r["attempts"] + 1
                status = "failed" if attempts >= cfg["MAX_ATTEMPTS"] else "queued"
                touch(c, r["id"], status=status, attempts=attempts, station_id=None, leased_until=None)
                log_frame(c, st["id"], "up", codec.to_hex(frames[0]), f"error: {str(item.get('error', ''))[:200]}")
            updated += 1
    return jsonify(ok=True, updated=updated, errors=errors)


def upsert_terminal(c, address):
    """Терминал регистрируется сам по первому кадру. True — если он новый."""
    t = clock()
    cur = c.execute("UPDATE terminals SET last_seen = ?, frames_rx = frames_rx + 1 WHERE address = ?", (t, address))
    if cur.rowcount:
        return False
    c.execute("INSERT INTO terminals(address, first_seen, last_seen, frames_rx) VALUES (?, ?, ?, 1)", (address, t, t))
    return True


def handle_ack(c, fr):
    r = c.execute("SELECT id, status FROM messages WHERE via = 'radio' AND radio_id = ? AND dst = ? AND src = ? "
                  "AND created_at > ? ORDER BY id DESC LIMIT 1",
                  (fr.msg_id, fr.src, fr.dst, clock() - DUP_WINDOW)).fetchone()
    if r is None:
        raise ValueError(f"ACK на неизвестное сообщение {fr.msg_id}")
    if r["status"] == "delivered":
        return "dup"
    touch(c, r["id"], status="delivered", leased_until=None)
    return "ok"


def handle_msg(c, fr):
    t = clock()
    if c.execute("SELECT 1 FROM messages WHERE via = 'radio' AND src = ? AND radio_id = ? AND created_at > ?",
                 (fr.src, fr.msg_id, t - DUP_WINDOW)).fetchone():
        return "dup"
    if is_user(fr.dst) and not c.execute("SELECT 1 FROM users WHERE address = ?", (fr.dst,)).fetchone():
        raise ValueError(f"Пользователя #{fr.dst} нет")
    cur = c.execute("INSERT OR IGNORE INTO fragments(src, msg_id, idx, count, dst, data, received_at) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?)", (fr.src, fr.msg_id, fr.index, fr.count, fr.dst, fr.data, t))
    if not cur.rowcount:
        return "dup"
    key = (fr.src, fr.msg_id)
    shape = c.execute("SELECT COUNT(DISTINCT count) AS counts, COUNT(DISTINCT dst) AS dsts, COUNT(*) AS n "
                      "FROM fragments WHERE src = ? AND msg_id = ?", key).fetchone()
    if shape["counts"] > 1 or shape["dsts"] > 1:
        c.execute("DELETE FROM fragments WHERE src = ? AND msg_id = ?", key)
        raise ValueError("Фрагменты сообщения противоречат друг другу, сборка сброшена")
    if shape["n"] < fr.count:
        return "ok"
    parts = [r["data"] for r in c.execute("SELECT data FROM fragments WHERE src = ? AND msg_id = ? ORDER BY idx", key)]
    c.execute("DELETE FROM fragments WHERE src = ? AND msg_id = ?", key)
    # сообщение другому терминалу уходит обратно в очередь — ретрансляция через сервер
    status = "queued" if is_terminal(fr.dst) else "received"
    insert_message(c, fr.src, fr.dst, codec.assemble(parts), "radio", status, fr.msg_id)
    return "ok"


@bp.post("/gs/v1/downlink")
def gs_downlink():
    st = station()
    frames = body().get("frames")
    if not isinstance(frames, list) or not 1 <= len(frames) <= DOWNLINK_MAX:
        raise ApiError(400, f"frames: ожидается список от 1 до {DOWNLINK_MAX} кадров")
    c = conn()
    accepted, duplicates, new_terminals, errors = 0, 0, [], []
    with db.transaction(c):
        for i, item in enumerate(frames):
            raw_hex = item.get("hex") if isinstance(item, dict) else item
            try:
                fr = codec.decode_frame(codec.from_hex(raw_hex))
                if not is_terminal(fr.src):
                    raise ValueError(f"Отправитель {fr.src} вне диапазона терминалов 1–999")
                if not (is_terminal(fr.dst) or is_user(fr.dst)):
                    raise ValueError(f"Получатель {fr.dst} вне допустимых диапазонов")
                if upsert_terminal(c, fr.src):
                    new_terminals.append(fr.src)
                result = (handle_ack if fr.kind == codec.KIND_ACK else handle_msg)(c, fr)
            except ValueError as exc:
                errors.append({"index": i, "error": str(exc)})
                log_frame(c, st["id"], "down", str(raw_hex)[:600], f"error: {exc}")
                continue
            log_frame(c, st["id"], "down", codec.to_hex(codec.from_hex(raw_hex)), result)
            if result == "dup":
                duplicates += 1
            else:
                accepted += 1
    return jsonify(ok=True, accepted=accepted, duplicates=duplicates, new_terminals=new_terminals, errors=errors)


@bp.post("/gs/v1/heartbeat")
def gs_heartbeat():
    st = station()
    event = body().get("event", "ping")
    if event not in ("ping", "aos", "los"):
        raise ApiError(400, 'event: допустимо "ping", "aos" или "los"')
    c = conn()
    if event != "ping":
        with db.transaction(c):
            c.execute(f"UPDATE stations SET last_{event} = ? WHERE id = ?", (clock(), st["id"]))
    return jsonify(ok=True, status=status_json(c))


# ---------- интерфейс ----------
@bp.get("/")
@bp.get("/<path:path>")
def spa(path=""):
    if path.startswith(("api/", "gs/", "static/")):
        raise ApiError(404, "Не найдено")
    if not os.path.exists(os.path.join(STATIC, "index.html")):
        return ("Интерфейс не собран: cd frontend && npm install && npm run build", 503,
                {"Content-Type": "text/plain; charset=utf-8"})
    resp = send_from_directory(STATIC, "index.html")
    resp.headers["Cache-Control"] = "no-cache"
    return resp


# ---------- команды админа ----------
def user_or_fail(c, username):
    row = c.execute("SELECT * FROM users WHERE username = ?", (username.strip().lower(),)).fetchone()
    if row is None:
        raise click.ClickException(f"Пользователь {username} не найден (сначала зарегистрируйтесь на сайте)")
    return row


def fmt_time(ts):
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M") if ts else "—"


@bp.cli.command("create-admin")
@click.argument("username")
def cli_create_admin(username):
    """Делает зарегистрированного пользователя админом."""
    c = conn()
    user = user_or_fail(c, username)
    with db.transaction(c):
        c.execute("UPDATE users SET is_admin = 1 WHERE id = ?", (user["id"],))
    click.echo(f"{user['username']} (#{user['address']}) теперь админ")


@bp.cli.command("ban")
@click.argument("username")
def cli_ban(username):
    """Блокирует пользователя: выход везде, его очередь на спутник снимается."""
    c = conn()
    user = user_or_fail(c, username)
    with db.transaction(c):
        c.execute("UPDATE users SET banned = 1, session_version = session_version + 1 WHERE id = ?", (user["id"],))
        rows = c.execute("SELECT id FROM messages WHERE src = ? AND status = 'queued'", (user["address"],)).fetchall()
        for r in rows:
            touch(c, r["id"], status="failed")
    click.echo(f"{user['username']} заблокирован, снято из очереди: {len(rows)}")


@bp.cli.command("unban")
@click.argument("username")
def cli_unban(username):
    c = conn()
    user = user_or_fail(c, username)
    with db.transaction(c):
        c.execute("UPDATE users SET banned = 0 WHERE id = ?", (user["id"],))
    click.echo(f"{user['username']} разблокирован")


@bp.cli.command("add-station")
@click.argument("name")
def cli_add_station(name):
    """Создаёт станцию (ЦУП) и печатает её токен — один раз."""
    c = conn()
    token = secrets.token_urlsafe(32)
    try:
        with db.transaction(c):
            c.execute("INSERT INTO stations(name, token_hash) VALUES (?, ?)",
                      (name, hashlib.sha256(token.encode("utf-8")).hexdigest()))
    except sqlite3.IntegrityError:
        raise click.ClickException(f"Станция {name} уже есть")
    click.echo(f"Станция {name} создана. Токен (больше не покажется):\n{token}")


@bp.cli.command("remove-station")
@click.argument("name")
def cli_remove_station(name):
    c = conn()
    with db.transaction(c):
        removed = c.execute("DELETE FROM stations WHERE name = ?", (name,)).rowcount
    if not removed:
        raise click.ClickException(f"Станции {name} нет")
    click.echo(f"Станция {name} удалена, её токен больше не действует")


@bp.cli.command("list-stations")
def cli_list_stations():
    for r in conn().execute("SELECT * FROM stations ORDER BY id"):
        click.echo(f"{r['name']:<16} на связи {fmt_time(r['last_seen'])}  AOS {fmt_time(r['last_aos'])}  "
                   f"LOS {fmt_time(r['last_los'])}")


@bp.cli.command("list-terminals")
def cli_list_terminals():
    """Терминалы, выходившие в эфир (регистрируются сами по первому кадру)."""
    for r in conn().execute("SELECT * FROM terminals ORDER BY address"):
        click.echo(f"#{r['address']:<4} {r['name'] or '':<20} кадров {r['frames_rx']:<6} "
                   f"впервые {fmt_time(r['first_seen'])}  последний {fmt_time(r['last_seen'])}")


@bp.cli.command("name-terminal")
@click.argument("address", type=int)
@click.argument("name")
def cli_name_terminal(address, name):
    """Подпись терминала в справочнике."""
    c = conn()
    with db.transaction(c):
        changed = c.execute("UPDATE terminals SET name = ? WHERE address = ?", (name.strip()[:40], address)).rowcount
    if not changed:
        raise click.ClickException(f"Терминал #{address} ещё не выходил в эфир")
    click.echo(f"Терминал #{address}: {name}")


@bp.cli.command("queue")
def cli_queue():
    """Сообщения, которые ждут спутника."""
    c = conn()
    rows = c.execute("SELECT * FROM messages WHERE status IN ('queued', 'uplinked', 'on_satellite') "
                     "ORDER BY id").fetchall()
    for r in rows:
        click.echo(f"{r['id']:>6} {r['status']:<13} #{r['src']} → #{r['dst']}  попыток {r['attempts']}  "
                   f"{fmt_time(r['created_at'])}  {r['text'][:40]!r}")
    click.echo(f"Всего: {len(rows)}")


@bp.cli.command("backup")
@click.option("--dir", "target_dir", default=None, help="Папка для копий (по умолчанию backups рядом с БД)")
@click.option("--keep", default=7, show_default=True, help="Сколько копий хранить")
def cli_backup(target_dir, keep):
    """Резервная копия БД без остановки сервера."""
    cfg = current_app.config
    target_dir = target_dir or cfg["BACKUP_DIR"] or os.path.join(os.path.dirname(os.path.abspath(cfg["DB_PATH"])),
                                                                 "backups")
    os.makedirs(target_dir, exist_ok=True)
    path = os.path.join(target_dir, f"glebotron-{datetime.now():%Y%m%d-%H%M%S}.db")
    db.backup(conn(), path)
    for old in sorted(glob.glob(os.path.join(target_dir, "glebotron-*.db")))[:-keep]:
        os.remove(old)
    click.echo(path)


# ---------- сборка ----------
def create_app(overrides=None):
    app = Flask(__name__, static_folder=STATIC, static_url_path="/static")
    app.config.update(default_config())
    app.config.update(overrides or {})
    if not app.config["SECRET_KEY"]:
        app.config["SECRET_KEY"] = secrets.token_hex(32)
        app.logger.warning("GLEBOTRON_SECRET_KEY не задан: сессии сбросятся при перезапуске")

    origin = urlsplit(app.config["ORIGIN"])
    # 127.0.0.1:5001 разрешён всегда: так ходят install.sh и проверки на самом сервере.
    # DNS rebinding это не открывает — у такой атаки в Host будет чужое имя.
    hosts, origins = {origin.netloc, "127.0.0.1:5001", "localhost:5001"}, {app.config["ORIGIN"]}
    if origin.hostname in ("127.0.0.1", "localhost"):
        port = f":{origin.port}" if origin.port else ""
        for host in ("127.0.0.1", "localhost"):
            hosts.add(host + port)
            origins.add(f"{origin.scheme}://{host}{port}")
    app.config.update(
        ALLOWED_HOSTS=hosts, ALLOWED_ORIGINS=origins,
        SESSION_COOKIE_NAME="glebotron", SESSION_COOKIE_HTTPONLY=True, SESSION_COOKIE_SAMESITE="Lax",
        SESSION_COOKIE_SECURE=origin.scheme == "https", PERMANENT_SESSION_LIFETIME=timedelta(days=30),
        MAX_CONTENT_LENGTH=256 * 1024,
    )
    app.json.ensure_ascii = False
    app.extensions["glebotron"] = {"housekeep_at": 0.0}
    app.wsgi_app = ProxyFix(app.wsgi_app, x_for=1, x_proto=1)  # перед приложением ровно один nginx

    with contextlib.closing(db.connect(app.config["DB_PATH"])) as c:
        db.migrate(c)
    app.register_blueprint(bp)
    return app


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):  # консоль Windows без UTF-8 не печатает кириллицу
        stream.reconfigure(encoding="utf-8", errors="replace")
    print("Глеботрон запущен: http://127.0.0.1:5001")
    create_app().run(host="127.0.0.1", port=5001, debug=False, threaded=True)
