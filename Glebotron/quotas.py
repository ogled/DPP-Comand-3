"""Лимиты при открытой регистрации. Считаются запросами к БД, поэтому переживают рестарт.

Каждая проверка возвращает 0, если можно, или число секунд до снятия ограничения.
"""
import math

REGISTER_PER_HOUR = 5
LOGIN_FAILS = 10
LOGIN_WINDOW = 15 * 60
WEB_WINDOW = 60
DAY = 24 * 3600


def _wait(oldest, window, now):
    return max(1, math.ceil(oldest + window - now))


def register_wait(conn, ip, now):
    rows = conn.execute("SELECT created_at FROM users WHERE created_ip = ? AND created_at > ? "
                        "ORDER BY created_at", (ip, now - 3600)).fetchall()
    return _wait(rows[-REGISTER_PER_HOUR]["created_at"], 3600, now) if len(rows) >= REGISTER_PER_HOUR else 0


def login_wait(conn, ip, username, now):
    rows = conn.execute("SELECT ts FROM login_failures WHERE (ip = ? OR username = ?) AND ts > ? "
                        "ORDER BY ts", (ip, username, now - LOGIN_WINDOW)).fetchall()
    return _wait(rows[-LOGIN_FAILS]["ts"], LOGIN_WINDOW, now) if len(rows) >= LOGIN_FAILS else 0


def radio_used_today(conn, address, now):
    return conn.execute("SELECT COUNT(*) FROM messages WHERE src = ? AND via = 'radio' AND created_at > ?",
                        (address, now - DAY)).fetchone()[0]


def radio_wait(conn, address, per_day, now):
    rows = conn.execute("SELECT created_at FROM messages WHERE src = ? AND via = 'radio' AND created_at > ? "
                        "ORDER BY created_at", (address, now - DAY)).fetchall()
    return _wait(rows[-per_day]["created_at"], DAY, now) if len(rows) >= per_day else 0


def web_wait(conn, address, per_minute, now):
    rows = conn.execute("SELECT created_at FROM messages WHERE src = ? AND via = 'web' AND created_at > ? "
                        "ORDER BY created_at", (address, now - WEB_WINDOW)).fetchall()
    return _wait(rows[-per_minute]["created_at"], WEB_WINDOW, now) if len(rows) >= per_minute else 0


def queue_size(conn):
    return conn.execute("SELECT COUNT(*) FROM messages WHERE status IN ('queued', 'uplinked')").fetchone()[0]
