"""DPP · монитор последовательного порта. Локальный Flask-сервер.

Запуск:  python app.py   →  http://127.0.0.1:5000
"""
import json
import random
import struct
import threading
import time
import webbrowser
from datetime import datetime

from flask import Flask, Response, jsonify, request, send_from_directory

from protocol import Protocol

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # приложение должно запускаться и без pyserial (демо-режим)
    serial = None

MARK_LOG = 0x01
MARK_DATA = 0x02
HEADER_SIZE = 12
MAX_PACKET_SIZE = 64
MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE

TYPE_NAMES = {0: "I2C", 1: "SPI", 2: "UART", 3: "Цифровой", 4: "Аналоговый"}
UART_NAMES = {0: "Serial", 1: "Serial1", 2: "Serial2", 3: "Serial3"}
DEMO_PORT = "DEMO"

app = Flask(__name__, static_folder="static", static_url_path="/static")
proto = Protocol()


class PacketParser:
    def __init__(self, callback):
        self.buffer = bytearray()
        self.callback = callback

    def feed(self, data):
        self.buffer.extend(data)
        while self.buffer:
            marker = self.buffer[0]
            if marker == MARK_LOG:
                end = self.buffer.find(b"\n", 1)
                if end < 0:
                    return
                self.callback("log", bytes(self.buffer[1:end]))
                del self.buffer[:end + 1]
                continue
            if marker == MARK_DATA:
                if len(self.buffer) < 3:
                    return
                length = self.buffer[1] | (self.buffer[2] << 8)
                if length > MAX_PACKET_SIZE:
                    del self.buffer[0]
                    continue
                total = 3 + length
                if len(self.buffer) < total:
                    return
                self.callback("data", bytes(self.buffer[3:total]))
                del self.buffer[:total]
                continue
            positions = [p for p in (self.buffer.find(bytes([MARK_LOG])),
                                     self.buffer.find(bytes([MARK_DATA]))) if p >= 0]
            if not positions:
                self.buffer.clear()
                return
            del self.buffer[:min(positions)]


class CcsdsParser:
    """Поток «как есть»: подряд идущие пакеты с 6-байтовым заголовком (как шлёт спутник)."""

    HEADER = 6

    def __init__(self, callback):
        self.buffer = bytearray()
        self.callback = callback
        self.synced = False

    def _total(self, pos, cfg):
        """Длина пакета, начинающегося с pos, или None, если заголовок не подходит."""
        b = self.buffer
        if len(b) - pos < self.HEADER:
            return 0  # пока мало байт
        if (b[pos] >> 5) != int(cfg.get("version", 0)):
            return None
        apid = ((b[pos] & 0x07) << 8) | b[pos + 1]
        if cfg.get("apids") and apid not in cfg["apids"]:
            return None
        total = self.HEADER + ((b[pos + 4] << 8) | b[pos + 5]) + 1
        ok = int(cfg.get("min_packet", self.HEADER + 1)) <= total <= int(cfg.get("max_packet", MAX_PACKET_SIZE))
        return total if ok else None

    def feed(self, data, cfg):
        self.buffer.extend(data)
        while len(self.buffer) >= self.HEADER:
            total = self._total(0, cfg)
            if total is None:
                self.synced = False
                del self.buffer[0]
                continue
            if len(self.buffer) < total:
                return
            if not self.synced:
                # после потери синхронизации доверяем пакету, только если за ним идёт нормальный заголовок
                nxt = self._total(total, cfg)
                if nxt == 0:
                    return
                if nxt is None:
                    del self.buffer[0]
                    continue
                self.synced = True
            self.callback("ccsds", bytes(self.buffer[:total]))
            del self.buffer[:total]


class StreamParser:
    """Выбирает разбор потока по framing.mode из protocol.json (можно менять на лету)."""

    def __init__(self, callback):
        self.mode = None
        self.markers = PacketParser(callback)
        self.ccsds = CcsdsParser(callback)

    def feed(self, data):
        cfg = proto.data.get("framing", {})
        mode = cfg.get("mode", "markers")
        if mode != self.mode:
            self.mode = mode
            self.markers.buffer.clear()
            self.ccsds.buffer.clear()
            self.ccsds.synced = False
        if mode == "ccsds":
            self.ccsds.feed(data, cfg)
        else:
            self.markers.feed(data)


def hx(data):
    return bytes(data).hex(" ")


def parse_packet(data):
    if len(data) < HEADER_SIZE or len(data) > MAX_PACKET_SIZE:
        return None
    size_data = data[11]
    if size_data > len(data) - HEADER_SIZE:
        return None
    packed = data[10]
    return {
        "device_id": data[0],
        "longitude": struct.unpack_from("<i", data, 1)[0],
        "latitude": struct.unpack_from("<i", data, 5)[0],
        "type": data[9],
        "type_input": packed & 0x0F,
        "packet_number": (packed >> 4) & 0x0F,
        "size_data": size_data,
        "payload": data[HEADER_SIZE:HEADER_SIZE + size_data],
        "trailing": data[HEADER_SIZE + size_data:],
    }


def type_name(rec):
    name = TYPE_NAMES.get(rec["type"], f"Неизвестный ({rec['type']})")
    if rec["type"] == 2:
        name += " / " + UART_NAMES.get(rec["type_input"], f"вход {rec['type_input']}")
    return name


def decode_payload(rec):
    payload, vt, it = rec["payload"], rec["type"], rec["type_input"]
    if vt == 2 and it == 0:
        return payload.decode("utf-8", errors="replace")
    if vt == 3:
        return str(payload[0]) if len(payload) == 1 else hx(payload)
    if vt == 4:
        if len(payload) == 1:
            return str(payload[0])
        if len(payload) == 2:
            return str(struct.unpack_from("<H", payload)[0])
        if len(payload) == 4:
            return (f"uint32 = {struct.unpack_from('<I', payload)[0]}\n"
                    f"float = {struct.unpack_from('<f', payload)[0]:.6g}")
    return hx(payload)


class Monitor:
    def __init__(self):
        self.lock = threading.Lock()
        self.write_lock = threading.Lock()
        self.records = []
        self.next_id = 1
        self.ser = None
        self.port_name = None
        self.baud = None
        self.stop = threading.Event()
        self.thread = None
        self.stats = {"rx": 0, "tx": 0, "rx_bytes": 0, "tx_bytes": 0, "errors": 0, "tm_unmatched": 0}
        self.tm = {}
        self.tm_version = proto.version

    # ---------- записи ----------
    def add(self, rec):
        with self.lock:
            rec["id"] = self.next_id
            rec["time"] = datetime.now().strftime("%H:%M:%S")
            self.next_id += 1
            self.records.append(rec)
            if len(self.records) > 5000:
                del self.records[:500]
        return rec

    def system(self, text, error=False):
        if error:
            self.stats["errors"] += 1
        self.add({"kind": "err" if error else "sys", "valid": not error,
                  "preview": text, "text": text})

    def on_frame(self, kind, data):
        if kind == "log":
            self.system(data.decode("utf-8", errors="replace"))
        elif kind == "data":
            self.stats["rx"] += 1
            self.stats["rx_bytes"] += len(data) + 3
            self.add_radio(data)
        elif kind == "ccsds":
            self.stats["rx"] += 1
            self.stats["rx_bytes"] += len(data)
            self.add_ccsds(data)

    def add_ccsds(self, data):
        w1, w2, w3 = struct.unpack_from(">HHH", data, 0)
        apid, counter, flags = w1 & 0x7FF, w2 & 0x3FFF, w2 >> 14
        pkt_type = (w1 >> 12) & 1
        payload = data[6:]
        parsed = {"device_id": apid, "type": None, "type_input": 0, "packet_number": counter & 0x0F,
                  "size_data": len(payload), "payload": payload, "apid": apid, "pkt_type": pkt_type,
                  "seq_flags": flags, "counter": counter}
        self.sync_proto()
        tm = proto.decode(parsed)
        if tm is None:
            self.stats["tm_unmatched"] += 1
        else:
            self.update_card(tm, parsed, 0, 0)
        kind = "телекоманда" if pkt_type else "телеметрия"
        segs = [
            {"name": "Версия, тип, APID", "key": "dev", "start": 0, "len": 2,
             "value": f"APID {apid} · {kind}", "hint": f"версия {w1 >> 13}"},
            {"name": "Флаги, счётчик", "key": "pack", "start": 2, "len": 2,
             "value": f"флаг {flags} · № {counter}", "hint": "старшие 2 бита — сегментация"},
            {"name": "Длина", "key": "size", "start": 4, "len": 2,
             "value": f"{len(payload)} байт", "hint": f"в заголовке {w3} (длина − 1)"},
            {"name": "Данные", "key": "payload", "start": 6, "len": len(payload)},
        ]
        summary = ", ".join(f"{f['name']} {f['text']}{' ' + f['unit'] if f['unit'] else ''}"
                            for f in tm["fields"]) if tm else ""
        self.add({
            "kind": "rx", "valid": True, "alarm": False, "device_id": apid,
            "type_name": f"CCSDS · {kind}", "packet_number": counter & 0x0F,
            "size": len(payload), "text": hx(payload), "preview": summary or hx(payload),
            "payload_hex": hx(payload), "raw_hex": hx(data), "frame_hex": hx(data),
            "tail_hex": "", "segments": segs, "telemetry": tm, "tm_summary": summary,
        })

    def add_radio(self, data):
        frame = bytes([MARK_DATA, len(data) & 0xFF, (len(data) >> 8) & 0xFF]) + data
        parsed = parse_packet(data)
        if parsed is None:
            self.stats["errors"] += 1
            self.add({"kind": "rx", "valid": False, "size": len(data),
                      "preview": hx(data), "raw_hex": hx(data), "frame_hex": hx(frame),
                      "segments": [{"name": "Данные", "key": "raw", "start": 0, "len": len(data)}]})
            return
        text = decode_payload(parsed).replace("\r", "")
        alarm = parsed["type"] == 2 and parsed["type_input"] == 0 and text.strip() == "ALERT"
        n = parsed["size_data"]
        tm = self.decode_telemetry(parsed, alarm)
        segs = [
            {"name": "Устройство", "key": "dev", "start": 0, "len": 1},
            {"name": "Долгота", "key": "lon", "start": 1, "len": 4},
            {"name": "Широта", "key": "lat", "start": 5, "len": 4},
            {"name": "Тип", "key": "type", "start": 9, "len": 1},
            {"name": "Вход и номер", "key": "pack", "start": 10, "len": 1},
            {"name": "Размер", "key": "size", "start": 11, "len": 1},
            {"name": "Данные", "key": "payload", "start": 12, "len": n},
        ]
        if parsed["trailing"]:
            segs.append({"name": "Хвост", "key": "tail", "start": 12 + n, "len": len(parsed["trailing"])})
        self.add({
            "kind": "rx", "valid": True, "alarm": alarm,
            "device_id": parsed["device_id"],
            "longitude": parsed["longitude"], "latitude": parsed["latitude"],
            "type": parsed["type"], "type_input": parsed["type_input"],
            "type_name": type_name(parsed),
            "packet_number": parsed["packet_number"],
            "size": n, "text": text, "preview": "ТРЕВОГА" if alarm else (text or "пустые данные"),
            "payload_hex": hx(parsed["payload"]), "raw_hex": hx(data), "frame_hex": hx(frame),
            "tail_hex": hx(parsed["trailing"]), "segments": segs,
            "telemetry": tm,
            "tm_summary": ", ".join(f"{f['name']} {f['text']}{' ' + f['unit'] if f['unit'] else ''}"
                                    for f in tm["fields"]) if tm else "",
        })

    def sync_proto(self):
        """Перечитывает protocol.json при изменении; старые карточки телеметрии сбрасываются."""
        proto.refresh()
        if self.tm_version != proto.version:
            with self.lock:
                self.tm.clear()
                self.tm_version = proto.version

    def decode_telemetry(self, parsed, alarm):
        self.sync_proto()
        tm = proto.decode(parsed)
        if tm is None:
            if not alarm:
                self.stats["tm_unmatched"] += 1
            return None
        self.update_card(tm, parsed, parsed["longitude"], parsed["latitude"])
        return tm

    def update_card(self, tm, parsed, lon, lat):
        key = f"{tm['frame']}:{parsed['device_id']}"
        with self.lock:
            card = self.tm.setdefault(key, {"key": key, "frame": tm["frame"], "name": tm["name"],
                                            "device_id": parsed["device_id"], "count": 0, "fields": {}})
            card["count"] += 1
            card["ts"] = time.time()
            card["time"] = datetime.now().strftime("%H:%M:%S")
            card["lon"], card["lat"] = lon, lat
            for f in tm["fields"]:
                slot = card["fields"].setdefault(f["name"], {"history": []})
                slot.update(f)
                if isinstance(f["value"], (int, float)):
                    slot["history"].append(f["value"])
                    del slot["history"][:-60]
        return tm

    # ---------- порт ----------
    def connect(self, port, baud, parity="N"):
        if self.is_connected():
            return False, "Уже подключено"
        if port == DEMO_PORT:
            self.ser = None
        else:
            if serial is None:
                return False, "Не установлен pyserial: pip install pyserial"
            try:
                self.ser = serial.Serial(port=port, baudrate=baud, bytesize=serial.EIGHTBITS,
                                         parity=parity, stopbits=serial.STOPBITS_ONE,
                                         timeout=0.05, write_timeout=1)
            except (serial.SerialException, OSError, ValueError) as exc:
                self.system(f"Ошибка подключения: {exc}", True)
                return False, str(exc)
        self.port_name, self.baud, self.parity = port, baud, parity
        self.stop = threading.Event()
        target = self._demo_loop if port == DEMO_PORT else self._read_loop
        self.thread = threading.Thread(target=target, args=(self.stop,), daemon=True)
        self.thread.start()
        self.system(f"Подключено: {port}" + ("" if port == DEMO_PORT else f" · {baud} 8{parity}1"))
        return True, ""

    def disconnect(self, log=True):
        was = self.is_connected()
        self.stop.set()
        if self.ser:
            try:
                self.ser.close()
            except Exception as exc:  # noqa: BLE001
                self.system(f"Ошибка закрытия порта: {exc}", True)
        if self.thread and self.thread.is_alive() and self.thread is not threading.current_thread():
            self.thread.join(timeout=1)
        self.ser = None
        self.thread = None
        self.port_name = None
        if was and log:
            self.system("Соединение закрыто")

    def is_connected(self):
        return self.thread is not None and self.thread.is_alive() and not self.stop.is_set()

    def _read_loop(self, stop):
        parser = StreamParser(self.on_frame)
        ser = self.ser
        while not stop.is_set():
            try:
                if not ser or not ser.is_open:
                    break
                count = ser.in_waiting
                if count:
                    parser.feed(ser.read(count))
                else:
                    stop.wait(0.01)
            except Exception as exc:  # noqa: BLE001
                if not stop.is_set():
                    self.system(f"Порт недоступен: {exc}", True)
                    stop.set()
                break

    def _demo_loop(self, stop):
        """Имитация спутника, чтобы пробовать интерфейс без железа."""
        n = 0
        lon, lat = 377_000_000, 557_000_000
        while not stop.wait(1.4):
            n += 1
            lon += random.randint(-900, 900)
            lat += random.randint(-900, 900)
            choice = n % 4
            if choice == 0:
                t, ti, body = 2, 0, f"T={random.uniform(-48, -31):.1f}C P={random.randint(8200, 9600) / 100:.2f}kPa".encode()
            elif choice == 1:
                t, ti, body = 4, 0, struct.pack("<H", random.randint(480, 520))
            elif choice == 2:
                t, ti, body = 3, 0, bytes([random.randint(0, 1)])
            else:
                t, ti, body = 4, 0, struct.pack("<f", random.uniform(14.2, 14.9))
            head = struct.pack("<BiiBBB", 7, lon, lat, t, ((n % 16) << 4) | ti, len(body))
            data = head + body
            self.stats["rx"] += 1
            self.stats["rx_bytes"] += len(data) + 3
            self.add_radio(data)

    def send_bytes(self, data):
        if self.port_name == DEMO_PORT and self.is_connected():
            self.stats["tx"] += 1
            self.stats["tx_bytes"] += len(data)
            return True, ""
        if not self.ser or not self.ser.is_open:
            self.system("Нет подключения", True)
            return False, "Нет подключения"
        try:
            with self.write_lock:
                written = self.ser.write(data)
            if written != len(data):
                raise OSError(f"отправлено {written} из {len(data)} байт")
            self.stats["tx"] += 1
            self.stats["tx_bytes"] += len(data)
            return True, ""
        except Exception as exc:  # noqa: BLE001
            self.system(f"Ошибка отправки: {exc}", True)
            return False, str(exc)

    def add_tx(self, raw, text, alarm=False, command=None):
        preview = "ТРЕВОГА" if alarm else (f"{command}: {text}" if command else text)
        self.add({"kind": "tx", "valid": True, "alarm": alarm, "size": len(raw),
                  "text": text, "preview": preview, "command": command,
                  "raw_hex": hx(raw), "frame_hex": hx(raw)})


mon = Monitor()


# ---------- HTTP ----------
ALLOWED_HOSTS = {"127.0.0.1:5000", "localhost:5000"}
ALLOWED_ORIGINS = {f"http://{h}" for h in ALLOWED_HOSTS}


@app.before_request
def guard():
    """Защита локального сервера от чужих сайтов (DNS rebinding и CSRF)."""
    if request.host not in ALLOWED_HOSTS:
        return jsonify(ok=False, error="Недопустимый адрес сервера"), 403
    if request.method == "POST":
        origin = request.headers.get("Origin")
        if origin is not None and origin not in ALLOWED_ORIGINS:
            return jsonify(ok=False, error="Запрос с чужого сайта отклонён"), 403
        if not request.is_json:
            return jsonify(ok=False, error="Нужен Content-Type: application/json"), 415
    return None


@app.get("/")
def index():
    return send_from_directory(app.static_folder, "index.html")


@app.get("/api/ports")
def api_ports():
    ports = []
    if serial is not None:
        try:
            for p in serial.tools.list_ports.comports():
                desc = p.description if p.description and p.description.lower() != "n/a" else ""
                ports.append({"device": p.device, "description": desc})
        except Exception as exc:  # noqa: BLE001
            mon.system(f"Не удалось получить список портов: {exc}", True)
    ports.append({"device": DEMO_PORT, "description": "Имитация данных без оборудования"})
    return jsonify(ports)


@app.post("/api/connect")
def api_connect():
    body = request.get_json(silent=True) or {}
    port = body.get("port")
    if not port:
        return jsonify(ok=False, error="Порт не выбран"), 400
    try:
        baud = int(body.get("baud", 9600))
    except (TypeError, ValueError):
        return jsonify(ok=False, error="Некорректная скорость"), 400
    parity = str(body.get("parity", "N")).upper()
    if parity not in ("N", "E", "O"):
        return jsonify(ok=False, error="Некорректная чётность"), 400
    ok, err = mon.connect(port, baud, parity)
    return jsonify(ok=ok, error=err), (200 if ok else 400)


@app.post("/api/disconnect")
def api_disconnect():
    mon.disconnect()
    return jsonify(ok=True)


@app.post("/api/send")
def api_send():
    body = request.get_json(silent=True) or {}
    text = str(body.get("text", ""))
    if not text.strip():
        return jsonify(ok=False, error="Пустое сообщение"), 400
    payload = text.encode("utf-8")
    if len(payload) > MAX_PAYLOAD_SIZE:
        mon.system(f"Сообщение слишком длинное: {len(payload)} из {MAX_PAYLOAD_SIZE} байт", True)
        return jsonify(ok=False, error=f"Слишком длинное сообщение: {len(payload)} из {MAX_PAYLOAD_SIZE} байт"), 400
    raw = payload + b"\n"
    ok, err = mon.send_bytes(raw)
    if ok:
        mon.add_tx(raw, text)
    return jsonify(ok=ok, error=err), (200 if ok else 400)


@app.post("/api/alarm")
def api_alarm():
    raw = b"ALERT\n"
    ok, err = mon.send_bytes(raw)
    if ok:
        mon.add_tx(raw, "ALERT", alarm=True)
    return jsonify(ok=ok, error=err), (200 if ok else 400)


@app.get("/api/protocol")
def api_protocol_get():
    mon.sync_proto()
    return jsonify(ok=True, text=proto.text, error=proto.error, path=proto.path,
                   version=proto.version, **proto.public())


@app.post("/api/protocol")
def api_protocol_save():
    body = request.get_json(silent=True) or {}
    err = proto.save(str(body.get("text", "")))
    if err:
        return jsonify(ok=False, error=err), 400
    mon.sync_proto()
    mon.system("Протокол обновлён")
    return jsonify(ok=True)


@app.post("/api/command")
def api_command():
    body = request.get_json(silent=True) or {}
    mon.sync_proto()
    try:
        raw, shown, cmd = proto.build_command(str(body.get("id", "")), body.get("params") or {},
                                              MAX_PAYLOAD_SIZE)
    except ValueError as exc:
        return jsonify(ok=False, error=str(exc)), 400
    ok, err = mon.send_bytes(raw)
    if ok:
        mon.add_tx(raw, shown, alarm=bool(cmd.get("alarm")), command=cmd["name"])
    return jsonify(ok=ok, error=err), (200 if ok else 400)


@app.get("/api/state")
def api_state():
    since = request.args.get("since", 0, type=int)
    mon.sync_proto()
    with mon.lock:
        new = [r for r in mon.records if r["id"] > since]
        last = mon.next_id - 1
        telemetry = [dict(c, fields=[dict(f, name=n) for n, f in c["fields"].items()])
                     for c in mon.tm.values()]
    return jsonify(records=new, last=last, connected=mon.is_connected(),
                   port=mon.port_name, baud=mon.baud, parity=getattr(mon, "parity", "N"), stats=mon.stats,
                   reset=since > last, telemetry=telemetry, now=time.time(),
                   proto_version=proto.version, proto_error=proto.error)


@app.post("/api/clear")
def api_clear():
    with mon.lock:
        mon.records.clear()
    return jsonify(ok=True)


@app.get("/api/export")
def api_export():
    with mon.lock:
        data = json.dumps(mon.records, ensure_ascii=False, indent=2)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return Response(data, mimetype="application/json",
                    headers={"Content-Disposition": f"attachment; filename=dpp_{stamp}.json"})


if __name__ == "__main__":
    url = "http://127.0.0.1:5000"
    threading.Timer(1.0, lambda: webbrowser.open(url)).start()
    print(f"DPP монитор запущен: {url}")
    app.run(host="127.0.0.1", port=5000, debug=False, threaded=True)
