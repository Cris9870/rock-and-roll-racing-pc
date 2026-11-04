Local changes to the snesrecomp submodule, as `git diff` output against the pinned
commits. CMake configure and `tools/regen.sh` apply them automatically
(`tools/apply_patches.sh`; already-applied patches are skipped). After moving
a submodule pin, regenerate the file with `git -C <submodule> diff`.

snesrecomp.patch
- beam_frame_driver.c: pay beam debt before a slice and cap the slice at the
  next IRQ match (hero-select split timing).
- interp_bridge.c/.h: `g_interp_bridge_master_scale_pct` (CPU overclock for
  interpreted code).
- rom_image_verify.c/.h, mod_runtime.cpp, desktop/host_main.c/.h:
  `rom_digest_whole_file` verifies the ROM file as stored, header included.
- snes_savestate_menu.c/.h, desktop/host_main.c/.h: `state_menu_no_pad_chord`
  disables the Select+R controller chord for the save-state browser.
- snes/ppu.c: BG modes 4-6 use the per-pixel renderer; the fast one only
  covers 0-3 and 7 (fixes the equipment shop, mode 4).
- snes/dsp.c: a voice is silent on its key-on sample; it played one stale
  sample at the new gain, a click on every direct-gain note start.
- snes/dsp.c: the Gaussian sum clamps instead of wrapping at 16 bits; loud
  samples (the engine) flipped to the opposite peak, a click on wall hits.
- snes/dsp.c/.h: a voice cut off by key-on or sample end fades its last
  output (after volume) to zero over 32 samples instead of dropping it
  (voice-steal clicks). The fade only feeds the dry mix, never the echo
  buffer, and its state sits before `ram`, outside save states and digests.
- snes/dsp.c: a voice fading out (key-off release) keeps its pitch and sample
  until it is silent or keyed on again; the RNRR driver loads the next sound's
  pitch during the release and the explosion tail turned into a noise burst.

- recompiler/v2/codegen.py: compiled reads with direct, absolute or long
  addressing first put the operand's last byte on the open bus, as the CPU's
  own operand fetch would. Compiled code fetched no instruction bytes, so an
  unmapped read returned a stale value; RNRR's hero select reads $5C00 with
  DB=$80 and uses the open-bus $5C5C as a VRAM address (2P screen corrupted).
- snes_savestate_menu.c: labels in Spanish and in Xbox terms (an Xbox pad maps
  SNES buttons by position: the menu's SNES A/X/B are the pad's B/Y/A), and
  the keyboard hint matches this port's WASD + JIKL layout.
- snes/ppu.c (raster journal): OAM rewritten during the visible lines is
  replayed per line. The journal skipped OAMDATA as an upload port, so the
  field was drawn with the table as it ended; RNRR's 2-player split DMAs the
  upper view's sprites in vblank and the lower view's at line 113, and the
  upper half lost its cars. The first OAMDATA write on each visible line now
  snapshots the table; the render swaps the snapshots in at their lines and
  gives the guest its own table back afterwards. Fields that upload only in
  vblank record nothing.
