#ifndef RNRR_ROMPATCH_H
#define RNRR_ROMPATCH_H

#include <stddef.h>
#include <stdint.h>

/* A ROM immediate that parks a hidden sprite at X=$180: a 16-bit LDA #$0080
 * (wide) or an 8-bit LDA #$80. addr is a LoROM bus address. */
typedef struct { uint32_t addr; uint8_t wide; } RnrrParkSite;

/* Patches the header-less image in place; returns 1 when applied. */
int RnrrRomPatchApply(uint8_t *rom, size_t size);
/* Park sites found by the last RnrrRomPatchApply. */
int RnrrRomPatchParkSites(const RnrrParkSite **sites);

#endif
