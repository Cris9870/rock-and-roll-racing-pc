#include "rnrr_revision.h"

#include <stdio.h>
#include <string.h>

#include "sha256.h"

typedef struct {
    RnrrRevision rev;
    const char *sha256;  /* header-less image */
    const char *name;
    int verified;
} KnownRom;

static const KnownRom kKnown[] = {
    {RNRR_REV_EUROPE, "94d761a7339804271bf77d541f46a1d211918336ce741b561bfc3ddcaca98ec7",
     "Europa", 1},
    /* No-Intro "Rock n' Roll Racing (USA)"; the headered GoodSNES dump
     * (CRC32 3cf0de1f) is the same image behind a copier header. */
    {RNRR_REV_USA, "9d721753301278325c851f1843d669a697aed757dcf6495a31fc31ddf664b182",
     "USA", 0},
};

static void to_hex(const uint8_t d[32], char out[65])
{
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
    out[64] = 0;
}

RnrrRevision RnrrIdentifyRom(const uint8_t *data, size_t size, char sha_hex[65])
{
    char hex[65] = {0};
    if (!data || size < 0x8000) {
        if (sha_hex) sha_hex[0] = 0;
        return RNRR_REV_UNKNOWN;
    }
    if (size % 1024 == 512) { data += 512; size -= 512; }
    uint8_t d[32];
    sha256_compute(data, size, d);
    to_hex(d, hex);
    if (sha_hex) memcpy(sha_hex, hex, 65);
    for (size_t i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); i++)
        if (!strcmp(hex, kKnown[i].sha256)) return kKnown[i].rev;
    return RNRR_REV_UNKNOWN;
}

static const KnownRom *find(RnrrRevision rev)
{
    for (size_t i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); i++)
        if (kKnown[i].rev == rev) return &kKnown[i];
    return NULL;
}

const char *RnrrRevisionName(RnrrRevision rev)
{
    const KnownRom *k = find(rev);
    return k ? k->name : "desconocida";
}

int RnrrRevisionVerified(RnrrRevision rev)
{
    const KnownRom *k = find(rev);
    return k ? k->verified : 0;
}

const char *RnrrRevisionSha256(RnrrRevision rev)
{
    const KnownRom *k = find(rev);
    return k ? k->sha256 : "";
}

void RnrrRomCrc32(const uint8_t *data, size_t size, char crc_hex[9])
{
    if (size % 1024 == 512) { data += 512; size -= 512; }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    snprintf(crc_hex, 9, "%08x", crc ^ 0xFFFFFFFFu);
}
