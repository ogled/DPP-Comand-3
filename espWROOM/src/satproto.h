// The line protocol on the shared E32 LoRa channel: terminals, and the
// payload (PN). Every line ends '\n'.
//
// A message says who it is from and who it is for:
//
//   MSG|<from>|<to>|<text>
//     from  terminal number ("7"), or "@callsign" for a person on the site
//           (put on air by a terminal that is online, for the payload to carry)
//     to    "*" for everyone in range, a terminal number ("3"), "@callsign"
//           or "#48213" for a person on the site
//     text  "nick: message", or just the message
//
// The payload keeps every MSG line that is addressed (to is not "*") and
// keeps dumping its cache:
//
//   SAT|BEGIN|<first>|<last>|<count>   a dump starts: ids first..last, count lines
//   SAT|<id>|MSG|...                   one stored line, exactly as it was heard
//   SAT|END|<last>                     the dump is over; the payload listens
//
// After SAT|END the payload listens for SAT_LISTEN_MS (longer while it
// keeps hearing lines). That window is when terminals talk to it:
//
//   RESET CACHE <last>   from a terminal online with the site that caught the
//                        whole dump: drop every stored line with id <= last
//   MSG|...              a terminal's addressed lines it has not yet seen come
//                        back in a dump, sent again (at most SAT_RESEND per window)
//
// A terminal shows a dumped line only if it is addressed to its own number;
// an online terminal passes every dumped line to the site.
// Ids only ever grow, across resets and reboots.
//
// Commands to the payload, typed on a terminal as "/cam". The terminal
// sends the command at once and again in each listening window until the
// payload answers:
//
//   CMD|<from>|cam                       take a photo for terminal <from>
//
// The payload answers with the photo, a small JPEG, cut into base64 lines:
//
//   IMG|<img>|BEGIN|<from>|<chunks>|<bytes>|<w>x<h>
//   IMG|<img>|<seq>|<crc>|<base64 of up to IMG_CHUNK bytes>   seq 0..chunks-1
//   IMG|<img>|END                        the payload listens again
//
// The E32 sends at most 58 bytes per radio packet, so a longer line goes
// out in several packets, and losing one leaves a line that still parses
// but has a hole in it. A photo line is kept under 58 bytes - one packet,
// all or nothing - and carries a CRC-8 (2 hex digits) of its bytes, so a
// damaged piece is asked for again instead of spoiling the photo.
//
// A terminal missing pieces asks for them - after IMG|...|END, in every
// listening window (after SAT|END), and whenever photo lines stop coming
// for a while - and the payload sends just those, then END again. The
// list is pieces and ranges, cut to fit one radio packet:
//
//   CMD|<from>|again|<img>|31-90,95,120-136
//
// This file is shared: keep PN/src/satproto.h and espS3/src/satproto.h the same.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MSG_PREFIX "MSG|"
#define MSG_EVERYONE "*"
#define SAT_PREFIX "SAT|"
#define SAT_BEGIN "SAT|BEGIN|"
#define SAT_END "SAT|END|"
#define SAT_RESET "RESET CACHE"

#define SAT_LISTEN_MS 4000         // the payload listens this long after each dump...
#define SAT_LISTEN_EXTEND_MS 1500  // ...and this much more after each line it hears
#define SAT_REPLY_MIN_MS 200       // a terminal answers after a random delay in this range
#define SAT_REPLY_MAX_MS 1500
#define SAT_RESEND 3               // lines a terminal sends again per listening window

#define CMD_PREFIX "CMD|"
#define IMG_PREFIX "IMG|"
#define IMG_CHUNK 30               // bytes of JPEG per line: 40 base64 characters,
                                   // "IMG|9999|409|ab|" + 40 + '\n' = 57 bytes: one E32 packet
#define IMG_ID_MAX 9999            // photo numbers wrap, to keep a line inside one packet
#define IMG_MAX_BYTES 4096         // a bigger photo is taken again at lower quality (137 lines)
#define LINE_ONE_PACKET 57         // longest line that still goes out as one E32 packet

// CRC-8 (polynomial 0x07) of a photo piece. Header-only so both sides use the same code.
static inline uint8_t imgCrc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
  }
  return crc;
}
