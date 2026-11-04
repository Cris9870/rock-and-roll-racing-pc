/* Public builds carry no recompiled game code: the dispatch tables the
 * generated program would provide are empty, so every routine runs through
 * the interpreter. (The framework's own SETUP_HOST mode refuses to boot a game
 * at all; this build is meant to be played.) */
#include "cpu_state.h"

#if !defined(RNRR_PUBLIC_BUILD)
#error "rnrr_no_native.c is only compiled into RNRR_PUBLIC_BUILD builds"
#endif

const DispatchEntry g_dispatch_table[] = {
    { 0xFFFFFFFFu, { NULL, NULL, NULL, NULL }, 0 },
};
const unsigned g_dispatch_table_count = 0u;

const RamRoutineGuard g_ram_routine_guards[] = {
    { 0xFFFFFFFFu, 0u, 0u },
};
const unsigned g_ram_routine_guard_count =
    (unsigned)(sizeof(g_ram_routine_guards) / sizeof(g_ram_routine_guards[0]));
