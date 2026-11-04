/*
 * Rock N' Roll Racing — desktop host shim.
 *
 * Identity and hooks, nothing else. The host itself is the framework's
 * (snesrecomp/runner/src/desktop/host_main.h): the pre-boot launcher, ROM
 * resolution and digest checks, config.ini and keybinds.ini, the window and
 * the SDL / OpenGL presenters, audio, gamepads, the in-game save-state
 * browser and rewind filmstrip, the OSD, the pacing clock, crash handlers and
 * the post-mortem report, mod packages, Generate & rebuild, and the netplay
 * barrier when the project is built with it. A fix there reaches this
 * project on a submodule pull; nothing in this file needs to change for it.
 *
 * This file used to BE the host -- a ~700-line copy made at scaffold time --
 * so every project scaffolded before a fix kept the bug. Two ports of the
 * same game, a week apart, had different hosts.
 *
 * Nothing here ever ships a ROM. The player supplies one; the host makes
 * that easy (launcher, then positional argument, then a copy beside the
 * executable, then the rom.cfg cache, then a file picker) and checks what it
 * is handed against the digests in rom_identity.txt.
 *
 * Grow this file only with what is specific to THIS title: a custom
 * presenter (prepare_frame / draw_frame), an SPC player, a pacing rule
 * (keep_pacing_debt), a Mods provider. See SnesDesktopHostGame for the
 * complete list of hooks and what each is for.
 */

#include "host_main.h"
#include "game_rtl.h"
#include "rnrr_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rnrr_rompatch.h"
#include "rnrr_launcher.h"
#include "rnrr_revision.h"
#include "host_paths.h"
#include "sha256.h"
#include "snesrecomp_rom_identity.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void rnrr_on_rom_loaded(const uint8_t *rom, size_t size)
{
    char sha[65];
    RnrrRevision rev = RnrrIdentifyRom(rom, size, sha);
    if (rev == RNRR_REV_UNKNOWN) {
        fprintf(stderr, "[rnrr] unsupported ROM (SHA-256 %s): this port accepts Rock & Roll Racing "
                        "(Europe) and Rock n' Roll Racing (USA)\n", sha);
        exit(1);
    }
    fprintf(stderr, "[rnrr] ROM: %s%s\n", RnrrRevisionName(rev),
            RnrrRevisionVerified(rev) ? "" : " (unverified revision)");
    if (size % 1024 == 512) { rom += 512; size -= 512; }
    int native = 0;
#if defined(SNESRECOMP_HAS_GENERATED_CODE)
    /* src/gen is generated from the European image (plus the sprite patch
     * below), so only that revision can run it. engine.cfg ("interpreter",
     * written by the launcher) or RNRR_INTERPRETER=1 selects the interpreter;
     * RNRR_NATIVE=1 forces native. Public builds carry no generated code. */
    if (rev == RNRR_REV_EUROPE) {
        native = 1;
        char path[1024];
        if (snesrecomp_exe_dir_path("engine.cfg", path, sizeof(path))) {
            FILE *f = fopen(path, "r");
            if (f) {
                char word[32] = {0};
                if (fscanf(f, "%31s", word) == 1 && strcmp(word, "interpreter") == 0) native = 0;
                fclose(f);
            }
        }
        const char *e = getenv("RNRR_INTERPRETER");
        if (e && *e) native = atoi(e) == 0;
        e = getenv("RNRR_NATIVE");
        if (e && *e) native = atoi(e) != 0;
    }
#endif
    if (native) {
        fprintf(stderr, "[rnrr] running recompiled code\n");
    } else {
        cpu_select_interpreted_program();
        interp_bridge_set_scheduler_aot_policy(0);
        fprintf(stderr, "[rnrr] running through the interpreter\n");
    }
    if (!getenv("RNRR_NO_ROMPATCH"))  /* test switch: run the unpatched game */
        RnrrRomPatchApply((uint8_t *)rom, size);
}

static const char kRnrrDefaultConfigIni[] =
  "[General]\n"
  "# Automatically save state on quit and reload on start\n"
  "Autosave = 0\n"
  "\n"
  "# Disable the SDL_Delay that happens each frame (slightly better\n"
  "# perf if your display is set to exactly 60hz)\n"
  "DisableFrameDelay = 0\n"
  "SkipLauncher = 0\n"
  "RunAhead = 0\n"
  "\n"
  "[Graphics]\n"
  "# Window size (Auto or WidthxHeight)\n"
  "WindowSize = Auto\n"
  "\n"
  "# Fullscreen mode (0=windowed, 1=desktop fullscreen, 2=fullscreen w/mode change)\n"
  "Fullscreen = 1\n"
  "\n"
  "# Window scale (1=100%, 2=200%, 3=300%, etc.)\n"
  "WindowScale = 3\n"
  "\n"
  "# Use the optimized SNES PPU implementation\n"
  "NewRenderer = 1\n"
  "\n"
  "# Don't keep the aspect ratio\n"
  "IgnoreAspectRatio = 0\n"
  "\n"
  "# Remove the sprite limits per scan line\n"
  "NoSpriteLimits = 1\n"
  "\n"
  "DisplayAspect = 4:3\n"
  "OutputMethod = OpenGL\n"
  "Renderer = opengl\n"
  "LinearFiltering = 0\n"
  "FrameBlend = 0\n"
  "Shader = assets/shaders/rnrr-suavizada.glslp\n"
  "VSync = 1\n"
  "\n"
  "[Sound]\n"
  "EnableAudio = 1\n"
  "# 0..100; VolumeUp / VolumeDown move it in 5% steps and show a bar.\n"
  "Volume = 100\n"
  "AudioFreq = 32000\n"
  "AudioChannels = 2\n"
  "AudioSamples = 512\n"
  "\n"
  "[KeyMap]\n"
  "# This section is for system-level shortcuts (save/load state,\n"
  "# fullscreen, pause, etc.). The 12 SNES controller buttons live\n"
  "# in keybinds.ini next to the executable.\n"
  "Fullscreen = Alt+Return\n"
  "Reset = Ctrl+r\n"
  "Pause = Shift+p\n"
  "PauseDimmed = p\n"
  "Turbo = Tab\n"
  "WindowBigger = Ctrl+Up\n"
  "WindowSmaller = Ctrl+Down\n"
  "VolumeUp = Keypad +\n"
  "VolumeDown = Keypad -\n"
  "DisplayPerf = f\n"
  "ToggleRenderer = r\n"
  "SaveStateMenu = F11\n"
  "Rewind = F12\n"
  "Load =      F1,     F2,     F3,     F4,     F5,     F6,     F7,     F8,     F9,     F10\n"
  "Save = Shift+F1,Shift+F2,Shift+F3,Shift+F4,Shift+F5,Shift+F6,Shift+F7,Shift+F8,Shift+F9,Shift+F10\n"
  "\n"
  "[GamepadMap]\n"
  "# Enable each player's gamepad slot. SDL_GameController-compatible\n"
  "# controllers (Xbox, PlayStation, Switch Pro, etc.) auto-detect\n"
  "# when plugged in. Set to false to force keyboard-only.\n"
  "EnableGamepad1 = true\n"
  "EnableGamepad2 = true\n"
  "\n"
  "# Default Xbox-layout mapping. Order matches kKeys_Controls:\n"
  "#   Up, Down, Left, Right, Select, Start, A, B, X, Y, L, R\n"
  "# Edit + restart to rebind. Shoulder = L1/Lb (top), trigger = L2.\n"
  "Controls =   DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb\n"
  "ControlsP2 = DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb\n"
  "GamepadDeadzone = 7864\n"
  "\n"
  "[Controller]\n"
  "SourceP1 = 2\n"
  "SourceP2 = 2\n"
  "\n"
  "[Rewind]\n"
  "Enabled = 1\n"
  "Depth = 50\n"
  "Interval = 15\n";

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED 1
#endif
#include "desktop/sdl_compat.h"
#ifdef __ANDROID__
/* SDLActivity loads libmain.so and dlsyms its SDL_main. */
#define main SDL_main
#endif

#ifndef SNES_GAME_VERSION
#define SNES_GAME_VERSION "dev"
#endif

/* Not const: main() points the ROM identity at the dump being started. */
static SnesDesktopHostGame kGameHost = {
    .display_name        = "Rock N' Roll Racing",
    .window_title        = "Rock N' Roll Racing",
    .launcher_title      = "Rock N' Roll Racing - Launcher",
    .region              = SNESRECOMP_ROM_REGION,
    .rom_file            = SNESRECOMP_ROM_FILE,
    .expected_sha256_hex = SNESRECOMP_ROM_EXPECTED_SHA256,
    .expected_crc32_hex  = SNESRECOMP_ROM_EXPECTED_CRC32,
    .game_id             = SNESRECOMP_ROM_GAME_ID,
    .build_version       = SNES_GAME_VERSION,
    .game_info           = &kGameInfo,
    .num_players         = 2,
    .sram_path           = NULL,
    .native_widescreen   = 1,
    .state_menu_hotkeys  = 1,
    .state_menu_no_pad_chord = 0,   /* Select+R (View+RB) opens save states */
    .display_aspect_supported = 1,
    .shader_supported    = 1,
    .rewind_settings     = 1,
    .in_game_launcher    = 0,   /* the compact launcher runs before boot */
    .rom_digest_whole_file = 1,
    .default_config_ini  = kRnrrDefaultConfigIni,
    .on_rom_loaded       = rnrr_on_rom_loaded,
    .on_reset            = RnrrOnReset,
    .prepare_frame       = RnrrPrepareFrame,
    .begin_sim_frame     = RnrrBeginSimFrame,
    .end_sim_frame       = RnrrEndSimFrame,
    .draw_frame          = RnrrDrawFrame,
    .presentation_hz     = RnrrPresentationHz,
    .window_base_width   = RnrrWindowBaseWidth,
};

#ifndef __ANDROID__
#undef main   /* desktop: keep plain main() even if SDL_main.h remapped it */
#endif
int RnrrShaderTest(const char *spec);

/* The host verifies the ROM and keys the mod catalog on the SHA-256 it is
 * given. This port accepts more than one revision, so that identity is the
 * digest of the dump actually being started (header-less; the launcher stores
 * a header-less copy). */
static char s_rom_sha[65], s_rom_crc[9];
static void rnrr_set_identity(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 8 * 1024 * 1024) { fclose(f); return; }
    uint8_t *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n &&
        RnrrIdentifyRom(buf, (size_t)n, s_rom_sha) != RNRR_REV_UNKNOWN) {
        RnrrRomCrc32(buf, (size_t)n, s_rom_crc);
        kGameHost.expected_sha256_hex = s_rom_sha;
        kGameHost.expected_crc32_hex = s_rom_crc;
    }
    free(buf);
    fclose(f);
}

static int has_rom_ext(const char *s)
{
    size_t n = strlen(s);
    return n > 4 && (!_stricmp(s + n - 4, ".sfc") || !_stricmp(s + n - 4, ".smc"));
}

int main(int argc, char **argv)
{
    /* Every SDL window (launcher and game) takes the exe's icon resource. */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    {
        const char *shader_test = getenv("RNRR_SHADER_TEST");
        if (shader_test && *shader_test) return RnrrShaderTest(shader_test);
    }
    /* Plain double-click: show the compact launcher, then boot straight into
     * the game. Any command-line argument keeps the framework's behaviour. */
    if (argc == 1) {
        static char rom[1024];
        static char *args[7];
        int r = RnrrLauncherRun(kRnrrDefaultConfigIni, SNESRECOMP_ROM_EXPECTED_SHA256, rom, sizeof(rom));
        if (r == 0) return 0;
        args[0] = argv[0];
        rnrr_set_identity(rom);
        args[1] = (char *)"--no-launcher";
        args[2] = rom;
        /* Test hook: drive the game headless after an automated launch. */
        const char *test_script = getenv("RNRR_LAUNCHER_TEST_SCRIPT");
        if (test_script && *test_script) {
            args[3] = (char *)"--script";
            args[4] = (char *)test_script;
            return snesrecomp_desktop_main(&kGameHost, 5, args);
        }
        return snesrecomp_desktop_main(&kGameHost, 3, args);
    }
    for (int i = 1; i < argc; i++)
        if (has_rom_ext(argv[i])) rnrr_set_identity(argv[i]);
    return snesrecomp_desktop_main(&kGameHost, argc, argv);
}
