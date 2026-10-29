/* SPDX-License-Identifier: MIT */
/*
 * Panel geometry for the terminal overlay.
 *
 * The EU and US NSPanels share the same display controller and stock runtime
 * addresses, but their visible areas differ. The EU build is landscape and
 * exposes 454x320 pixels inside a 480x320 controller; the US build is portrait
 * and exposes the full 320x480 controller. The builder selects a profile with
 * -D flags; the defaults below are the EU layout so a plain compile still
 * produces the EU receiver.
 *
 * NSPANEL_PANEL_* is the visible framebuffer size used by the host and the
 * geometry checks. NSPANEL_ENTRY_* is the width/height that the stock runtime
 * passes to helper 0; it is the controller size, which can be wider than the
 * visible area on the EU panel. NSPANEL_ENTRY_STATE is the stock state byte at
 * 0x20000101: 2 on the EU project state machine and 1 on the US one.
 */
#pragma once

#ifndef NSPANEL_PANEL_WIDTH
#define NSPANEL_PANEL_WIDTH 454
#endif
#ifndef NSPANEL_PANEL_HEIGHT
#define NSPANEL_PANEL_HEIGHT 320
#endif
#ifndef NSPANEL_ENTRY_WIDTH
#define NSPANEL_ENTRY_WIDTH 480
#endif
#ifndef NSPANEL_ENTRY_HEIGHT
#define NSPANEL_ENTRY_HEIGHT 320
#endif
#ifndef NSPANEL_ENTRY_STATE
#define NSPANEL_ENTRY_STATE 2
#endif
