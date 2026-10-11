"""Протокол ЦУПа: декодер телеметрии и сборка команд по конфигу protocol.json.

Весь протокол описан в protocol.json (его можно править прямо в интерфейсе,
кнопка «Протокол»). Код менять не нужно: файл перечитывается при изменении.
"""
import json
import os
import re
import struct
import threading

PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "protocol.json")

INT_TYPES = {"u8": "B", "i8": "b", "u16": "H", "i16": "h", "u32": "I", "i32": "i",
             "u64": "Q", "i64": "q", "f32": "f", "f64": "d"}
FIELD_TYPES = set(INT_TYPES) | {"bool", "str", "hex", "regex"}
MATCH_KEYS = {"device_id", "type", "type_input", "packet_number", "size", "min_size", "prefix",
              "apid", "pkt_type", "seq_flags", "counter"}


def _int(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def _as_list(value):
    return value if isinstance(value, list) else [value]


def validate(data):
    """Проверяет структуру конфига. Бросает ValueError с понятным текстом."""
    if not isinstance(data, dict):
        raise ValueError("Корень файла должен быть объектом { ... }")
    if data.get("byte_order", "little") not in ("little", "big"):
        raise ValueError('byte_order: допустимо "little" или "big"')
    framing = data.get("framing", {"mode": "markers"})
    if not isinstance(framing, dict) or framing.get("mode", "markers") not in ("markers", "ccsds"):
        raise ValueError('framing.mode: допустимо "markers" или "ccsds"')
    ids = set()
    for fi, frame in enumerate(data.get("telemetry", [])):
        where = f"telemetry[{fi}]"
        if not isinstance(frame, dict) or not frame.get("id") or not frame.get("name"):
            raise ValueError(f'{where}: нужны поля "id" и "name"')
        if frame["id"] in ids:
            raise ValueError(f'{where}: повторяется id "{frame["id"]}"')
        ids.add(frame["id"])
        for key in frame.get("match", {}):
            if key not in MATCH_KEYS:
                raise ValueError(f'{where}.match: неизвестный ключ "{key}" (можно: {", ".join(sorted(MATCH_KEYS))})')
        if "prefix" in frame.get("match", {}):
            try:
                bytes.fromhex(frame["match"]["prefix"])
            except (ValueError, TypeError):
                raise ValueError(f'{where}.match.prefix: ожидается HEX-строка, например "A5 01"')
        if not frame.get("fields"):
            raise ValueError(f'{where}: пустой список "fields"')
        for ki, f in enumerate(frame["fields"]):
            fw = f'{where}.fields[{ki}]'
            if not isinstance(f, dict) or not f.get("name"):
                raise ValueError(f'{fw}: нужно поле "name"')
            t = f.get("type", "u8")
            if t not in FIELD_TYPES:
                raise ValueError(f'{fw}: неизвестный type "{t}" (можно: {", ".join(sorted(FIELD_TYPES))})')
            if t == "regex":
                try:
                    re.compile(f.get("regex", ""))
                except re.error as exc:
                    raise ValueError(f'{fw}.regex: {exc}')
                if not f.get("regex"):
                    raise ValueError(f'{fw}: для type "regex" нужно поле "regex"')
    cids = set()
    for ci, cmd in enumerate(data.get("commands", [])):
        where = f"commands[{ci}]"
        if not isinstance(cmd, dict) or not cmd.get("id") or not cmd.get("name"):
            raise ValueError(f'{where}: нужны поля "id" и "name"')
        if cmd["id"] in cids:
            raise ValueError(f'{where}: повторяется id "{cmd["id"]}"')
        cids.add(cmd["id"])
        if ("text" in cmd) == ("bytes" in cmd):
            raise ValueError(f'{where}: укажите ровно одно из "text" или "bytes"')
        names = set()
        for p in cmd.get("params", []):
            if not isinstance(p, dict) or not p.get("name"):
                raise ValueError(f'{where}.params: у параметра нужно поле "name"')
            pt = p.get("type", "str")
            if pt not in INT_TYPES and pt not in ("str", "bool"):
                raise ValueError(f'{where}.params.{p["name"]}: неизвестный type "{pt}"')
            if "bytes" in cmd and pt == "str":
                raise ValueError(f'{where}.params.{p["name"]}: в "bytes"-команде нужен числовой type (u8, u16, f32...)')
            names.add(p["name"])
        template = cmd.get("text") if "text" in cmd else cmd.get("bytes")
        if not isinstance(template, str):
            raise ValueError(f'{where}: шаблон должен быть строкой')
        for used in re.findall(r"\{(\w+)\}", template):
            if used not in names:
                raise ValueError(f'{where}: в шаблоне {{{used}}}, но параметра "{used}" нет в params')
        if "bytes" in cmd:
            for tok in re.sub(r"\{\w+\}", " ", template).split():
                try:
                    if not 0 <= int(tok, 16) <= 255:
                        raise ValueError
                except ValueError:
                    raise ValueError(f'{where}.bytes: "{tok}" — не байт в HEX (00..FF)')


class Protocol:
    def __init__(self, path=PATH):
        self.path = path
        self.lock = threading.Lock()
        self.data = {}
        self.text = ""
        self.error = ""
        self.version = 0
        self._mtime = None
        self.refresh(force=True)

    # ---------- загрузка ----------
    def refresh(self, force=False):
        """Перечитывает файл, если он изменился. Возвращает True при смене версии."""
        try:
            mtime = os.stat(self.path).st_mtime_ns
        except OSError:
            mtime = None
        if not force and mtime == self._mtime:
            return False
        with self.lock:
            self._mtime = mtime
            if mtime is None:
                self.data, self.text = {}, ""
                self.error = f"Файл протокола не найден: {self.path}"
            else:
                try:
                    with open(self.path, "r", encoding="utf-8") as fh:
                        text = fh.read()
                    data = json.loads(text)
                    validate(data)
                    self.data, self.text, self.error = data, text, ""
                except (OSError, ValueError) as exc:  # json.JSONDecodeError ⊂ ValueError
                    # прежний рабочий протокол остаётся в силе, ошибку показываем в интерфейсе
                    self.error = f"protocol.json: {exc}"
                    try:
                        with open(self.path, "r", encoding="utf-8") as fh:
                            self.text = fh.read()
                    except OSError:
                        pass
            self.version += 1
        return True

    def save(self, text):
        """Проверяет и записывает конфиг. Возвращает текст ошибки или ''."""
        try:
            data = json.loads(text)
            validate(data)
        except ValueError as exc:
            return str(exc)
        tmp = self.path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text if text.endswith("\n") else text + "\n")
            os.replace(tmp, self.path)
        except OSError as exc:
            return f"Не удалось записать файл: {exc}"
        self.refresh(force=True)
        return ""

    @property
    def order(self):
        return "<" if self.data.get("byte_order", "little") == "little" else ">"

    def public(self):
        """Часть конфига, нужная интерфейсу (кнопки команд)."""
        cmds = []
        for c in self.data.get("commands", []):
            item = {k: c.get(k) for k in ("id", "name", "description", "confirm", "danger")}
            item["params"] = c.get("params", [])
            cmds.append(item)
        return {"commands": cmds}

    # ---------- телеметрия ----------
    @staticmethod
    def _match(rule, parsed):
        for key, want in rule.items():
            if key == "min_size":
                if parsed["size_data"] < _int(want):
                    return False
            elif key == "prefix":
                if not parsed["payload"].startswith(bytes.fromhex(want)):
                    return False
            else:
                have = parsed["size_data"] if key == "size" else parsed.get(key)
                if have is None or have not in [_int(x) for x in _as_list(want)]:
                    return False
        return True

    def decode(self, parsed):
        """Возвращает {'frame','name','fields':[...]} или None, если ни один кадр не подошёл."""
        for frame in self.data.get("telemetry", []):
            if self._match(frame.get("match", {}), parsed):
                order = "<" if frame.get("byte_order", self.data.get("byte_order", "little")) == "little" else ">"
                return {"frame": frame["id"], "name": frame["name"],
                        "fields": [self._field(f, parsed["payload"], order) for f in frame["fields"]]}
        return None

    @staticmethod
    def _field(f, payload, order):
        out = {"name": f["name"], "unit": f.get("unit", ""), "value": None, "text": "—", "state": "err",
               "group": f.get("group", ""), "kpi": bool(f.get("kpi"))}
        t = f.get("type", "u8")
        try:
            off = _int(f.get("offset", 0))
            if t == "regex":
                m = re.search(f["regex"], payload.decode("utf-8", errors="replace"))
                if not m:
                    raise ValueError("нет совпадения с regex")
                s = m.group(1) if m.groups() else m.group(0)
                try:
                    v = float(s)
                    if v.is_integer() and "." not in s and "e" not in s.lower():
                        v = int(v)
                except ValueError:
                    v = s
            elif t in ("str", "hex"):
                length = f.get("length")
                chunk = payload[off:off + _int(length)] if length else payload[off:]
                v = chunk.decode("utf-8", errors="replace").rstrip("\x00") if t == "str" else chunk.hex(" ")
            else:
                v = struct.unpack_from(order + INT_TYPES.get(t, "B"), payload, off)[0]
                if "mask" in f:
                    v &= _int(f["mask"])
                if "bit" in f:
                    v = (v >> _int(f["bit"])) & 1
                if t == "bool":
                    v = bool(v)
        except (struct.error, ValueError, IndexError, TypeError) as exc:
            out["note"] = str(exc)
            return out

        raw = v
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            if "scale" in f:
                v = v * f["scale"]
            if "add" in f:
                v = v + f["add"]
        label = None
        if "enum" in f and isinstance(raw, (int, bool)):
            label = f["enum"].get(str(int(raw)))
        if label is not None:
            text = str(label)
        elif isinstance(v, bool):
            text = "да" if v else "нет"
        elif isinstance(v, (int, float)):
            text = f"{v:.{int(f['decimals'])}f}" if "decimals" in f else (str(v) if isinstance(v, int) else f"{v:.6g}")
        else:
            text = str(v)
        state = "ok"
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            if ("crit_min" in f and v < f["crit_min"]) or ("crit_max" in f and v > f["crit_max"]):
                state = "bad"
            elif ("warn_min" in f and v < f["warn_min"]) or ("warn_max" in f and v > f["warn_max"]):
                state = "warn"
        if isinstance(raw, (int, bool)) and str(int(raw)) in [str(x) for x in f.get("bad_values", [])]:
            state = "bad"
        out.update(value=(v if isinstance(v, (int, float, str)) and not isinstance(v, bool) else int(v)),
                   text=text, state=state)
        return out

    # ---------- команды ----------
    def build_command(self, cid, values, max_payload):
        """Собирает байты команды. Возвращает (raw, display_text, cmd). ValueError — ошибка пользователю."""
        cmd = next((c for c in self.data.get("commands", []) if c["id"] == cid), None)
        if cmd is None:
            raise ValueError("Неизвестная команда (проверьте protocol.json)")
        order = "<" if cmd.get("byte_order", self.data.get("byte_order", "little")) == "little" else ">"
        params = {}
        for p in cmd.get("params", []):
            raw = values.get(p["name"], p.get("default"))
            if raw is None or raw == "":
                raise ValueError(f"Не задан параметр «{p.get('label', p['name'])}»")
            params[p["name"]] = self._coerce(p, raw)

        if "text" in cmd:
            text = re.sub(r"\{(\w+)\}", lambda m: str(params[m.group(1)]), cmd["text"])
            payload = text.encode("utf-8")
            suffix = cmd.get("suffix", self.data.get("tx_suffix", "\n")).encode("utf-8")
            shown = text
        else:
            ptypes = {p["name"]: p.get("type") for p in cmd.get("params", [])}
            payload = b""
            for tok in cmd["bytes"].split():
                m = re.fullmatch(r"\{(\w+)\}", tok)
                if m:
                    fmt = "B" if ptypes[m.group(1)] == "bool" else INT_TYPES[ptypes[m.group(1)]]
                    try:
                        payload += struct.pack(order + fmt, params[m.group(1)])
                    except struct.error as exc:
                        raise ValueError(f"Параметр «{m.group(1)}» не помещается в {ptypes[m.group(1)]}: {exc}")
                else:
                    payload += bytes([int(tok, 16)])
            suffix = bytes.fromhex(cmd.get("suffix", "")) if cmd.get("suffix") else b""
            shown = payload.hex(" ")
        if len(payload) > max_payload:
            raise ValueError(f"Команда слишком длинная: {len(payload)} из {max_payload} байт")
        return payload + suffix, shown, cmd

    @staticmethod
    def _coerce(p, raw):
        t = p.get("type", "str")
        label = p.get("label", p["name"])
        try:
            if t == "str":
                v = str(raw)
            elif t == "bool":
                v = 1 if raw in (True, 1, "1", "true", "True", "on") else 0
            elif t in ("f32", "f64"):
                v = float(raw)
            else:
                v = _int(raw) if not isinstance(raw, float) else int(raw)
        except (TypeError, ValueError):
            raise ValueError(f"Параметр «{label}»: ожидается значение типа {t}")
        if isinstance(v, (int, float)) and t != "bool":
            if "min" in p and v < p["min"]:
                raise ValueError(f"Параметр «{label}»: минимум {p['min']}")
            if "max" in p and v > p["max"]:
                raise ValueError(f"Параметр «{label}»: максимум {p['max']}")
        if p.get("options"):
            allowed = {str(o["value"] if isinstance(o, dict) else o) for o in p["options"]}
            if str(v) not in allowed:
                raise ValueError(f"Параметр «{label}»: недопустимое значение")
        return v
