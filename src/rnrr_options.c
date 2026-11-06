#include "rnrr_options.h"

#include <stdio.h>
#include <string.h>

#include "common_rtl.h"
#include "snes/ppu.h"
#include "desktop/config.h"
#include "desktop/host_main.h"
#include "rnrr_display.h"
#include "rnrr_settings.h"

void ChangeWindowScale(int scale_step);   /* desktop/host_main.c */

typedef struct {
    uint32_t addr;
    const uint8_t *expect;
    uint16_t expect_len;
    const uint8_t *data;
    uint16_t len;
} RnrrOptSite;

#include "rnrr_options_patch.inc"

/* Mailbox layout (tools/options_menu.py). */
enum { MB_FORMATO, MB_PANTALLA, MB_FILTRO, MB_MOVIMIENTO, MB_DIRTY, MB_ACTIVE };
enum { MB_VALUES = 4, MB_CHANGED = 0xA5 };
static const uint8_t kCounts[MB_VALUES] = {RNRR_ASPECT_COUNT, 2, RNRR_IMAGE_COUNT, 2};

static int s_patched;
static uint8_t s_written[MB_VALUES];
static int s_written_valid;
static int s_warned;

static size_t lorom_offset(uint32_t addr)
{
    return (size_t)((addr >> 16) & 0x7f) * 0x8000 + (addr & 0x7fff);
}

int RnrrOptionsPatchRom(uint8_t *rom, size_t size)
{
    s_patched = 0;
    size_t n = sizeof(kRnrrOptSites) / sizeof(kRnrrOptSites[0]);
    for (size_t i = 0; i < n; i++) {
        const RnrrOptSite *s = &kRnrrOptSites[i];
        size_t off = lorom_offset(s->addr);
        if (off + s->expect_len > size || memcmp(rom + off, s->expect, s->expect_len) != 0) {
            fprintf(stderr, "[rnrr] OPTIONS screen differs at $%06X; PC settings not added to it\n", s->addr);
            return 0;
        }
    }
    size_t code = lorom_offset(RNRR_OPT_CODE_ADDR);
    if (code + sizeof(kRnrrOptCode) > size) return 0;
    for (size_t i = 0; i < sizeof(kRnrrOptCode); i++)
        if (rom[code + i] != 0x00 && rom[code + i] != 0xff) {
            fprintf(stderr, "[rnrr] no free ROM at $%06X; PC settings not added to OPTIONS\n",
                    RNRR_OPT_CODE_ADDR);
            return 0;
        }
    memcpy(rom + code, kRnrrOptCode, sizeof(kRnrrOptCode));
    for (size_t i = 0; i < n; i++)
        if (kRnrrOptSites[i].len)
            memcpy(rom + lorom_offset(kRnrrOptSites[i].addr), kRnrrOptSites[i].data, kRnrrOptSites[i].len);
    s_patched = 1;
    s_written_valid = 0;
    fprintf(stderr, "[rnrr] OPTIONS screen: PC settings added (%u bytes at $%06X)\n",
            (unsigned)sizeof(kRnrrOptCode), RNRR_OPT_CODE_ADDR);
    return 1;
}

/* ------------------------------------------------------- live settings -- */

static int current_aspect(void)
{
    switch (RnrrDisplayWidescreenMode()) {
    case RNRR_WS_OFF: return RNRR_ASPECT_4_3;
    case RNRR_WS_21_9:
    case RNRR_WS_32_9: return RNRR_ASPECT_21_9;   /* 32:9 is drawn as 21:9 */
    case RNRR_WS_FIT: return RNRR_ASPECT_FIT;
    default: return RNRR_ASPECT_16_9;
    }
}

static void current(uint8_t v[MB_VALUES])
{
    v[MB_FORMATO] = (uint8_t)current_aspect();
    v[MB_PANTALLA] = g_config.fullscreen ? 0 : 1;
    v[MB_FILTRO] = (uint8_t)RnrrSettingsImageFromShader(g_config.shader);
    v[MB_MOVIMIENTO] = RnrrDisplayPresentationEnabled() ? 0 : 1;
}

static void apply(const uint8_t want[MB_VALUES])
{
    static const int kWidescreen[RNRR_ASPECT_COUNT] = {RNRR_WS_OFF, RNRR_WS_16_9, RNRR_WS_21_9, RNRR_WS_FIT};
    static const char *const kAspect[] = {"4:3", "16:9", "21:9", "ajustar"};
    static const char *const kImage[] = {"suavizada", "nitida", "crt", "tv"};
    uint8_t cur[MB_VALUES];
    current(cur);
    int mods = 0, config = 0;
    if (want[MB_FORMATO] != cur[MB_FORMATO]) {
        RnrrDisplaySetWidescreen(kWidescreen[want[MB_FORMATO]]);
        RnrrOnReset();
        ChangeWindowScale(0);   /* a window follows the new width; fullscreen ignores it */
        mods = 1;
        fprintf(stderr, "[rnrr] opciones: formato %s\n", kAspect[want[MB_FORMATO]]);
    }
    if (want[MB_PANTALLA] != cur[MB_PANTALLA]) {
        snesrecomp_desktop_set_fullscreen(want[MB_PANTALLA] == 0);
        config = 1;
        fprintf(stderr, "[rnrr] opciones: pantalla %s\n", want[MB_PANTALLA] ? "ventana" : "completa");
    }
    if (want[MB_FILTRO] != cur[MB_FILTRO]) {
        g_config.shader = RnrrSettingsShaderPath(want[MB_FILTRO]);
        snesrecomp_desktop_apply_video();
        config = 1;
        fprintf(stderr, "[rnrr] opciones: filtro %s\n", kImage[want[MB_FILTRO]]);
    }
    if (want[MB_MOVIMIENTO] != cur[MB_MOVIMIENTO]) {
        RnrrDisplaySetPresentationFps(want[MB_MOVIMIENTO] == 0, RnrrDisplayPresentationRate());
        RnrrOnReset();
        snesrecomp_desktop_request_clock_reset();
        snesrecomp_desktop_apply_video();   /* a decoupled presentation swaps without vsync */
        mods = 1;
        fprintf(stderr, "[rnrr] opciones: movimiento %s\n", want[MB_MOVIMIENTO] ? "original" : "suave");
    }
    if (mods && !RnrrSettingsWriteModState(want[MB_FORMATO], want[MB_MOVIMIENTO] == 0))
        fprintf(stderr, "[rnrr] opciones: cannot write mods/preloaded/state.toml\n");
    if (config) snesrecomp_desktop_save_config();
}

/* ------------------------------------------------------------- the panel -- */

/* Our screen, not just our flag: a stale flag must never touch another
 * screen's tilemap, so the title we printed has to be on BG3 too. */
static int on_options_screen(void)
{
    static const char kTitle[] = "OPCIONES";
    if (!g_ppu || g_ram[RNRR_OPT_MAILBOX + MB_ACTIVE] != 1) return 0;
    uint16_t bg3 = (uint16_t)PPU_bgTilemapAdr(g_ppu, 2);
    for (int i = 0; kTitle[i]; i++) {
        uint16_t e = g_ppu->vram[(bg3 + 3 * 32 + 12 + i) & 0x7fff];
        if ((e & 0x3ff) != (uint16_t)(kTitle[i] - 0x20)) return 0;
    }
    return 1;
}

/* The stock screen puts its list in a small panel between four car bubbles.
 * Rebuild BG1 rows 1-17 as one wide panel out of the password panel below
 * (rows 18-26: top edge, six body rows, bottom edge, shadow); the granite
 * texture hides the repeat. Idempotent, and cheap when already done. */
static void widen_panel(void)
{
    static const uint8_t kSrc[17] = {18, 19, 20, 21, 22, 23, 24, 19, 20, 21, 22, 23, 24, 19, 20, 25, 26};
    uint16_t bg1 = (uint16_t)PPU_bgTilemapAdr(g_ppu, 0);
    for (int r = 0; r < 17; r++) {
        uint16_t *dst = &g_ppu->vram[(bg1 + (r + 1) * 32) & 0x7fff];
        const uint16_t *src = &g_ppu->vram[(bg1 + kSrc[r] * 32) & 0x7fff];
        if (memcmp(dst, src, 32 * sizeof(uint16_t)) != 0) memcpy(dst, src, 32 * sizeof(uint16_t));
    }
}

/* ------------------------------------------------------------- mailbox -- */

void RnrrOptionsAfterFrame(void)
{
    if (!s_patched) return;
    uint8_t *mb = &g_ram[RNRR_OPT_MAILBOX];
    if (mb[MB_DIRTY] == MB_CHANGED) {
        uint8_t want[MB_VALUES];
        for (int i = 0; i < MB_VALUES; i++) want[i] = mb[i] < kCounts[i] ? mb[i] : 0;
        mb[MB_DIRTY] = 0;
        apply(want);
    } else if (s_written_valid && !s_warned) {
        /* Diagnostic: the game must never use these bytes. A RAM clear (0)
         * at boot is expected. */
        for (int i = 0; i < MB_VALUES; i++)
            if (mb[i] != s_written[i] && mb[i] != 0) {
                fprintf(stderr, "[rnrr] opciones: WRAM $7F:%04X changed outside the menu (%02X -> %02X)\n",
                        0xfff0 + i, s_written[i], mb[i]);
                s_warned = 1;
                break;
            }
    }
    current(s_written);
    memcpy(mb, s_written, MB_VALUES);
    s_written_valid = 1;
    if (on_options_screen()) widen_panel();
}

void RnrrOptionsOnReset(void)
{
    s_written_valid = 0;
    if (!s_patched) return;
    current(s_written);
    memcpy(&g_ram[RNRR_OPT_MAILBOX], s_written, MB_VALUES);
}
