# Authenticated commands from Earth

Commands are executed immediately after authentication; the satellite does not
wait for an operator confirmation or send a command-acceptance handshake.

## Frame format

All multi-byte values are little-endian. The complete frame is at most 64 bytes:

| Offset | Size | Value |
| --- | ---: | --- |
| 0 | 1 | Magic `0x53` |
| 1 | 1 | Magic `0x43` |
| 2 | 1 | Protocol version `1` |
| 3 | 4 | Monotonically increasing command sequence |
| 7 | 1 | Command ID |
| 8 | 1 | Payload size, from 0 to 39 bytes |
| 9 | 0-39 | Payload |
| `9 + payload size` | 16 | First 16 bytes of HMAC-SHA-256 |

The HMAC covers every frame byte before the tag. The key is a 32-byte
pre-shared secret. The satellite fails closed when `include/command_key.h` is
missing. Generate 32 random bytes locally (for example, with
`openssl rand -hex 32`) and create the Git-ignored `include/command_key.h`
defining `static const uint8_t COMMAND_HMAC_KEY[32]` with those bytes. Use the
same key at the ground station; do not commit or transmit it in cleartext.
The current sequence check rejects repeated or older commands until reboot.
Persistent replay protection across power cycles requires persisting the last
accepted sequence in reliable non-volatile storage.

## Built-in commands

| ID | Payload | Effect |
| --- | --- | --- |
| `0x01` | One byte | Set operating mode (`0` through `4`) |
| `0x02` | Four-byte unsigned milliseconds | Set telemetry period (`1000` through `86400000`) |

To add a command, define a handler with signature
`bool handler(const uint8_t *payload, uint8_t length)` and add its ID,
handler, minimum payload length, and maximum payload length to the command
registry in `src/main.cpp`. Return `false` if the payload is invalid.
