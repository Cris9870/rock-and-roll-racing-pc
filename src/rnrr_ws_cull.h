#ifndef RNRR_WS_CULL_H
#define RNRR_WS_CULL_H

#include <stdint.h>

extern volatile uint16_t g_rnrr_cull_left;
extern volatile uint16_t g_rnrr_cull_right;
/* X a hidden ("parked") sprite is moved to: the right cull bound in
 * widescreen, so it and its part offsets stay in the 9-bit X dead zone. */
extern volatile uint16_t g_rnrr_park_x;
extern volatile uint8_t g_rnrr_oc_fast;
extern volatile uint8_t g_rnrr_oc_slow;

#endif
