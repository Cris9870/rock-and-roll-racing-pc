# Technical notes

How this port works and why: revision differences, the widescreen design, ROM patching and
native-code debugging. Build commands are in the [README](../README.md).

## Testing headless

There are no unit tests. Verification is done by running the game headless and inspecting dumps:

```sh
# A script skips the launcher; --config points at an ini with EnableAudio = 0. The game still
# opens its window: SNESRECOMP_HIDDEN_WINDOW=1 creates it hidden (OpenGL and shaders still run).
SNESRECOMP_HIDDEN_WINDOW=1 ./RockNRollRacingSNESRecomp.exe --config test.ini --no-launcher --script inputs.txt "<rom>"
#   script grammar: wait N | press b+right[+p2:b] N | dump <tag> | turbo on|off | reset | quit
#   SNESRECOMP_DUMP_DIR=dir -> each `dump` writes <tag>.fb.bmp (core 256 px field), .wram.bin, .vram.bin, .oam.bin
#   --framedump dir + SNESRECOMP_FRAMEDUMP_PIXELS=1 SNESRECOMP_FRAMEDUMP_START/END=N
#                           -> presented frames as frame_NNNNNN.bmp, at widescreen width (342 for 16:9, 448 for 21:9)
#   RNRR_NATIVE=1 / RNRR_INTERPRETER=1 force an engine; RNRR_NO_ROMPATCH=1 runs the game without this port's ROM patches
#   RNRR_TEST_WAV=dir RNRR_TEST_WAV_FRAMES=a,b,c -> dir/a<N>.wav: the last ~2 s of DSP output at
#                           those frames, also with EnableAudio = 0 (check music without a sound device)
# Launcher, hidden, to a screenshot (exe with no args):
RNRR_LAUNCHER_SHOT=out.bmp ./RockNRollRacingSNESRecomp.exe
#   RNRR_LAUNCHER_PICK=<rom> acts as a dropped file; RNRR_LAUNCHER_SHOT_WAIT_ART=1 waits for the art job
# Render a shader preset offline through the real GLSL pipeline:
RNRR_SHADER_TEST="assets/shaders/rnrr-suavizada.glslp|in.bmp|out.bmp|2520|1080" ./RockNRollRacingSNESRecomp.exe
```

The widescreen aspect comes from `mods/preloaded/state.toml` (feature `widescreen`, value `aspect`).
Native and interpreter runs legitimately differ in timing, so compare scenes, not whole frames.


**Execution model.** `src/main.c` (`rnrr_on_rom_loaded`) chooses the engine. Native (recompiled
`src/gen/*.c`) is the default. The interpreter is used if `engine.cfg` says `interpreter`, or if
`RNRR_INTERPRETER=1` / `RNRR_NATIVE` is set. Native mode is still LLE-scheduled: the
framework's interpreter drives the frame and "bounces" into compiled functions, and any code missing
from `src/gen` is interpreted. Coverage, and therefore how much runs natively, comes from
`recomp/coverage/*.json`; `tools/regen.sh` passes each file as a historical profile.

**ROM identity and revision differences.** `src/rnrr_revision.c` lists the accepted dumps (Europe
verified, USA unverified; a 512-byte copier header is stripped before hashing). `main.c`
(`rnrr_set_identity`) sets the host's expected SHA from the actual ROM before boot, and unknown ROMs
are refused. `rom_identity.txt` keeps the European SHA for `regen.sh`. The mod manifest
`mods/preloaded/packages/rnrr.display/1.0.0/manifest.toml` has one `[[target]]` per revision. Native
code is used only for the European ROM in a non-public build.

Compared with USA, the European bank-0 code is +$A0 bytes, the sprite-attribute WRAM is −$26
(`$0AE3/$0AE9/$0AFB/$0B01/$0B07`), and the NMI is at `$81AA`. Camera (`$18-$1F`) and map tables
(`$7E3500`, `$7F0000`, `$7F5DC0`, `$7F89C0`) are unchanged. The USA-only 30 FPS and MSU-1 code was
deleted. Save-state identity stays `"rnrr-stock"` (`src/game_rtl.c`) for compatibility.

**ROM patching happens in three places, and they must stay consistent.**
1. `tools/patch_rom.py` patches the image that `regen.sh` feeds to the code generator. It also
   applies the OPTIONS screen patch from `tools/options_menu.py` (see below).
2. `src/rnrr_rompatch.c` patches the same bytes in memory at load time, so the interpreter matches
   the generated C. It finds the sprite emitter's X cull tests by byte signature (so it works on
   both revisions), relocates them to free ROM at `$01:FF10/FF30/FF60` (EU call sites
   `$00:D445/D4AA/D6D2/D741`) and reports the park sites via `RnrrRomPatchParkSites`.
3. `tools/apply_overrides.py` rewrites constants in the generated C to read host globals from
   `src/rnrr_ws_cull.h`:
   - `g_rnrr_cull_left/right`: the cull bounds.
   - `g_rnrr_park_x`: the X where hidden sprites are parked. The game parks them at X=$180 and draws
     their parts at +16/+32; in a wide left margin those parts showed up as ghost cars.
   - Overclock cycle charges.

   The interpreter equivalents are ROM bytes rewritten at run time in `apply_cull_window()`
   (`src/rnrr_display.c`).

**Widescreen** (`src/rnrr_display.c`, hooks registered in `main.c`'s `SnesDesktopHostGame`):
- `RnrrPrepareFrame` picks the frame width. It is capped at `RNRR_SAFE_FRAME_WIDTH = 448` (exactly
  21:9): beyond 96 extra pixels per side, the 9-bit sprite X wraps, and beyond 104 the background map
  wraps.
- The margin backgrounds come from a tile cache plus `map_tile()`, which reads the track map from
  WRAM in BG space. Split-screen views offset from their camera must shift map rows by `off_y`.
- Each detected view group maps to a camera (`view_for_scroll`): an exact scroll match may share a
  camera (one view cut into bands), a partial (X-only) match may not. When both split-screen cameras
  shared an X, the lower view took the upper camera, re-based its tile store, and the upper margin
  showed stale tilemap columns for a few frames.
- Split-screen sprites: the game DMAs the upper view's OAM (`$7E1704`) in vblank and the lower
  view's (`$7E14E4`) at line 113 under forced blank. The engine's raster journal replays those OAM
  snapshots per line (`patches/README.md`), and motion smoothing keeps one OAM segment per band
  (`FrameRecord.seg`) so interpolated frames keep both views' sprites.
- Menus are pillarboxed to 4:3.
- The HUD is anchored to the screen edges with PPU elastic bands.
- Correct geometry requires `DisplayAspect = 4:3`: 7:6 SNES pixels. The launcher always writes it.

**Code generator fix.** `snesrecomp/recompiler/v2/codegen.py` `_emit_read` sets `cpu->open_bus` to
the instruction's last operand byte before direct, absolute and long reads. The game reads unmapped
`$5C00` and uses the open-bus value `$5C5C` as a VRAM address. Changes to `snesrecomp/` must be
saved into `patches/snesrecomp.patch` (applied automatically by `tools/apply_patches.sh` during
regen and configure), with a note in `patches/README.md`.

**Front end.**
- `src/rnrr_launcher.cpp`: a compact Spanish ImGui/SDL3 launcher, shown when the exe runs with no
  arguments. It writes `config.ini` (display, shader, audio), `mods/preloaded/state.toml`
  (widescreen aspect, motion smoothing) and `engine.cfg`, then calls `snesrecomp_desktop_main` with
  `--no-launcher <rom>`. The third-party recomp-ui launcher and its "Generate & rebuild" wizard were
  removed from the build. Dear ImGui (with SDL3/OpenGL3 backends) is vendored in
  `src/third_party/imgui`, and the Lato fonts are in `assets/fonts`. Only this port's shaders are
  staged (`assets/shaders`). Third-party licenses live in `licenses/`, and snesrecomp is PolyForm
  Noncommercial.
- Launcher UX (redesigned 2026-11-04): left column logo + poster + JUGAR (nav starts there) + Controles/Salir;
  right column grouped settings (IMAGEN, RENDIMIENTO, SONIDO). Each choice row shows a help line for the
  hovered/focused option, else the selected one. A save tip sits at the bottom; "Controles" opens a sheet
  with the full pad/keyboard/save-state table. ROM problems appear only when there is one, replacing JUGAR.
  `RNRR_LAUNCHER_SHOW_CONTROLS=1` opens the sheet, for screenshots.
- Input: the launcher installs a WASD + JIKL (SNES Y X B A) + Q/O (L/R) `keybinds.ini` when it is missing
  or still holds the framework's old defaults. Player 2 uses the arrows and the keypad. The save-state
  browser's pad chord Select+R (Xbox "Select (View)" + RB) is enabled (`state_menu_no_pad_chord = 0`); rewind is
  Select+R3. The browser's labels were patched to Spanish/Xbox terms in snesrecomp. Overlays ignore
  script input; verify pad gestures with `SNESRECOMP_OVERLAY_SELFTEST_PAD=<frame>`.
- ROM onboarding: with no valid ROM the launcher shows "Elegir archivo..." / drag-and-drop; the ROM is
  copied header-less to `rom/juego.sfc` (preferred over `rom.cfg`, which keeps the folder portable).
  Test hook: `RNRR_LAUNCHER_PICK=<rom>`.
- The launcher is themed with art made from the player's ROM on first run by `RnrrArtJob`
  (`src/rnrr_art.cpp`): it re-runs the exe headless (capture script, race framedump, offline shader
  renders) and writes `logo/poster/race/marble.bmp` plus `source.txt` (ROM SHA) to `assets/launcher/`,
  never committed or shipped. ~20 s; `RNRR_LAUNCHER_SHOT_WAIT_ART=1` waits for it, `RNRR_LAUNCHER_NO_ART=1`
  skips it. The launcher falls back to plain colours when the art is missing. Pad navigation works because chips are
  `InvisibleButton`s with a hand-drawn focus ring; keep decorative elements out of navigation.
- Image filters are GLSL presets in `assets/shaders/` (RetroArch legacy GLSL format, loaded by
  `snesrecomp/runner/src/desktop/glsl_shader.c`):
  - `rnrr-suavizada` (default): Scale2x ×2, then FXAA, then sharp-bilinear.
  - `rnrr-nitida`: sharp-bilinear only.
- The exe is linked `-mwindows`: no console window. Its icon is original artwork drawn by
  `tools/make_icon.py <fonts_dir> <out_dir>` into `assets/icon/rnrr.ico` (a badge with "R&R" and a
  checkered corner; sizes of 32 px and below show a single "R"). It is embedded through `src/rnrr.rc`, and
  every SDL window uses it via `SDL_HINT_WINDOWS_INTRESOURCE_ICON(_SMALL)="1"` in `main()`.
- `src/rnrr.manifest` (embedded by `rnrr.rc`) sets the UTF-8 active code page, so ANSI paths
  (`GetModuleFileNameA`, `fopen`) work in folders with accents or ñ. Do not remove it.

**PC settings in the game's OPTIONS screen** (garage → SET OPTIONS). The stock screen (bank `$01`,
`$BC17`-`$BEF3`) lists MUSIC / SOUND / LARRY / EXIT in a small panel between four car bubbles. It
becomes one Spanish list: OPCIONES with MUSICA, SONIDO, LARRY, FORMATO, PANTALLA, FILTRO,
MOVIMIENTO and SALIR, plus "CLAVE" / "JUGADOR 1/2" in the password panel. None of that code is in
`src/gen`, so the interpreter always runs it and the patch needs no regen in either engine.
- `tools/options_menu.py` is a small 65816 assembler holding the patch. It assembles 549 bytes of
  original code and text into free ROM at `$01:FA10` and lists the patched sites with the bytes
  each must hold first. `--write-inc` generates `src/rnrr_options_patch.inc`; `--check <rom>`
  applies the patch to a copy and disassembles it. `tools/patch_rom.py` imports it, so the regen
  image matches the running one.
- `src/rnrr_options.c` `RnrrOptionsPatchRom` applies it at load (after `RnrrRomPatchApply`). If any
  signature differs, it logs that and keeps the stock menu.
- Hooks into the stock code:
  - The text printer `$01:E0FA` is entered at `$E102` with our own string pointer in `$04`
    (`print_ptr`), so the shared string table at `$01:E228` is untouched.
  - The toggle chain at `$BD72` goes through `dispatch`. Rows 0-2 keep the stock code, rows 3-6 step a
    mailbox value, and row 7 exits.
  - `$BD6E` keeps Y (the direction) across the menu beep `$80:FB6E`, which clobbers it.
  - `$BDCD` (the value printer), `$BECB` (cursor X and Y), `$BEB6` (8 rows) and `$BC20` (start on
    SALIR) are redirected or rewritten.
  - The bubble cars are dropped. Their graphics loads (`$907C` ×4) are skipped, and in the screen's
    sprite table at `$01:A98F` the four cars get X=$80 with the 9th X bit set (X=384, off screen).
    The table's layout is `[count][count×4 OAM bytes][n][n high-table bytes]`. Cutting the count
    instead makes the game read the first car's bytes as the high table, which put the cursor
    at X=288, out of view.
  - MUSICA (`$BD7F`, `music_toggle`): the stock code stops the music when it goes off and leaves it
    stopped, even after leaving the screen. Turning it back on now queues "play" through
    `$80:F942` with X=0 (no song reload). The song restarts about 2.8 s later.
- WRAM mailbox at `$7F:FFF0`. Every earlier WRAM dump was zero from `$7F:B250` up.

  | Bytes | Meaning |
  |---|---|
  | `+0` | FORMATO: 4:3, 16:9, 21:9, AJUSTAR |
  | `+1` | PANTALLA: COMPLETA, VENTANA |
  | `+2` | FILTRO: SUAVIZADA, NITIDA, CRT, TV CLASICA |
  | `+3` | MOVIMIENTO: SUAVE, ORIGINAL |
  | `+4` | `$A5` when the menu changed a value |
  | `+5` | 1 while the screen is open |

- Protocol, in `RnrrOptionsAfterFrame` after every frame (the `after_run_frame` hook):
  1. If `+4` holds `$A5`, apply and save what changed, then clear it.
  2. Always write the live settings back into `+0..3`.

  Alt+Enter, resets, save-state loads and rewinds therefore never apply anything by accident.
  `on_reset` rewrites the mailbox too. A value that changes without `$A5` is logged once, as a
  safety net; long 1P and 2P runs logged nothing.
- How each setting is applied and saved:

  | Setting | Applied with | Saved to |
  |---|---|---|
  | FORMATO | `RnrrDisplaySetWidescreen` + `ChangeWindowScale(0)` | `mods/preloaded/state.toml` |
  | MOVIMIENTO | `RnrrDisplaySetPresentationFps` + clock reset + `snesrecomp_desktop_apply_video` (vsync) | `mods/preloaded/state.toml` |
  | FILTRO | `g_config.shader` + `snesrecomp_desktop_apply_video` (shader reload) | `config.ini`, via `snesrecomp_desktop_save_config` |
  | PANTALLA | `snesrecomp_desktop_set_fullscreen` | `config.ini`, via `snesrecomp_desktop_save_config` |

  `state.toml` is written by `RnrrSettingsWriteModState` (`src/rnrr_settings.c`), shared with the
  launcher, so both write the same file the same way.
- The wide panel: while `+5` is 1 and BG3 row 3 reads "OPCIONES", the host rebuilds BG1 rows 1-17
  from the password panel's rows 18-26. The granite texture hides the repeat. The game does not
  re-upload BG1 while the screen is open, and the check makes a stale flag harmless.

**Updates** (`src/rnrr_update.cpp`). At start the launcher asks
`api.github.com/repos/Cris9870/rock-and-roll-racing-pc/releases/latest` (WinHTTP, background thread)
and compares `tag_name` with `SNES_GAME_VERSION` (the `VERSION` file; "dev" builds never check).
Install: download the asset named `RockAndRollRacingPC-windows.zip`, verify the asset's `digest`
(sha256) when GitHub provides it, inflate every entry (stb_image's raw-deflate decoder; the input is
padded with 8 zero bytes because stb errors on a stream that ends flush with its buffer) into
`update-tmp/`, check each CRC-32, then rename the running exe to `.exe.old` (deleted on the next
start), move the new one in (rolled back if that fails) and move the rest over. Builds without
`RNRR_PUBLIC_BUILD` only link to the release page. Releases must keep the tag format `vX.Y.Z`, the
asset name and the single top folder in the zip. Test hooks: `RNRR_UPDATE_API=<url>`,
`RNRR_UPDATE_FAKE_VERSION=<x.y.z>`, `RNRR_LAUNCHER_UPDATE_TEST=1` (hidden window: check, install,
quit; `[update]` lines on stderr) and `RNRR_UPDATE_TEST_RELAUNCH=1` (with `RNRR_LAUNCHER_SHOT`).

**Debugging native-code bugs.** Bisect compiled functions with
`SNESRECOMP_LLE_INTERP_TARGET_FILE`, a file of hex PCs forced to the interpreter. List **both**
`$00xxxx` and `$80xxxx` mirrors, because matching is exact. The node list is in
`src/gen/program_manifest.json`. `SNESRECOMP_LLE_BOUNCE=0` disables bounces as a quick A/B test.
For 65816 disassembly, use `snesrecomp/recompiler/snes65816.py` `decode_insn` (LoROM offset =
`(bank&0x7f)*0x8000 + (addr&0x7fff)`).
