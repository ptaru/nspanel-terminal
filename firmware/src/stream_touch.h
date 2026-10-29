/* SPDX-License-Identifier: MIT */
/*
 * Minimal GPIO bit-banged I2C for the stock Goodix touch controller.
 *
 * PB0 is SDA and PB1 is SCL. The stock runtime has already configured both
 * pins as open-drain outputs, so this code only changes the output latch
 * through BSRR/BRR. It does not reconfigure GPIO mode, clocks or pull-ups.
 *
 * The delay is a bounded, conservative number of NOPs. It was retained from
 * the hardware-tested V89 path; it is not a calibrated I2C timing guarantee.
 * See docs/FIRMWARE.md for the touch data path and its limits.
 */
#define G 0x50000400u /* GPIOB */

/* Short, bounded half-bit delay. Kept deliberately conservative. */
static void edge_delay(void) {
  for (volatile unsigned i = 0; i < 24; i++) __asm volatile("nop");
}

/* Drive SDA (bit 1) or SCL (bit 2) high through BSRR or low through BRR. */
static void line(unsigned bit, unsigned high) {
  R(G + (high ? 0x18 : 0x28)) = bit;
  edge_delay();
}

static void i_start(void) {
  line(1, 1);
  line(2, 1);
  line(1, 0);
  line(2, 0);
}

static void i_stop(void) {
  line(2, 0);
  line(1, 0);
  line(2, 1);
  line(1, 1);
}

/* Send one byte, MSB first, and return whether the controller acknowledged. */
static int i_tx(uint8_t v) {
  for (unsigned i = 0; i < 8; i++) {
    line(2, 0);
    line(1, (v & 128) != 0);
    line(2, 1);
    v <<= 1;
  }
  line(2, 0);
  line(1, 1);
  line(2, 1);
  int ok = !(R(G + 16) & 1);
  line(2, 0);
  return ok;
}

/* Receive one byte, MSB first. The last byte is NACKed by the caller. */
static uint8_t i_rx(unsigned last) {
  uint8_t v = 0;
  line(1, 1);
  for (unsigned i = 0; i < 8; i++) {
    line(2, 0);
    line(2, 1);
    v = (v << 1) | (R(G + 16) & 1);
  }
  line(2, 0);
  line(1, last);
  line(2, 1);
  line(2, 0);
  line(1, 1);
  return v;
}

/* Register read: write the 16-bit register address, restart, then read. */
static int i_read(uint16_t reg, uint8_t *p, unsigned n) {
  i_start();
  if (!i_tx(0xba) || !i_tx(reg >> 8) || !i_tx(reg)) goto fail;
  i_stop();
  i_start();
  if (!i_tx(0xbb)) goto fail;
  for (unsigned i = 0; i < n; i++) p[i] = i_rx(i + 1 == n);
  i_stop();
  return 1;
fail:
  i_stop();
  return 0;
}

/* Register write: address, then one data byte. */
static int i_write(uint16_t reg, uint8_t v) {
  i_start();
  if (!i_tx(0xba) || !i_tx(reg >> 8) || !i_tx(reg) || !i_tx(v)) goto fail;
  i_stop();
  return 1;
fail:
  i_stop();
  return 0;
}

/*
 * Poll the controller once.
 *
 * fresh: 0 no new report, 1 valid contact(s), 2 malformed contact data,
 *        3 I2C/ACK failure. Bit 5 of mask is a sticky fault indication.
 * The stock coordinate offsets and orientation transform are reused; only
 * the first contact is forwarded to the host by the caller.
 */
static void poll(uint8_t *work, TouchState *s) {
  s->fresh = 0;
  if (R(0x50000010) & 256) return; /* stock panel-busy gate */
  if (!i_read(0x814e, work, 1)) {
    s->mask |= 32;
    s->fresh = 3;
    return;
  }
  unsigned n = work[0] & 15;
  if (!(work[0] & 128)) return; /* no new report */
  if (n > 5) {
    s->mask |= 32;
    s->fresh = 2;
    return;
  }
  if (n && !i_read(0x814f, work, 8 * n)) {
    s->mask |= 32;
    s->fresh = 3;
    return;
  }
  unsigned mask = 0;
  for (unsigned i = 0; i < n; i++) {
    uint8_t *q = work + 8 * i;
    unsigned id = q[0];
    if (id > 4) {
      s->mask |= 32;
      s->fresh = 2;
      return;
    }
    uint16_t x = ((q[1] | (q[2] << 8)) & 4095) + *(volatile uint16_t *)0x20000310;
    uint16_t y = ((q[3] | (q[4] << 8)) & 4095) + *(volatile uint16_t *)0x20000312;
    ((void (*)(uint16_t *, uint16_t *))0x0800968d)(&x, &y);
    s->xy[2 * id] = x;
    s->xy[2 * id + 1] = y;
    mask |= 1u << id;
  }
  if (!i_write(0x814e, 0)) { /* clear the report */
    s->mask |= 32;
    s->fresh = 3;
    return;
  }
  s->mask = mask;
  s->fresh = 1;
}
