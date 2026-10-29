/* SPDX-License-Identifier: MIT */
/*
 * Shared hardware interfaces for the NSPanel display terminal.
 *
 * Target: the STM32-class display MCU in the EU and US Sonoff NSPanel
 * (NX4832F035_011C, MCU code F130). Addresses are fixed by the stock
 * Nextion runtime and are not portable to another panel or display model.
 *
 * The terminal is loaded as a helper overlay while the stock UI is
 * suspended. It borrows the stock UART, DMA, GPIO and project RAM, then
 * restores them before returning. See docs/FIRMWARE.md for the memory map.
 */
#pragma once
#include <stdint.h>

#include "panel_profile.h"

/* One Goodix contact report. The stock path supports at most five contacts;
 * this terminal publishes the first one and uses the mask/fresh flags to tell
 * the host whether a contact is held or the controller reported a fault. */
typedef struct {
  uint16_t xy[10];
  uint8_t mask, fresh;
  uint16_t pad;
} TouchState;

/* Callbacks provided by the stock runtime (or by the host-side tests). */
typedef struct {
  void (*poll)(uint8_t *, TouchState *);
  void (*report)(uint32_t, uint32_t, uint32_t, uint32_t, const TouchState *);
  void (*probe)(TouchState *);
} Services;

/* Volatile register accessors. */
#define R(a) (*(volatile uint32_t *)(a))

/* USART1, DMA1 and DMAMUX, as allocated by the stock display runtime. */
#define U 0x40013800u
#define D 0x40020000u
#define M 0x40020800u

/* Stock baud-rate setter. Thumb address; the low bit is part of the call. */
#define BAUD ((void (*)(uint32_t))0x0800bf89)
