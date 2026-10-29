/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

#include "panel_profile.h"

/*
 * Protocol 6 receiver: COBS-delimited records, a 20-byte header, a body of at
 * most 512 bytes and a uint16 trailer CRC. The receiver validates the header
 * and sequence before any body can reach the LCD, then streams a PackBits
 * RGB565 body directly to the panel. See docs/PROTOCOL.md for the wire format.
 *
 * The including translation unit supplies:
 *   STREAM_WINDOW(x, y, w, h)  set the LCD window before a rectangle
 *   STREAM_BYTE(b)             write one pixel byte (RGB565, high byte first)
 *
 * Protocol 6 headers (little endian):
 *   0..1   'R' 'S'
 *   2      protocol version, 6
 *   3      kind: RECT=1, DATA=2, END=3, PING=4, EXIT=5, SYNC=6, BRIGHTNESS=7
 *   4..5   session
 *   6..7   sequence modulo 65536
 *   8..9   body length (0..512)
 *   10..17 geometry: x, y, width, height (uint16 each)
 *   18..19 header CRC16-CCITT over bytes 0..17, initial 0xffff
 *
 * The body is followed by a CRC16-CCITT over the header and body, initial
 * 0xffff. The trailer is checked after streaming, so a damaged DATA record can
 * already have written pixels; recovery requires a SYNC and a full redraw.
 */

/* A hardware build can provide a register-only repeated-pixel writer. A
 * portable build keeps the identical byte sequence through STREAM_BYTE. */
#ifndef STREAM_REPEAT
static void stream_repeat_portable(uint8_t hi, uint8_t lo, unsigned count) {
  do {
    STREAM_BYTE(hi);
    STREAM_BYTE(lo);
  } while (--count);
}
#define STREAM_REPEAT(hi, lo, n) stream_repeat_portable(hi, lo, n)
#endif

/* Forward record kinds. Kinds 4..6 are the stock command records; kind 7 is the
 * live-brightness extension. */
enum {
  STREAM_RECT = 1,
  STREAM_DATA,
  STREAM_END,
  STREAM_PING,
  STREAM_EXIT,
  STREAM_SYNC,
  STREAM_BRIGHTNESS
};

/* Reverse-channel statuses. READY_BRIGHTNESS (134) is the capability signal:
 * a host that sees it may use live BRIGHTNESS records; a host that sees READY
 * (128) must fall back to command-mode brightness. */
enum {
  STREAM_READY = 128,
  STREAM_BYE,
  STREAM_PROGRESS,
  STREAM_ERROR,
  STREAM_SYNCED,
  STREAM_DONE,
  STREAM_READY_BRIGHTNESS,
  STREAM_BRIGHTNESS_DONE
};

/* Fixed buffer sizes. The ring is twice the credit window so a conforming
 * sender cannot overwrite unread bytes while touch polling delays the CPU. */
enum {
  STREAM_HEADER = 20,
  STREAM_PAYLOAD = 512,
  STREAM_WINDOW_BYTES = 512,
  STREAM_RING = 1024
};

/* Decoder state. One instance lives on the terminal's stack; there is no heap,
 * no writable static state and no libc dependency. */
typedef struct {
  uint8_t head[STREAM_HEADER];
  uint16_t session, seq, pos, length, crc, trailer, encoded, rectangles;
  uint8_t code_left, zero, bad, fault, active, run, repeat, half, hi;
  uint32_t remaining;
} StreamPackBits;

static uint16_t stream_u16(const uint8_t *p) {
  return p[0] | ((uint16_t)p[1] << 8);
}

/* CRC16-CCITT, nibble table, initial 0xffff. */
static __attribute__((always_inline)) inline uint16_t stream_crc(uint16_t c, uint8_t b) {
  static const uint16_t t[16] = {
      0,      0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
      0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef};
  c ^= (uint16_t)b << 8;
  c = (uint16_t)((c << 4) ^ t[c >> 12]);
  return (uint16_t)((c << 4) ^ t[c >> 12]);
}

static void stream_reset_frame(StreamPackBits *s) {
  s->pos = s->length = s->trailer = s->encoded = 0;
  s->crc = 65535;
  s->code_left = s->zero = s->bad = 0;
  s->run = s->half = 0;
}

static void stream_init(StreamPackBits *s, uint16_t session) {
  s->session = session;
  s->seq = s->rectangles = 0;
  s->fault = s->active = 0;
  s->remaining = 0;
  stream_reset_frame(s);
}

/* Validate a complete header. Geometry, body length, session and sequence are
 * checked before the body is decoded. SYNC is accepted after a fault and does
 * not have to match the expected sequence. */
static unsigned stream_header_ok(StreamPackBits *s) {
  const uint8_t *p = s->head;
  unsigned k = p[3], n = stream_u16(p + 8);
  unsigned x = stream_u16(p + 10), y = stream_u16(p + 12);
  unsigned w = stream_u16(p + 14), h = stream_u16(p + 16);
  if (p[0] != 'R' || p[1] != 'S' || p[2] != 6 || stream_u16(p + 4) != s->session ||
      n > STREAM_PAYLOAD)
    return 0;
  if (k != STREAM_SYNC && (s->fault || stream_u16(p + 6) != s->seq)) return 0;
  if (k == STREAM_RECT)
    return !s->active && !n && w && h && x < NSPANEL_PANEL_WIDTH &&
           y < NSPANEL_PANEL_HEIGHT && w <= NSPANEL_PANEL_WIDTH - x &&
           h <= NSPANEL_PANEL_HEIGHT - y;
  if (k == STREAM_BRIGHTNESS) return !s->active && !n && x <= 100 && !y && !w && !h;
  if (x || y || w || h) return 0;
  if (k == STREAM_DATA) return s->active && n;
  if (n) return 0;
  if (k == STREAM_END) return s->active && !s->remaining;
  return k == STREAM_PING || k == STREAM_EXIT || k == STREAM_SYNC;
}

/* Decode one byte of the COBS payload. The caller has already removed the
 * COBS code bytes and inserts a zero after every code block. */
static __attribute__((always_inline)) inline void stream_decoded(
    StreamPackBits *s, uint8_t b) {
  unsigned pos = s->pos++;
  if (pos < STREAM_HEADER) {
    s->head[pos] = b;
    if (pos == 18) s->trailer = s->crc;
    s->crc = stream_crc(s->crc, b);
    if (pos == 19) {
      s->length = stream_u16(s->head + 8);
      if (s->trailer != stream_u16(s->head + 18) || !stream_header_ok(s)) s->bad = 1;
    }
  } else if (pos < STREAM_HEADER + s->length) {
    s->crc = stream_crc(s->crc, b);
    if (!s->bad && s->head[3] == STREAM_DATA) {
      /* PackBits token: high bit = repeat, low seven bits = run length - 1.
       * A repeat token is followed by one RGB565 pixel; a literal token by
       * that many pixels. Pixels go straight to the panel. */
      if (!s->run) {
        s->run = (b & 127) + 1;
        s->repeat = b >> 7;
        if (2u * s->run > s->remaining) s->bad = 1;
      } else if (!s->half) {
        s->hi = b;
        s->half = 1;
      } else {
        if (s->repeat) {
          unsigned n = s->run;
          STREAM_REPEAT(s->hi, b, n);
          s->remaining -= 2u * n;
          s->run = 0;
        } else {
          STREAM_BYTE(s->hi);
          STREAM_BYTE(b);
          s->remaining -= 2;
          --s->run;
        }
        s->half = 0;
      }
    }
  } else if (pos == STREAM_HEADER + s->length) {
    s->trailer = b;
  } else if (pos == STREAM_HEADER + s->length + 1) {
    s->trailer |= (uint16_t)b << 8;
  } else {
    s->bad = 1;
  }
}

/*
 * Feed one COBS byte. A zero byte ends a frame and returns a reverse-channel
 * status, or zero when no report is due. After an error no pixels are written
 * until a valid SYNC; the sender must redraw the entire rectangle.
 *
 * BRIGHTNESS (kind 7) has no body. Its percentage is in the header's x field.
 * After validation it calls the stock backlight routine at 0x0800bbfc with
 * force=1. The low bit in 0x0800bbfd is the Thumb entry. Status 135 confirms
 * the change. A host without live-brightness support cannot interpret status
 * 134 and does not know kind 7, so it must be updated before the TFT.
 */
static unsigned stream_feed(StreamPackBits *s, uint8_t b) {
  if (b) {
    if (s->encoded >= 537) {
      s->bad = 1;
      return 0;
    }
    ++s->encoded;
    if (s->code_left) {
      stream_decoded(s, b);
      --s->code_left;
    } else {
      if (s->zero) stream_decoded(s, 0);
      s->code_left = b - 1;
      s->zero = b != 255;
    }
    return 0;
  }
  if (!s->encoded) return 0;
  unsigned event = 0;
  if (s->bad || s->run || s->half || s->code_left ||
      s->pos != STREAM_HEADER + s->length + 2 || s->crc != s->trailer) {
    if (!s->fault) event = STREAM_ERROR;
    s->fault = 1;
    s->active = 0;
  } else {
    unsigned k = s->head[3];
    s->seq = (uint16_t)(stream_u16(s->head + 6) + 1);
    if (k == STREAM_SYNC) {
      s->fault = s->active = 0;
      s->remaining = 0;
      event = STREAM_SYNCED;
    } else if (k == STREAM_RECT) {
      unsigned x = stream_u16(s->head + 10), y = stream_u16(s->head + 12);
      unsigned w = stream_u16(s->head + 14), h = stream_u16(s->head + 16);
      STREAM_WINDOW(x, y, w, h);
      s->remaining = 2u * w * h;
      s->active = 1;
    } else if (k == STREAM_END) {
      s->active = 0;
      ++s->rectangles;
      event = STREAM_DONE;
    } else if (k == STREAM_EXIT) {
      event = STREAM_BYE;
    } else if (k == STREAM_BRIGHTNESS) {
      ((void (*)(uint32_t, uint32_t))0x0800bbfd)(stream_u16(s->head + 10), 1);
      event = STREAM_BRIGHTNESS_DONE;
    }
  }
  stream_reset_frame(s);
  return event;
}
