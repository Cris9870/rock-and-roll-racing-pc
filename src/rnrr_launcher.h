#ifndef RNRR_LAUNCHER_H
#define RNRR_LAUNCHER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shows the compact launcher. Returns 0 to quit, 1 to play (rom_out holds the
 * verified ROM path; settings are saved). */
int RnrrLauncherRun(const char *default_ini, const char *expected_sha,
                    char *rom_out, size_t rom_cap);

#ifdef __cplusplus
}
#endif

#endif
