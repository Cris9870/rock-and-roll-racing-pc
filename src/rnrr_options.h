/* PC settings in the game's own OPTIONS screen (garage -> SET OPTIONS).
 *
 * The ROM side (tools/options_menu.py, generated into rnrr_options_patch.inc)
 * adds FORMATO, PANTALLA, FILTRO and MOVIMIENTO rows and keeps their values in
 * a WRAM mailbox at $7F:FFF0. The host side here fills the mailbox with the
 * live settings after every frame, applies and saves what the menu changed,
 * and widens the screen's top panel so the longer Spanish rows fit. */
#ifndef RNRR_OPTIONS_H
#define RNRR_OPTIONS_H

#include <stddef.h>
#include <stdint.h>

/* Patch the OPTIONS screen into the ROM image before the machine starts.
 * Returns nonzero when applied; a ROM whose bytes differ keeps its menu. */
int RnrrOptionsPatchRom(uint8_t *rom, size_t size);
/* After every simulated frame. */
void RnrrOptionsAfterFrame(void);
/* The guest's timeline jumped (reset, save-state load, rewind). */
void RnrrOptionsOnReset(void);

#endif
