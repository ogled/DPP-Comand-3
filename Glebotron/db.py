"""SQLite Глеботрона: схема, миграции, счётчики и транзакции.

Сервер работает одним процессом (gunicorn -w 1), поэтому записи
сериализуются общим замком; BEGIN IMMEDIATE защищает от параллельных
CLI-команд на том же файле.
"""
import contextlib
import sqlite3
import threading

SCHEMA_VERSION = 1

SCHEMA = """
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS users(
  id INTEGER PRIMARY KEY,
  username TEXT NOT NULL UNIQUE COLLATE NOCASE,
  password_hash TEXT NOT NULL,
  address INTEGER NOT NULL UNIQUE CHECK(address BETWEEN 1000 AND 65000),
  is_admin INTEGER NOT NULL DEFAULT 0,
  banned INTEGER NOT NULL DEFAULT 0,
  session_version INTEGER NOT NULL DEFAULT 0,
  created_ip TEXT, created_at REAL NOT NULL, last_seen REAL);

CREATE TABLE IF NOT EXISTS terminals(
  address INTEGER PRIMARY KEY CHECK(address BETWEEN 1 AND 999),
  name TEXT,
  first_seen REAL NOT NULL, last_seen REAL, frames_rx INTEGER NOT NULL DEFAULT 0,
  last_lat REAL, last_lon REAL);

CREATE TABLE IF NOT EXISTS stations(
  id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE,
  token_hash TEXT NOT NULL,
  last_seen REAL, last_aos REAL, last_los REAL);

CREATE TABLE IF NOT EXISTS messages(
  id INTEGER PRIMARY KEY,
  src INTEGER NOT NULL, dst INTEGER NOT NULL,
  text TEXT NOT NULL,
  via TEXT NOT NULL,
  radio_id INTEGER,
  status TEXT NOT NULL,
  station_id INTEGER REFERENCES stations(id) ON DELETE SET NULL,
  leased_until REAL, attempts INTEGER NOT NULL DEFAULT 0,
  seq INTEGER NOT NULL,
  created_at REAL NOT NULL, updated_at REAL NOT NULL);
CREATE INDEX IF NOT EXISTS msg_seq ON messages(seq);
CREATE INDEX IF NOT EXISTS msg_pair ON messages(src, dst, id);
CREATE INDEX IF NOT EXISTS msg_dst ON messages(dst, id);
CREATE INDEX IF NOT EXISTS msg_queue ON messages(status, id);
CREATE INDEX IF NOT EXISTS msg_radio ON messages(radio_id, dst);

CREATE TABLE IF NOT EXISTS fragments(
  src INTEGER, msg_id INTEGER, idx INTEGER, count INTEGER, dst INTEGER,
  data BLOB, received_at REAL, PRIMARY KEY(src, msg_id, idx));

CREATE TABLE IF NOT EXISTS frames(
  id INTEGER PRIMARY KEY, ts REAL, station_id INTEGER,
  direction TEXT, hex TEXT, result TEXT);

CREATE TABLE IF NOT EXISTS login_failures(ip TEXT, username TEXT, ts REAL);
CREATE INDEX IF NOT EXISTS login_failures_ts ON login_failures(ts);
"""

write_lock = threading.RLock()


def connect(path):
    conn = sqlite3.connect(path, timeout=5, isolation_level=None, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    conn.execute("PRAGMA busy_timeout=5000")
    return conn


def migrate(conn):
    """Создаёт схему и счётчики. Миграции только добавляющие, по schema_version."""
    with write_lock:
        conn.executescript(SCHEMA)  # executescript сам коммитит, поэтому вне transaction()
    with transaction(conn):
        for key, value in (("schema_version", SCHEMA_VERSION), ("seq", 0), ("radio_counter", 0)):
            conn.execute("INSERT OR IGNORE INTO meta(key, value) VALUES (?, ?)", (key, value))
        version = get_meta(conn, "schema_version")
        if version > SCHEMA_VERSION:
            raise RuntimeError(f"База новее кода: схема {version}, код знает {SCHEMA_VERSION}")
        # здесь будут шаги вида: if version < 2: conn.execute("ALTER TABLE ...")
        conn.execute("UPDATE meta SET value = ? WHERE key = 'schema_version'", (SCHEMA_VERSION,))


@contextlib.contextmanager
def transaction(conn):
    """Пишущая транзакция. Вложенные вызовы идут внутри внешней."""
    with write_lock:
        if conn.in_transaction:
            yield conn
            return
        conn.execute("BEGIN IMMEDIATE")
        try:
            yield conn
        except BaseException:
            conn.execute("ROLLBACK")
            raise
        conn.execute("COMMIT")


def get_meta(conn, key):
    row = conn.execute("SELECT value FROM meta WHERE key = ?", (key,)).fetchone()
    return row["value"] if row else 0


def next_seq(conn):
    """Номер изменения: на нём держится опрос /api/updates?since=. Вызывать внутри transaction()."""
    return conn.execute("UPDATE meta SET value = value + 1 WHERE key = 'seq' RETURNING value").fetchone()[0]


def next_radio_id(conn):
    """msg_id для кадров, которые отправляет сервер (по модулю 65536)."""
    return conn.execute("UPDATE meta SET value = (value + 1) % 65536 WHERE key = 'radio_counter' "
                        "RETURNING value").fetchone()[0]


def backup(conn, target_path):
    dest = sqlite3.connect(target_path)
    try:
        conn.backup(dest)
    finally:
        dest.close()
