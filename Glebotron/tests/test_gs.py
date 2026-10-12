import codec
from conftest import register


def hexes(frames):
    return [{"hex": codec.to_hex(f)} for f in frames]


def statuses(api, peer):
    return [m["status"] for m in api.get(f"/api/messages?peer={peer}").get_json()["items"]]


def test_bad_token(station):
    assert station("/gs/v1/heartbeat", token_override="wrong").status_code == 401


def test_full_chain_to_delivered(client, station):
    api = client()
    me = register(api, "alice")
    text = "Привет с сайта, как слышно? Приём!"
    api.post("/api/messages", json={"to": 7, "text": text})

    up = station("/gs/v1/uplink", {"limit": 10, "max_frames": 40}).get_json()
    [item] = up["items"]
    assert item["dst"] == 7 and item["src"] == me["address"] and len(item["frames"]) == codec.frames_needed(text, 39) > 1
    assert statuses(api, 7) == ["uplinked"]
    assert station("/gs/v1/uplink").get_json()["items"] == []  # уже выдано

    r = station("/gs/v1/uplink/result", {"results": [{"message_id": item["message_id"], "ok": True}]}).get_json()
    assert r["updated"] == 1
    assert statuses(api, 7) == ["on_satellite"]

    ack = codec.encode_ack(item["radio_id"], 7, me["address"])
    r = station("/gs/v1/downlink", {"frames": hexes([ack])}).get_json()
    assert (r["accepted"], r["duplicates"], r["new_terminals"]) == (1, 0, [7])
    assert statuses(api, 7) == ["delivered"]

    r = station("/gs/v1/downlink", {"frames": hexes([ack])}).get_json()
    assert (r["accepted"], r["duplicates"]) == (0, 1)


def test_lease_expiry_and_failures(app, client, station, clock):
    api = client()
    register(api, "alice")
    api.post("/api/messages", json={"to": 7, "text": "x"})
    mid = station("/gs/v1/uplink").get_json()["items"][0]["message_id"]

    clock.advance(app.config["LEASE_SEC"] + 1)
    assert station("/gs/v1/uplink").get_json()["items"][0]["message_id"] == mid  # lease истёк — снова выдано

    for attempt in range(app.config["MAX_ATTEMPTS"]):
        station("/gs/v1/uplink/result", {"results": [{"message_id": mid, "ok": False, "error": "tx timeout"}]})
        if attempt < app.config["MAX_ATTEMPTS"] - 1:
            assert station("/gs/v1/uplink").get_json()["items"][0]["message_id"] == mid
    assert statuses(api, 7) == ["failed"]
    assert station("/gs/v1/uplink").get_json()["items"] == []


def test_message_expires_after_week(app, client, station, clock):
    api = client()
    register(api, "alice")
    api.post("/api/messages", json={"to": 7, "text": "x"})
    clock.advance(app.config["EXPIRE_SEC"] + 1)
    assert station("/gs/v1/uplink").get_json()["items"] == []
    assert statuses(api, 7) == ["expired"]


def test_terminal_message_to_user_with_dedup(client, station):
    api = client()
    me = register(api, "alice")
    seq = api.get("/api/me").get_json()["me"]["seq"]
    frames = codec.encode_message(42, 7, me["address"], "Привет с поля! Тут ветрено, но связь есть.", 39)
    assert len(frames) > 1

    r = station("/gs/v1/downlink", {"frames": hexes(reversed(frames))}).get_json()
    assert r["accepted"] == len(frames) and r["errors"] == [] and r["new_terminals"] == [7]

    msgs = api.get(f"/api/updates?since={seq}").get_json()["messages"]
    assert [(m["text"], m["status"], m["from"]["kind"]) for m in msgs] == [
        ("Привет с поля! Тут ветрено, но связь есть.", "received", "terminal")]

    r = station("/gs/v1/downlink", {"frames": hexes(frames)}).get_json()  # повтор с другого пролёта
    assert (r["accepted"], r["duplicates"]) == (0, len(frames))
    assert len(api.get("/api/messages?peer=7").get_json()["items"]) == 1

    directory = api.get("/api/directory").get_json()["items"]
    assert [(t["address"], t["seen"]) for t in directory] == [(7, True)]


def test_terminal_counters_and_spoofing(app, client, station):
    api = client()
    me = register(api, "alice")
    station("/gs/v1/downlink", {"frames": hexes(codec.encode_message(1, 9, me["address"], "a", 39))})
    station("/gs/v1/downlink", {"frames": hexes(codec.encode_message(2, 9, me["address"], "b", 39))})
    out = app.test_cli_runner().invoke(args=["list-terminals"]).output
    assert "#9" in out and "кадров 2" in out

    spoof = codec.encode_message(3, me["address"], 9, "я не терминал", 39)  # из эфира от имени веб-пользователя
    r = station("/gs/v1/downlink", {"frames": hexes(spoof)}).get_json()
    assert r["accepted"] == 0 and "вне диапазона терминалов" in r["errors"][0]["error"]

    r = station("/gs/v1/downlink", {"frames": ["zz", "01 02"]}).get_json()
    assert len(r["errors"]) == 2


def test_relay_terminal_to_terminal(station):
    frames = codec.encode_message(77, 5, 6, "От терминала 5 терминалу 6", 39)
    station("/gs/v1/downlink", {"frames": hexes(frames)})
    [item] = station("/gs/v1/uplink").get_json()["items"]
    assert (item["src"], item["dst"], item["radio_id"]) == (5, 6, 77)
    assert item["frames"] == [codec.to_hex(f) for f in frames]

    station("/gs/v1/uplink/result", {"results": [{"message_id": item["message_id"], "ok": True}]})
    r = station("/gs/v1/downlink", {"frames": hexes([codec.encode_ack(77, 6, 5)])}).get_json()
    assert r["accepted"] == 1 and r["errors"] == []


def test_uplink_round_robin_and_frame_budget(client, station):
    a, b = client(), client()
    register(a, "alice")
    me_b = register(b, "bob")
    for i in range(3):
        a.post("/api/messages", json={"to": 7, "text": f"alice {i}"})
    b.post("/api/messages", json={"to": 7, "text": "bob 0"})

    items = station("/gs/v1/uplink", {"limit": 2}).get_json()["items"]
    assert [i["src"] for i in items][1] == me_b["address"]  # Боб не ждёт, пока уйдёт вся очередь Алисы

    long_text = "Я" * 80  # 6 кадров
    b.post("/api/messages", json={"to": 7, "text": long_text})
    items = station("/gs/v1/uplink", {"max_frames": 3}).get_json()["items"]
    assert sum(len(i["frames"]) for i in items) <= 3


def test_heartbeat_marks_session(client, station):
    assert station("/gs/v1/heartbeat", {"event": "aos"}).get_json()["status"]["last_aos"]
    st = client().get("/api/status").get_json()
    assert st["station_online"] is True and st["last_aos"] is not None
    assert station("/gs/v1/heartbeat", {"event": "boom"}).status_code == 400


def test_backup_command(app, tmp_path):
    out = app.test_cli_runner().invoke(args=["backup", "--dir", str(tmp_path / "b"), "--keep", "2"]).output
    assert out.strip().endswith(".db")
