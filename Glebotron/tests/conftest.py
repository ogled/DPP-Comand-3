import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import app as glb  # noqa: E402

ORIGIN = "http://127.0.0.1:5001"


class Clock:
    def __init__(self):
        self.t = 1_760_000_000.0

    def __call__(self):
        return self.t

    def advance(self, seconds):
        self.t += seconds


class Api:
    """Тестовый клиент с правильными Host и Origin, как у браузера на сайте."""

    def __init__(self, app, ip="10.0.0.1"):
        self.client = app.test_client()
        self.ip = ip

    def get(self, path, **kw):
        return self.client.get(path, base_url=ORIGIN, headers={"X-Forwarded-For": self.ip}, **kw)

    def post(self, path, json=None, headers=None, **kw):
        h = {"Origin": ORIGIN, "X-Forwarded-For": self.ip}
        h.update(headers or {})
        return self.client.post(path, json={} if json is None else json, headers=h, base_url=ORIGIN, **kw)


@pytest.fixture
def clock(monkeypatch):
    c = Clock()
    monkeypatch.setattr(glb, "clock", c)
    return c


@pytest.fixture
def app(tmp_path, clock):
    return glb.create_app({"DB_PATH": str(tmp_path / "test.db"), "SECRET_KEY": "test", "ORIGIN": ORIGIN})


@pytest.fixture
def client(app):
    counter = iter(range(1, 10_000))
    return lambda: Api(app, ip=f"10.0.0.{next(counter)}")


def register(api, username, password="password123"):
    r = api.post("/api/register", json={"username": username, "password": password})
    assert r.status_code == 201, r.get_json()
    return r.get_json()["me"]


@pytest.fixture
def station(app):
    """Возвращает функцию запросов к /gs/v1/* с токеном новой станции."""
    out = app.test_cli_runner().invoke(args=["add-station", "test"]).output
    token = out.strip().splitlines()[-1]
    gs = app.test_client()

    def call(path, json=None, token_override=None):
        return gs.post(path, json=json if json is not None else {}, base_url=ORIGIN,
                       headers={"Authorization": f"Bearer {token_override or token}"})

    call.token = token
    return call
