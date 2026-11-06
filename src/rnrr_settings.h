/* Settings shared by the launcher and the in-game OPTIONS screen: the image
 * filter presets and the mod state file (widescreen aspect, motion smoothing).
 * Both write the same files the same way. */
#ifndef RNRR_SETTINGS_H
#define RNRR_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* In the order the launcher and the menu list them. */
enum { RNRR_ASPECT_4_3, RNRR_ASPECT_16_9, RNRR_ASPECT_21_9, RNRR_ASPECT_FIT, RNRR_ASPECT_COUNT };
enum { RNRR_IMAGE_SMOOTH, RNRR_IMAGE_SHARP, RNRR_IMAGE_CRT, RNRR_IMAGE_TV, RNRR_IMAGE_COUNT };

/* config.ini [Graphics] Shader for a preset, relative to the exe folder. */
const char *RnrrSettingsShaderPath(int image);
/* The preset a Shader value names; anything else reads as the default. */
int RnrrSettingsImageFromShader(const char *shader);
/* Rewrite mods/preloaded/state.toml: widescreen (off for 4:3) and its aspect,
 * presentation-fps (smooth), overclock on. Returns nonzero on success. */
int RnrrSettingsWriteModState(int aspect, int smooth);

#ifdef __cplusplus
}
#endif

#endif
