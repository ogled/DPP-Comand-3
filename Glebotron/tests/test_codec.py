import pytest

import codec


def test_message_round_trip_matches_spec_example():
    frames = codec.encode_message(517, 48213, 7, "Привет", 39)
    assert codec.to_hex(frames[0]) == "01 05 02 55 BC 07 00 00 D0 9F D1 80 D0 B8 D0 B2 D0 B5 D1 82"
    fr = codec.decode_frame(frames[0])
    assert (fr.kind, fr.msg_id, fr.src, fr.dst, fr.index, fr.count) == (codec.KIND_MSG, 517, 48213, 7, 0, 1)
    assert codec.assemble([fr.data]) == "Привет"


def test_ack_round_trip():
    raw = codec.encode_ack(517, 7, 48213)
    assert codec.to_hex(raw) == "02 05 02 07 00 55 BC 00"
    fr = codec.decode_frame(raw)
    assert (fr.kind, fr.msg_id, fr.src, fr.dst, fr.data) == (codec.KIND_ACK, 517, 7, 48213, b"")


@pytest.mark.parametrize("text", ["Я" * 80, "🛰" * 40, "a" * 160, "Глеботрон 🛰 на связи! " * 6])
def test_fragments_split_on_char_boundaries(text):
    frames = codec.encode_message(1, 2, 3, text, 39)
    assert all(len(f) <= 39 for f in frames)
    decoded = [codec.decode_frame(f) for f in frames]
    for fr in decoded:
        fr.data.decode("utf-8")  # каждый фрагмент — целые символы
    assert codec.assemble([fr.data for fr in sorted(decoded, key=lambda f: f.index)]) == text
    assert codec.frames_needed(text, 39) == len(frames)


def test_out_of_order_fragments_reassemble():
    text = "Сообщение из нескольких кадров через спутник"
    decoded = [codec.decode_frame(f) for f in codec.encode_message(9, 5, 1000, text, 39)]
    assert len(decoded) > 1
    shuffled = list(reversed(decoded))
    assert codec.assemble([f.data for f in sorted(shuffled, key=lambda f: f.index)]) == text


def test_too_many_fragments():
    with pytest.raises(ValueError):
        codec.encode_message(1, 2, 3, "x" * (31 * 16 + 1), 39)
    assert len(codec.encode_message(1, 2, 3, "x" * (31 * 16), 39)) == 16


def test_empty_message_rejected():
    with pytest.raises(ValueError):
        codec.encode_message(1, 2, 3, "", 39)


def test_msg_id_wraps_to_u16():
    fr = codec.decode_frame(codec.encode_message(65536 + 5, 2, 3, "a", 39)[0])
    assert fr.msg_id == 5


@pytest.mark.parametrize("raw", [
    b"",
    bytes.fromhex("01 05 02 07 00"),                      # короче заголовка
    bytes.fromhex("03 05 02 07 00 E8 03 00 41"),          # неизвестный тип
    bytes.fromhex("01 05 02 07 00 E8 03 00"),             # пустой фрагмент
    bytes.fromhex("01 05 02 07 00 E8 03 21 41"),          # фрагмент 2 из 2
    bytes.fromhex("01 05 02 07 00 E8 03 00 D0"),          # обрезанный символ UTF-8
    bytes.fromhex("02 05 02 07 00 E8 03 00 41"),          # ACK с данными
])
def test_broken_frames(raw):
    with pytest.raises(ValueError):
        codec.decode_frame(raw)


def test_hex_helpers():
    assert codec.from_hex("01 0a FF") == b"\x01\x0a\xff"
    with pytest.raises(ValueError):
        codec.from_hex("zz")
