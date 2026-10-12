from conftest import ORIGIN, register


def test_register_login_logout(client):
    api = client()
    me = register(api, "Gleb")
    assert me["username"] == "gleb"
    assert 1000 <= me["address"] <= 65000
    assert api.get("/api/me").get_json()["me"]["address"] == me["address"]

    assert api.post("/api/logout").status_code == 200
    assert api.get("/api/me").status_code == 401

    assert api.post("/api/login", json={"username": "gleb", "password": "wrong-pass"}).status_code == 401
    r = api.post("/api/login", json={"username": "GLEB", "password": "password123"})
    assert r.status_code == 200 and r.get_json()["me"]["address"] == me["address"]


def test_register_validation(client):
    api = client()
    register(api, "anton")
    cases = [
        ({"username": "anton", "password": "password123"}, 409),
        ({"username": "ab", "password": "password123"}, 400),
        ({"username": "Антон", "password": "password123"}, 400),
        ({"username": "admin", "password": "password123"}, 400),
        ({"username": "valid_name", "password": "short"}, 400),
        ({"username": "valid_name", "password": 12345678}, 400),
    ]
    for payload, code in cases:
        assert client().post("/api/register", json=payload).status_code == code, payload


def test_addresses_are_random_and_unique(client):
    addresses = {register(client(), f"user{i}")["address"] for i in range(20)}
    assert len(addresses) == 20
    assert all(1000 <= a <= 65000 for a in addresses)
    assert sorted(addresses) != list(range(min(addresses), min(addresses) + 20))


def test_post_requires_same_origin_and_json(app):
    raw = app.test_client()
    body = {"username": "eve", "password": "password123"}
    assert raw.post("/api/register", json=body, base_url=ORIGIN,
                    headers={"Origin": "https://evil.example"}).status_code == 403
    assert raw.post("/api/register", json=body, base_url=ORIGIN).status_code == 403  # без Origin
    assert raw.post("/api/register", data="x", base_url=ORIGIN,
                    headers={"Origin": ORIGIN}).status_code == 415
    assert raw.get("/api/status", base_url="http://evil.example").status_code == 403  # чужой Host


def test_web_to_web_delivered_via_updates(client):
    a, b = client(), client()
    me_a, me_b = register(a, "alice"), register(b, "bob")
    seq = b.get("/api/me").get_json()["me"]["seq"]

    r = a.post("/api/messages", json={"to": "bob", "text": "  Привет, Боб!\r\n "})
    assert r.status_code == 201
    msg = r.get_json()["message"]
    assert (msg["status"], msg["via"], msg["text"]) == ("sent", "web", "Привет, Боб!")

    upd = b.get(f"/api/updates?since={seq}").get_json()
    assert [m["text"] for m in upd["messages"]] == ["Привет, Боб!"]
    assert upd["messages"][0]["peer"] == me_a["address"] and not upd["messages"][0]["out"]
    assert b.get(f"/api/updates?since={upd['seq']}").get_json()["messages"] == []

    conv = b.get("/api/conversations").get_json()["items"]
    assert conv[0]["peer"]["name"] == "alice"
    hist = a.get(f"/api/messages?peer={me_b['address']}").get_json()
    assert [m["text"] for m in hist["items"]] == ["Привет, Боб!"]


def test_send_validation(client):
    api = client()
    me = register(api, "alice")
    stranger = 1000 if me["address"] != 1000 else 1001
    assert api.post("/api/messages", json={"to": "nobody", "text": "hi"}).status_code == 404
    assert api.post("/api/messages", json={"to": stranger, "text": "hi"}).status_code == 404
    assert api.post("/api/messages", json={"to": 0, "text": "hi"}).status_code == 400
    assert api.post("/api/messages", json={"to": 7, "text": "   "}).status_code == 400
    assert api.post("/api/messages", json={"to": 7, "text": "Я" * 81}).status_code == 413


def test_radio_message_to_unseen_terminal(client):
    api = client()
    register(api, "alice")
    r = api.post("/api/messages", json={"to": "#500", "text": "Есть кто?"})
    assert r.status_code == 201
    d = r.get_json()
    assert d["terminal_seen"] is False and d["frames"] == 1 and d["queue_position"] == 1
    assert d["message"]["status"] == "queued" and d["message"]["to"]["kind"] == "terminal"


def test_radio_daily_limit(app, client, clock):
    app.config["RADIO_PER_DAY"] = 3
    api = client()
    register(api, "alice")
    for i in range(3):
        assert api.post("/api/messages", json={"to": 7, "text": f"m{i}"}).status_code == 201
        clock.advance(10)
    r = api.post("/api/messages", json={"to": 7, "text": "lim"})
    assert r.status_code == 429 and r.get_json()["retry_after"] > 0
    assert api.get("/api/me").get_json()["me"]["limits"]["radio_left_today"] == 0
    clock.advance(24 * 3600)
    assert api.post("/api/messages", json={"to": 7, "text": "next day"}).status_code == 201


def test_queue_full(app, client):
    app.config["QUEUE_MAX"] = 2
    a, b = client(), client()
    register(a, "alice")
    register(b, "bob")
    assert a.post("/api/messages", json={"to": 7, "text": "1"}).status_code == 201
    assert a.post("/api/messages", json={"to": 7, "text": "2"}).status_code == 201
    assert b.post("/api/messages", json={"to": 7, "text": "3"}).status_code == 503


def test_web_rate_limit(app, client):
    app.config["WEB_PER_MIN"] = 2
    a = client()
    register(a, "alice")
    register(client(), "bob")
    assert a.post("/api/messages", json={"to": "bob", "text": "1"}).status_code == 201
    assert a.post("/api/messages", json={"to": "bob", "text": "2"}).status_code == 201
    assert a.post("/api/messages", json={"to": "bob", "text": "3"}).status_code == 429


def test_register_limit_per_ip(app):
    from conftest import Api
    api = Api(app, ip="10.9.9.9")
    for i in range(5):
        register(Api(app, ip="10.9.9.9"), f"u{i}x")
    assert api.post("/api/register", json={"username": "u6x", "password": "password123"}).status_code == 429


def test_login_bruteforce_limit(client):
    register(client(), "alice")
    api = client()
    for _ in range(10):
        assert api.post("/api/login", json={"username": "alice", "password": "nope-nope"}).status_code == 401
    assert api.post("/api/login", json={"username": "alice", "password": "password123"}).status_code == 429


def test_ban_invalidates_session_and_queue(app, client):
    api = client()
    register(api, "spammer")
    api.post("/api/messages", json={"to": 7, "text": "spam"})
    out = app.test_cli_runner().invoke(args=["ban", "spammer"]).output
    assert "снято из очереди: 1" in out
    assert api.get("/api/me").status_code == 401
    r = api.post("/api/login", json={"username": "spammer", "password": "password123"})
    assert r.status_code == 403


def test_lookup_and_directory(client):
    api = client()
    me = register(api, "alice")
    assert api.get("/api/lookup?q=alice").get_json()["peer"]["address"] == me["address"]
    peer = api.get("/api/lookup?q=%2312").get_json()["peer"]
    assert peer == {"address": 12, "name": "Терминал #12", "kind": "terminal", "seen": False}
    assert api.get("/api/lookup?q=ghost").status_code == 404
    assert api.get("/api/directory").get_json()["items"] == []


def test_production_origin_and_local_health_check(tmp_path, clock):
    import app as glb
    prod = glb.create_app({"DB_PATH": str(tmp_path / "prod.db"), "SECRET_KEY": "x",
                           "ORIGIN": "https://glebotron.cosmos1562.space"})
    raw = prod.test_client()
    # так install.sh проверяет, что сервис поднялся
    assert raw.get("/api/status", base_url="http://127.0.0.1:5001").status_code == 200
    assert raw.get("/api/status", base_url="https://glebotron.cosmos1562.space").status_code == 200
    body = {"username": "eve", "password": "password123"}
    assert raw.post("/api/register", json=body, base_url="https://glebotron.cosmos1562.space",
                    headers={"Origin": "http://127.0.0.1:5001"}).status_code == 403
    r = raw.post("/api/register", json=body, base_url="https://glebotron.cosmos1562.space",
                 headers={"Origin": "https://glebotron.cosmos1562.space"})
    assert r.status_code == 201
    cookie = r.headers["Set-Cookie"]
    assert "Secure" in cookie and "HttpOnly" in cookie and "SameSite=Lax" in cookie


def test_updates_reset_when_cursor_ahead(client):
    api = client()
    register(api, "alice")
    d = api.get("/api/updates?since=999999").get_json()
    assert d["reset"] is True and d["seq"] < 999999


def test_public_status_and_headers(client):
    r = client().get("/api/status")
    assert r.status_code == 200
    assert r.get_json()["queue"] == 0 and r.get_json()["station_online"] is False
    assert "frame-ancestors 'none'" in r.headers["Content-Security-Policy"]
    assert r.headers["Cache-Control"] == "no-store"
