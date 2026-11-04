/* Widescreen sprite patches, located by signature so one build serves every
 * supported revision (the European image's bank-0 code sits $A0 bytes after
 * the USA one's, and its sprite-attribute WRAM $26 bytes lower).
 *
 * 1. The sprite emitter's screen-X reject test is relocated into free ROM at
 *    $01:FF10/FF30/FF60. The relocated test reads its two bounds from
 *    immediates the host rewrites at run time (rnrr_display.c,
 *    apply_cull_window) and sets the OAM high-X bit for X >= 256 too, so the
 *    widescreen margins get real sprites instead of wrapped ones. The WRAM
 *    tables the relocated code writes are read from the original code, which
 *    is how the same routine fits both revisions.
 * 2. Every place that parks a hidden sprite at X=$180 is recorded, so the host
 *    can move that X out of the widened view (RnrrRomPatchParkSites). */
#include "rnrr_rompatch.h"

#include <stdio.h>
#include <string.h>

static RnrrParkSite s_park[16];
static int s_park_count;

static size_t lorom_offset(uint32_t addr)
{
    return (size_t)((addr >> 16) & 0x7f) * 0x8000 + (addr & 0x7fff);
}

static uint32_t lorom_addr(size_t off)
{
    return (uint32_t)(((off / 0x8000) << 16) | 0x8000 | (off % 0x8000));
}

/* All matches of a pattern in bank $00 (the emitter's bank). */
static int find_all(const uint8_t *rom, size_t size, const uint8_t *pat, size_t n,
                    size_t *out, int cap)
{
    int found = 0;
    size_t end = size < 0x8000 ? size : 0x8000;
    for (size_t i = 0; i + n <= end; i++) {
        if (memcmp(rom + i, pat, n) != 0) continue;
        if (found < cap) out[found] = i;
        found++;
    }
    return found;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void park_add(uint32_t addr, int wide)
{
    if (s_park_count < (int)(sizeof(s_park) / sizeof(s_park[0]))) {
        s_park[s_park_count].addr = addr;
        s_park[s_park_count].wide = (uint8_t)wide;
        s_park_count++;
    }
}

int RnrrRomPatchApply(uint8_t *rom, size_t size)
{
    static const uint8_t kSiteA[] = {0xc9, 0xc0, 0xff, 0xb0, 0x09, 0xc9, 0x00, 0x01, 0xb0, 0xcc};
    static const uint8_t kSiteB[] = {0xc9, 0xc0, 0xff, 0xb0, 0x16, 0xc9, 0x00, 0x01, 0x90, 0x07};
    static const uint8_t kSiteC[] = {0xc9, 0xc0, 0xff, 0xb0, 0x16, 0xc9, 0x00, 0x01, 0x90, 0x08};
    static const uint8_t kParkWide[] = {0xe6, 0x9e, 0xa9, 0x80, 0x00, 0x85, 0xa0};
    size_t a[4], b[2], c[2], pw[4];
    s_park_count = 0;
    if (size < 0x10000) return 0;
    int na = find_all(rom, size, kSiteA, sizeof(kSiteA), a, 4);
    int nb = find_all(rom, size, kSiteB, sizeof(kSiteB), b, 2);
    int nc = find_all(rom, size, kSiteC, sizeof(kSiteC), c, 2);
    if (na != 2 || nb != 1 || nc != 1) {
        fprintf(stderr, "[rnrr] sprite emitter not found (%d/%d/%d); widescreen sprite patch not applied\n",
                na, nb, nc);
        return 0;
    }
    /* Operands the relocated code needs, read from the original routines:
     * site B: ... e2 30 9d <xlo> 9e <xhi> ...      (+19 STA, +22 STZ)
     * site C: ... a9 05 9d <yoff> 60 e2 30 9d <xlo> 9e <xhi> 60 */
    const uint8_t *sb = rom + b[0], *sc = rom + c[0];
    if (sb[19] != 0x9d || sb[22] != 0x9e || sc[14] != 0x9d || sc[20] != 0x9d || sc[23] != 0x9e) {
        fprintf(stderr, "[rnrr] sprite emitter layout differs; widescreen sprite patch not applied\n");
        return 0;
    }
    uint16_t car_xlo = rd16(sb + 20), car_xhi = rd16(sb + 23);
    uint16_t shd_yoff = rd16(sc + 15), shd_xlo = rd16(sc + 21), shd_xhi = rd16(sc + 24);

    size_t free_off = lorom_offset(0x01FF10);
    for (size_t i = free_off; i < free_off + 0x7C; i++)
        if (rom[i] != 0x00 && rom[i] != 0xff) {
            fprintf(stderr, "[rnrr] no free ROM at $01:FF10; widescreen sprite patch not applied\n");
            return 0;
        }

    /* R1 (objects): bounds at +1 (left) and +6 (right); park X at +0x17. */
    const uint8_t r1[] = {
        0xc9, 0xc0, 0xff, 0xb0, 0x0a, 0xc9, 0x00, 0x01, 0xb0, 0x0a, 0xc9, 0x00, 0x01, 0x90, 0x02,
        0xe6, 0x9e, 0x85, 0xa0, 0x6b,
        0xe6, 0x9e, 0xa9, 0x80, 0x00, 0x85, 0xa0, 0x6b};
    /* R3 (cars): writes the car's X low byte / high flag; parks at +0x2b. */
    const uint8_t r3[] = {
        0xc9, 0xc0, 0xff, 0xb0, 0x0a, 0xc9, 0x00, 0x01, 0xb0, 0x19, 0xc9, 0x00, 0x01, 0x90, 0x0b,
        0xe2, 0x30, 0x9d, (uint8_t)car_xlo, (uint8_t)(car_xlo >> 8), 0xa9, 0x01,
        0x9d, (uint8_t)car_xhi, (uint8_t)(car_xhi >> 8), 0x6b,
        0xe2, 0x30, 0x9d, (uint8_t)car_xlo, (uint8_t)(car_xlo >> 8),
        0x9e, (uint8_t)car_xhi, (uint8_t)(car_xhi >> 8), 0x6b,
        0xe2, 0x30, 0xa9, 0x01, 0x9d, (uint8_t)car_xhi, (uint8_t)(car_xhi >> 8),
        0xa9, 0x80, 0x9d, (uint8_t)car_xlo, (uint8_t)(car_xlo >> 8), 0x6b};
    /* R4 (shadows): same shape; off-screen keeps the game's own Y=5 hide. */
    const uint8_t r4[] = {
        0xc9, 0xc0, 0xff, 0xb0, 0x0a, 0xc9, 0x00, 0x01, 0xb0, 0x19, 0xc9, 0x00, 0x01, 0x90, 0x0b,
        0xe2, 0x30, 0x9d, (uint8_t)shd_xlo, (uint8_t)(shd_xlo >> 8), 0xa9, 0x01,
        0x9d, (uint8_t)shd_xhi, (uint8_t)(shd_xhi >> 8), 0x6b,
        0xe2, 0x30, 0x9d, (uint8_t)shd_xlo, (uint8_t)(shd_xlo >> 8),
        0x9e, (uint8_t)shd_xhi, (uint8_t)(shd_xhi >> 8), 0x6b,
        0xe2, 0x30, 0xa9, 0x05, 0x9d, (uint8_t)shd_yoff, (uint8_t)(shd_yoff >> 8), 0x6b};
    _Static_assert(sizeof(r1) == 28 && sizeof(r3) == 48 && sizeof(r4) == 43, "relocated routine sizes");

    /* Park sites in the untouched code: "INC $9E; LDA #$0080; STA $A0" (objects)
     * and "LDA #$01; STA xhi,X; LDA #$80; STA xlo,X" (cars), found before any
     * byte is changed. */
    int npw = find_all(rom, size, kParkWide, sizeof(kParkWide), pw, 4);
    for (int i = 0; i < npw && i < 4; i++) park_add(lorom_addr(pw[i] + 3), 1);
    {
        const uint8_t car_park[] = {0xa9, 0x01, 0x9d, (uint8_t)car_xhi, (uint8_t)(car_xhi >> 8),
                                    0xa9, 0x80, 0x9d, (uint8_t)car_xlo, (uint8_t)(car_xlo >> 8)};
        size_t cp[6];
        int ncp = find_all(rom, size, car_park, sizeof(car_park), cp, 6);
        for (int i = 0; i < ncp && i < 6; i++) park_add(lorom_addr(cp[i] + 6), 0);
    }

    memcpy(rom + lorom_offset(0x01FF10), r1, sizeof(r1));
    memcpy(rom + lorom_offset(0x01FF30), r3, sizeof(r3));
    memcpy(rom + lorom_offset(0x01FF60), r4, sizeof(r4));
    park_add(0x01FF27, 1);
    park_add(0x01FF5B, 0);
    static const uint8_t jsl_a[] = {0x22, 0x10, 0xff, 0x01, 0x80, 0x0c};
    static const uint8_t jsl_b[] = {0x22, 0x30, 0xff, 0x01, 0x80, 0x1f};
    static const uint8_t jsl_c[] = {0x22, 0x60, 0xff, 0x01, 0x60};
    memcpy(rom + a[0], jsl_a, sizeof(jsl_a));
    memcpy(rom + a[1], jsl_a, sizeof(jsl_a));
    memcpy(rom + b[0], jsl_b, sizeof(jsl_b));
    memcpy(rom + c[0], jsl_c, sizeof(jsl_c));
    fprintf(stderr, "[rnrr] widescreen sprite patch applied (emitter $%06X/$%06X/$%06X/$%06X, "
                    "car X $%04X/$%04X, %d park sites)\n",
            lorom_addr(a[0]), lorom_addr(a[1]), lorom_addr(b[0]), lorom_addr(c[0]),
            car_xlo, car_xhi, s_park_count);
    return 1;
}

int RnrrRomPatchParkSites(const RnrrParkSite **sites)
{
    if (sites) *sites = s_park;
    return s_park_count;
}
