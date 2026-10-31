/* SPDX-License-Identifier: MIT */
/*
 * NSPanel protocol-7 terminal receiver with burst completion and live brightness.
 *
 * This is the RAM overlay that runs while the stock Nextion UI is suspended.
 * It is copied by terminal_loader.c to 0x20000900 and entered at
 * 0x20000901 (Thumb). It borrows the stock USART1, DMA1 channel 1, GPIOA and
 * project RAM, then restores them before returning.
 *
 * Responsibilities:
 *   - receive COBS-framed protocol-6 records at 2 or 4 Mbaud;
 *   - draw PackBits RGB565 rectangles through the stock window/GPIO path;
 *   - poll the Goodix touch controller and publish contacts;
 *   - apply checked live-brightness records between rectangles;
 *   - restore the stock runtime after EXIT, a fault or 3 s of silence.
 *
 * There is no heap, no libc and no writable static state. The only persistent
 * effects are the pixels written and the backlight value. See docs/FIRMWARE.md
 * for the memory map, entry/exit contract and limits.
 */
#include "terminal_api.h"
#include "stream_touch.h"

/* Configure the LCD window with the stock ROM routine. Coordinates are
 * inclusive; the stock routine handles the profile's visible area. */
static void stream_window(unsigned x, unsigned y, unsigned w, unsigned h) {
  ((void (*)(uint32_t, uint32_t, uint32_t, uint32_t))0x08007f71)(
      x, y, x + w - 1, y + h - 1);
  R(0x50000018) = 1u << 11; /* stock window latch */
}

/* Same byte-latch / WR-low / WR-high order as the recovered ROM writer.
 * No GPIO reinitialisation, clock change or guessed delay is introduced. */
#define STREAM_WINDOW(x, y, w, h) stream_window(x, y, w, h)
#define STREAM_BYTE(b)                                                       \
  do {                                                                       \
    *(volatile uint8_t *)0x50000014 = (b); /* GPIOA output latch, low byte */ \
    R(0x50000028) = 1u << 12;              /* WR low  (BRR) */                \
    R(0x50000018) = 1u << 12;              /* WR high (BSRR) */               \
  } while (0)

/* Keep colour, bus address, strobe mask and count in this small function's
 * registers. Four-pixel groups amortise the branch; trailing pixels retain
 * exactly the same data/WR-low/WR-high sequence. No shortened WR pulse. */
static __attribute__((noinline)) void stream_repeat_gpio(uint8_t hi, uint8_t lo,
                                                         unsigned n) {
#define PIXEL()                                                                 \
  do {                                                                          \
    STREAM_BYTE(hi);                                                            \
    STREAM_BYTE(lo);                                                            \
  } while (0)
  while (n >= 4) {
    PIXEL();
    PIXEL();
    PIXEL();
    PIXEL();
    n -= 4;
  }
  while (n) {
    PIXEL();
    --n;
  }
#undef PIXEL
}
#define STREAM_REPEAT(hi, lo, n) stream_repeat_gpio(hi, lo, n)
#include "stream_protocol.h"

/* Bounded delay against the stock tick at 0x200000bc. The loop cap prevents a
 * stuck tick from hanging the helper. This is not a calibrated microsecond
 * timer; the stock runtime owns the tick. */
static void wait(uint32_t ms) {
  uint32_t t = R(0x200000bc);
  for (uint32_t n = 0; n < 4000000 && R(0x200000bc) - t < ms; n++)
    __asm volatile("nop");
}

/* Send a 32-byte reverse report. The layout is documented in
 * docs/PROTOCOL.md; keep it in sync with the ESPHome component. */
static unsigned report(const StreamPackBits *s, unsigned status,
                       uint32_t consumed, unsigned flags, unsigned high,
                       const TouchState *touch) {
  uint16_t f[16];
  uint8_t *p = (uint8_t *)f;
  f[0] = 0x5253; /* 'S' 'R' */
  p[2] = 7;
  p[3] = status;
  f[2] = s->session;
  f[3] = s->seq;
  f[4] = consumed;
  f[5] = consumed >> 16;
  f[6] = s->rectangles;
  f[7] = flags;
  uint32_t tick = R(0x200000bc);
  f[8] = tick;
  f[9] = tick >> 16;
  p[20] = touch->mask;
  p[21] = touch->fresh;
  f[11] = touch->xy[0];
  f[12] = touch->xy[1];
  f[13] = STREAM_WINDOW_BYTES;
  f[14] = high;
  uint16_t c = 65535;
  for (unsigned i = 0; i < 30; i++) c = stream_crc(c, p[i]);
  f[15] = c;
  for (unsigned i = 0; i < 32; i++) {
    unsigned n = 100000;
    while (!(R(U + 0x1c) & 128)) /* TXE */
      if (!--n) return 0;
    R(U + 0x28) = p[i]; /* TDR */
  }
  return 1;
}

/*
 * Terminal entry point, called by the helper loader after it copies and
 * checks this program. The stock entry passes a pointer to sys0..sys2 at
 * 0x20000294: baud rate, touch enable and session id.
 *
 * Return value: the held-touch mask, plus bit 5 if the last poll saw malformed
 * contact data. The loader stores this back into the stock touch cache.
 */
uint32_t pool_entry(void) {
  const volatile uint32_t *args =
      *(const volatile uint32_t *volatile *)0x20000294;
  uint32_t rate = args[0], touch_enabled = args[1], session = args[2] & 65535;
  if ((rate != 2000000 && rate != 4000000) || touch_enabled > 1 || !session)
    return 1;

  StreamPackBits s;
  TouchState touch;
  uint8_t work[40];
  stream_init(&s, session);
  for (unsigned i = 0; i < sizeof touch; i++)
    ((volatile uint8_t *)&touch)[i] = 0;
  touch.mask = *(volatile uint8_t *)0x2000016a & 63;

  /* Save the DMA channel 1 registers and DMAMUX request that we borrow. */
  uint32_t save[5];
  for (unsigned i = 0; i < 4; i++) save[i] = R(D + 8 + 4 * i);
  save[4] = R(M);

  wait(50);
  BAUD(rate);
  R(0xe000e180) = 1u << 27; /* disable USART1 interrupt while we own it */
  R(U) &= ~32u;             /* CR1: RXNEIE off */
  R(U + 8) &= ~65u;         /* CR3: EIE and DMAR off */
  R(D + 8) = 0;             /* DMA1 channel 1: disable */
  R(M) = 50;                /* DMAMUX: USART1 RX request */
  R(D + 16) = U + 0x24;     /* CPAR: USART1 RDR */
  R(D + 20) = 0x200004f0;   /* CMAR: borrowed 1 KiB UART buffer */
  poll(work, &touch);
  R(U + 0x18) = 8;          /* RQR: flush RX */
  R(U + 0x20) = 31;         /* ICR: clear all flags */
  R(D + 4) = 15;            /* DMA1 IFCR: clear channel 1 flags */
  R(D + 12) = STREAM_RING;  /* CNDTR: 1024-byte circular ring */
  R(D + 8) = 0xa1;          /* CCR: MINC | CIRC | EN */
  R(U + 8) |= 64;           /* CR3: DMAR on */

  uint32_t consumed = 0, published = 0, last = R(0x200000bc),
           last_touch = last, high = 0, flags = 0;
  unsigned ok = report(&s, STREAM_READY_BRIGHTNESS, 0, 0, 0, &touch), status = 0;

  while (ok) {
    flags = (R(U + 0x1c) & 15) | ((R(D) & 8) << 8);
    unsigned head = (STREAM_RING - R(D + 12)) & (STREAM_RING - 1);
    unsigned count = (head - consumed) & (STREAM_RING - 1);
    __asm volatile("dmb" ::: "memory");
    if (count > high) high = count;
    if (flags || count > STREAM_WINDOW_BYTES) {
      status = STREAM_ERROR;
      flags |= count > STREAM_WINDOW_BYTES ? 0x1000 : 0;
      break;
    }
    uint32_t now = R(0x200000bc);
    if (!count) {
      if (now - last >= 3000) { /* host-inactivity watchdog */
        status = STREAM_ERROR;
        flags = 0x2000;
        break;
      }
    } else {
      last = now;
    }
    if (touch_enabled && now - last_touch >= 10) {
      poll(work, &touch);
      last_touch = R(0x200000bc);
      if (touch.fresh && !report(&s, STREAM_PROGRESS, consumed, 0, high, &touch))
        break;
      /* Polling can take time; refresh producer state before touching ring
       * data. */
      continue;
    }
    /* Revisit touch and DMA status after at most 32 encoded bytes, even when
     * repeated pixels expand to substantially more LCD work. */
    if (count > 32) count = 32;
    while (count--) {
      uint8_t b =
          *(volatile uint8_t *)(0x200004f0 + (consumed & (STREAM_RING - 1)));
      unsigned event = stream_feed(&s, b);
      ++consumed;
      if (event || consumed - published >= 256) {
        if (event == STREAM_BYE) poll(work, &touch);
        ok = report(&s, event ? event : STREAM_PROGRESS, consumed, 0, high,
                    &touch);
        published = consumed;
        touch.fresh = 0;
        if (event == STREAM_BYE || !ok) {
          status = event;
          break;
        }
      }
    }
    if (status == STREAM_BYE) break;
  }

  /* Stop our DMA and UART receive, report a final error (except a clean BYE),
   * then restore the borrowed state and the stock baud rate. */
  R(U + 8) &= ~64u; /* CR3: DMAR off */
  R(D + 8) = 0;     /* DMA channel 1 off */
  if (status && status != STREAM_BYE)
    report(&s, status, consumed, flags, high, &touch);
  R(D + 4) = 15;
  for (unsigned i = 1; i < 4; i++) R(D + 8 + 4 * i) = save[i];
  R(M) = save[4];
  R(D + 8) = save[0];
  wait(50);
  BAUD(115200);
  R(0xe000e180) = 1u << 27;
  R(U + 0x18) = 8;
  R(U + 0x20) = 31;
  return touch.mask | (touch.fresh == 2 ? 32 : 0);
}
