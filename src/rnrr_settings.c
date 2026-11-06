#include "rnrr_settings.h"

#include <stdio.h>
#include <string.h>

#include "host_paths.h"

/* Suavizada: Scale2x x2 + edge anti-aliasing + sharp bilinear (no jaggies).
 * Nitida: sharp bilinear only (even, crisp pixels at any output size). */
static const char *const kShader[RNRR_IMAGE_COUNT] = {
    "assets/shaders/rnrr-suavizada.glslp", "assets/shaders/rnrr-nitida.glslp",
    "assets/shaders/crt-soft.glslp", "assets/shaders/ntsc-svideo.glslp"};

const char *RnrrSettingsShaderPath(int image)
{
    return kShader[image >= 0 && image < RNRR_IMAGE_COUNT ? image : RNRR_IMAGE_SMOOTH];
}

int RnrrSettingsImageFromShader(const char *shader)
{
    /* The old "sharp"/plain-pixel choices both read as the default. */
    if (!shader) return RNRR_IMAGE_SMOOTH;
    if (strstr(shader, "rnrr-nitida")) return RNRR_IMAGE_SHARP;
    if (strstr(shader, "crt")) return RNRR_IMAGE_CRT;
    if (strstr(shader, "ntsc")) return RNRR_IMAGE_TV;
    return RNRR_IMAGE_SMOOTH;
}

int RnrrSettingsWriteModState(int aspect, int smooth)
{
    char path[1024], tmp[1100];
    if (!snesrecomp_exe_dir_path("mods/preloaded/state.toml", path, sizeof(path))) return 0;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    const char *value = aspect == RNRR_ASPECT_21_9 ? "21:9" : aspect == RNRR_ASPECT_FIT ? "fit" : "16:9";
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    int ok = fprintf(f,
        "format_version = 1\n"
        "\n[[package]]\nid = \"rnrr.display\"\nversion = \"1.0.0\"\n"
        "\n[[feature]]\npackage_id = \"rnrr.display\"\nid = \"overclock\"\nenabled = true\n"
        "\n[[feature]]\npackage_id = \"rnrr.display\"\nid = \"presentation-fps\"\nenabled = %s\n"
        "\n[[feature]]\npackage_id = \"rnrr.display\"\nid = \"widescreen\"\nenabled = %s\n"
        "\n[feature.values]\naspect = \"%s\"\n",
        smooth ? "true" : "false", aspect == RNRR_ASPECT_4_3 ? "false" : "true", value) > 0;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    return rename(tmp, path) == 0;
}
