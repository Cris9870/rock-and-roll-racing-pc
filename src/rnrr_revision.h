#ifndef RNRR_REVISION_H
#define RNRR_REVISION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Rock & Roll Racing dumps this port knows. Identity is the SHA-256 of
 * the image without a 512-byte copier header; the launcher stores that
 * normalized image as rom/juego.sfc, so the game only ever sees these. */
typedef enum {
    RNRR_REV_UNKNOWN = 0,
    RNRR_REV_EUROPE,  /* "Rock & Roll Racing" logo, blank internal header */
    RNRR_REV_USA,     /* "Rock N' Roll Racing" */
} RnrrRevision;

/* Identifies an image (a copier header, if present, is ignored). Writes the
 * header-less SHA-256 as 64 lowercase hex chars + NUL when sha_hex is given. */
RnrrRevision RnrrIdentifyRom(const uint8_t *data, size_t size, char sha_hex[65]);

const char *RnrrRevisionName(RnrrRevision rev);
/* 1 when the revision has been played through on this port; 0 when it is
 * supported by signature but nobody has confirmed it yet. */
int RnrrRevisionVerified(RnrrRevision rev);
/* SHA-256 (hex) of the header-less image of a known revision. */
const char *RnrrRevisionSha256(RnrrRevision rev);
/* CRC32 of the header-less image, as 8 lowercase hex chars + NUL. */
void RnrrRomCrc32(const uint8_t *data, size_t size, char crc_hex[9]);

#ifdef __cplusplus
}
#endif

#endif
