"""Радиоформат Глеботрона GLB v1: кадры сообщений и подтверждений.

Сервер работает только с этими кадрами. Упаковку в CCSDS или HMAC-команду
спутника делает ЦУП (Laptop-program).

Кадр, little-endian:
  0  u8   kind: 0x01 MSG, 0x02 ACK
  1  u16  msg_id — назначает отправитель
  3  u16  src
  5  u16  dst
  7  u8   frag: index<<4 | (count-1), до 16 фрагментов
  8  ...  кусок текста UTF-8 (в ACK пусто)
"""
import struct
from dataclasses import dataclass

KIND_MSG = 0x01
KIND_ACK = 0x02
HEADER = struct.Struct("<BHHHB")
HEADER_SIZE = HEADER.size
MAX_FRAGMENTS = 16
MIN_FRAME = HEADER_SIZE + 4  # в кадр должен помещаться любой символ UTF-8


@dataclass(frozen=True)
class Frame:
    kind: int
    msg_id: int
    src: int
    dst: int
    index: int
    count: int
    data: bytes


def split_utf8(text, chunk):
    """Режет текст на куски не длиннее chunk байт, не разрывая символы."""
    parts, cur = [], b""
    for ch in text:
        b = ch.encode("utf-8")
        if len(cur) + len(b) > chunk:
            parts.append(cur)
            cur = b""
        cur += b
    if cur:
        parts.append(cur)
    return parts


def frames_needed(text, frame_max):
    return len(split_utf8(text, frame_max - HEADER_SIZE))


def encode_message(msg_id, src, dst, text, frame_max):
    """Список кадров сообщения. ValueError — пустой текст или больше 16 фрагментов."""
    if frame_max < MIN_FRAME:
        raise ValueError(f"Размер кадра меньше {MIN_FRAME} байт")
    chunks = split_utf8(text, frame_max - HEADER_SIZE)
    if not chunks:
        raise ValueError("Пустое сообщение")
    if len(chunks) > MAX_FRAGMENTS:
        raise ValueError(f"Сообщение не помещается в {MAX_FRAGMENTS} кадров")
    count = len(chunks)
    return [HEADER.pack(KIND_MSG, msg_id & 0xFFFF, src, dst, (i << 4) | (count - 1)) + c
            for i, c in enumerate(chunks)]


def encode_ack(msg_id, src, dst):
    return HEADER.pack(KIND_ACK, msg_id & 0xFFFF, src, dst, 0)


def decode_frame(raw):
    """Разбирает кадр. ValueError с понятным текстом, если кадр битый."""
    raw = bytes(raw)
    if len(raw) < HEADER_SIZE:
        raise ValueError(f"Кадр короче заголовка: {len(raw)} из {HEADER_SIZE} байт")
    kind, msg_id, src, dst, frag = HEADER.unpack_from(raw)
    index, count = frag >> 4, (frag & 0x0F) + 1
    data = raw[HEADER_SIZE:]
    if kind == KIND_ACK:
        if data or frag:
            raise ValueError("ACK не должен содержать данных и фрагментов")
    elif kind == KIND_MSG:
        if index >= count:
            raise ValueError(f"Номер фрагмента {index} при общем числе {count}")
        if not data:
            raise ValueError("Пустой фрагмент сообщения")
        data.decode("utf-8")  # UnicodeDecodeError — подкласс ValueError
    else:
        raise ValueError(f"Неизвестный тип кадра 0x{kind:02X}")
    return Frame(kind, msg_id, src, dst, index, count, data)


def assemble(parts):
    """Склеивает фрагменты, упорядоченные по номеру, в текст."""
    return b"".join(parts).decode("utf-8")


def to_hex(raw):
    return bytes(raw).hex(" ").upper()


def from_hex(text):
    try:
        return bytes.fromhex(str(text))
    except ValueError:
        raise ValueError("Ожидается HEX-строка, например \"01 05 02\"")
