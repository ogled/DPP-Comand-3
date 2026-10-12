"""Имитация ЦУП и пролёта спутника: проверка Глеботрона без железа.

  python tools/fake_gs.py --url http://127.0.0.1:5001 --token T pass --ack
  python tools/fake_gs.py --url http://127.0.0.1:5001 --token T send --from 7 --to 48213 --text "Привет с поля"
  python tools/fake_gs.py --url http://127.0.0.1:5001 --token T replay

Токен станции: flask --app app add-station test (на сервере: sudo glebotron add-station test).
"""
import argparse
import json
import os
import random
import sys
import tempfile
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import codec  # noqa: E402

LAST = os.path.join(tempfile.gettempdir(), "glebotron_fake_gs_last.json")


class Station:
    def __init__(self, url, token):
        self.url = url.rstrip("/")
        self.token = token

    def call(self, path, payload):
        req = urllib.request.Request(self.url + path, data=json.dumps(payload).encode("utf-8"), method="POST",
                                     headers={"Content-Type": "application/json",
                                              "Authorization": f"Bearer {self.token}"})
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                return json.load(resp)
        except urllib.error.HTTPError as exc:
            try:
                error = json.load(exc).get("error")
            except ValueError:
                error = exc.reason
            sys.exit(f"{path}: HTTP {exc.code} — {error}")

    def downlink(self, frames):
        with open(LAST, "w", encoding="utf-8") as fh:
            json.dump(frames, fh)
        r = self.call("/gs/v1/downlink", {"frames": [{"hex": h} for h in frames]})
        print(f"downlink: принято {r['accepted']}, дубликатов {r['duplicates']}, "
              f"новые терминалы {r['new_terminals'] or '—'}")
        for e in r["errors"]:
            print(f"  кадр {e['index']}: {e['error']}")


def do_pass(st, args):
    st.call("/gs/v1/heartbeat", {"event": "aos"})
    print("AOS — пролёт начался")
    items = st.call("/gs/v1/uplink", {"limit": args.limit, "max_frames": args.max_frames})["items"]
    print(f"uplink: выдано сообщений {len(items)}")
    results, acks = [], []
    for item in items:
        ok = random.random() >= args.fail_rate
        print(f"  #{item['message_id']} {item['src']} → {item['dst']}, кадров {len(item['frames'])}: "
              f"{'передано' if ok else 'ошибка передачи'}")
        for h in item["frames"]:
            print(f"    {h}")
        results.append({"message_id": item["message_id"], "ok": ok, **({} if ok else {"error": "имитация сбоя"})})
        if ok and args.ack and 1 <= item["dst"] <= 999:
            acks.append(codec.to_hex(codec.encode_ack(item["radio_id"], item["dst"], item["src"])))
    if results:
        r = st.call("/gs/v1/uplink/result", {"results": results})
        print(f"uplink/result: обновлено {r['updated']}")
    if acks:
        print("ACK от терминалов:")
        st.downlink(acks)
    st.call("/gs/v1/heartbeat", {"event": "los"})
    print("LOS — пролёт закончился")


def do_send(st, args):
    msg_id = args.id if args.id is not None else random.randint(0, 0xFFFF)
    frames = codec.encode_message(msg_id, args.src, args.dst, args.text, args.frame_max)
    print(f"Терминал #{args.src} → #{args.dst}, msg_id {msg_id}, кадров {len(frames)}")
    st.downlink([codec.to_hex(f) for f in frames])


def do_replay(st, _args):
    try:
        with open(LAST, encoding="utf-8") as fh:
            frames = json.load(fh)
    except OSError:
        sys.exit("Нечего повторять: сначала выполните send или pass --ack")
    print(f"Повтор {len(frames)} кадров")
    st.downlink(frames)


def main():
    p = argparse.ArgumentParser(description="Имитация ЦУП для Глеботрона")
    p.add_argument("--url", default="http://127.0.0.1:5001")
    p.add_argument("--token", default=os.environ.get("GLEBOTRON_TOKEN"), help="или env GLEBOTRON_TOKEN")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("pass", help="один пролёт: AOS, uplink, результат, ACK, LOS")
    sp.add_argument("--ack", action="store_true", help="терминалы подтверждают приём")
    sp.add_argument("--fail-rate", type=float, default=0.0, help="доля неудачных передач, 0..1")
    sp.add_argument("--limit", type=int, default=10)
    sp.add_argument("--max-frames", type=int, default=40)
    sp.set_defaults(func=do_pass)

    ss = sub.add_parser("send", help="сообщение от терминала, как будто пришло с радио")
    ss.add_argument("--from", dest="src", type=int, required=True, help="ID терминала 1–999")
    ss.add_argument("--to", dest="dst", type=int, required=True, help="адрес получателя")
    ss.add_argument("--text", required=True)
    ss.add_argument("--id", type=int, default=None, help="msg_id (по умолчанию случайный)")
    ss.add_argument("--frame-max", type=int, default=39)
    ss.set_defaults(func=do_send)

    sr = sub.add_parser("replay", help="повторить последний downlink — проверка дубликатов")
    sr.set_defaults(func=do_replay)

    args = p.parse_args()
    if not args.token:
        p.error("нужен --token или переменная окружения GLEBOTRON_TOKEN")
    args.func(Station(args.url, args.token), args)


if __name__ == "__main__":
    main()
