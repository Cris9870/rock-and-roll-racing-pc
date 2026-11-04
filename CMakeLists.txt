cmake_minimum_required(VERSION 3.20)
project(RockNRollRacingSNESRecomp C CXX)

set(CMAKE_C_STANDARD 11)

if(CMAKE_C_FLAGS MATCHES "-flto" AND CMAKE_C_COMPILER_AR AND CMAKE_C_COMPILER_RANLIB)
    set(CMAKE_AR "${CMAKE_C_COMPILER_AR}")
    set(CMAKE_RANLIB "${CMAKE_C_COMPILER_RANLIB}")
endif()

# Release pin for lobby matching. Release builds stamp the VERSION file so two
# peers can prove they are the same build; everything else stamps "dev".
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _GAME_VER_RAW)
string(STRIP "${_GAME_VER_RAW}" GAME_RELEASE_VERSION)
set(SNESRECOMP_BUILD_VERSION "" CACHE STRING
    "Lobby release pin (empty = Release uses VERSION, else dev)")
if(SNESRECOMP_BUILD_VERSION STREQUAL "")
    get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_is_multi)
        set(SNESRECOMP_BUILD_VERSION "$<IF:$<CONFIG:Release>,${GAME_RELEASE_VERSION},dev>")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(SNESRECOMP_BUILD_VERSION "${GAME_RELEASE_VERSION}")
    else()
        set(SNESRECOMP_BUILD_VERSION "dev")
    endif()
endif()

set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")
# The snesrecomp submodule is a pinned upstream checkout; our changes to it
# live in patches/ and are applied here, so every build sees the same tree.
find_package(Git QUIET)
foreach(_rnrr_sub snesrecomp)
    set(_rnrr_patch "${CMAKE_CURRENT_SOURCE_DIR}/patches/${_rnrr_sub}.patch")
    set(_rnrr_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_rnrr_sub}")
    if(NOT EXISTS "${_rnrr_patch}" OR NOT EXISTS "${_rnrr_dir}/.git")
        continue()
    endif()
    if(NOT GIT_FOUND)
        message(FATAL_ERROR "git is required to apply patches/${_rnrr_sub}.patch")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_rnrr_dir}" apply --reverse --check "${_rnrr_patch}"
                    RESULT_VARIABLE _rnrr_applied OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rnrr_applied EQUAL 0)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_rnrr_dir}" apply "${_rnrr_patch}"
                        RESULT_VARIABLE _rnrr_rc ERROR_VARIABLE _rnrr_err)
        if(NOT _rnrr_rc EQUAL 0)
            message(FATAL_ERROR "patches/${_rnrr_sub}.patch does not apply to ${_rnrr_sub}:\n${_rnrr_err}")
        endif()
        message(STATUS "applied patches/${_rnrr_sub}.patch")
    endif()
endforeach()

if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
    message(FATAL_ERROR
        "snesrecomp submodule missing at ${SNESRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive")
endif()
# Mod packages are part of every project, not an option. The launcher's Mods
# page, the .snesmod loader, the trusted-plugin registry and netplay's mod-set
# gate all hang off this one switch; a build without it cannot exchange mods
# with a peer that has them, and the framework says so at every lobby init.
# Must precede the include: runner.cmake reads it to add the loader sources.
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL
    "Mod package loader + trusted plugins (required for every title)" FORCE)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

add_executable(RockNRollRacingSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    src/main.c
    src/host_contract.c
    src/game_rtl.c
    src/rnrr_display.c
    src/rnrr_mods.c
    src/rnrr_rompatch.c
    src/gen_stubs.c
    src/rnrr_launcher.cpp
    src/rnrr_revision.c
    src/rnrr_art.cpp
    src/rnrr_update.cpp
    src/rnrr_shader_test.c
)

# ROM identity. rom_identity.txt is the one place the digests live: the build
# turns it into snesrecomp_rom_identity.h (SNESRECOMP_ROM_EXPECTED_SHA256 and
# friends) for src/main.c, and tools/regen.sh, scripts/package_release.sh and
# the release workflow read the same file. A revision bump is one edit there.
snesrecomp_rom_identity(RockNRollRacingSNESRecomp ${CMAKE_SOURCE_DIR}/rom_identity.txt)

# Generated recompiler output. Not committed: it is derived from a copyrighted
# ROM, so every developer regenerates it locally from their own copy -- and a
# release does not ship it either. With -DSNESRECOMP_SETUP_HOST=ON and an empty
# src/gen/, this builds the SETUP HOST instead: the same executable minus the
# recompiled code, whose only path is the launcher's Generate & rebuild wizard.
# That is what release zips contain; the player's machine produces the
# playable binary from their own ROM. Without the option an empty src/gen/ is
# still a hard error (the framework owns the message).
# RNRR_PUBLIC_BUILD: the distributable executable. It carries nothing derived
# from a ROM -- no generated code (src/gen is ignored even if present) -- and
# runs every revision through the interpreter. Without it, src/gen (made by
# tools/regen.sh from the European dump) is compiled in for the native engine.
option(RNRR_PUBLIC_BUILD "Interpreter-only build with no ROM-derived code" OFF)
if(RNRR_PUBLIC_BUILD)
    target_sources(RockNRollRacingSNESRecomp PRIVATE src/rnrr_no_native.c)
    target_compile_definitions(RockNRollRacingSNESRecomp PRIVATE RNRR_PUBLIC_BUILD=1)
    message(STATUS "RockNRollRacingSNESRecomp: PUBLIC build (interpreter only, no ROM-derived code)")
else()
    snesrecomp_target_generated_code(RockNRollRacingSNESRecomp ${CMAKE_SOURCE_DIR}/src/gen)
endif()

# The mod catalog. This names WHERE the packages are; the framework owns the
# layout they are staged into beside the executable, and stages them on every
# build OF THE EXECUTABLE -- including the launcher's own Generate & rebuild on
# a player's machine, which builds only this target and would skip a sibling
# ALL target. mods/preloaded/packages/ holds reviewed, default-disabled
# packages; players install .snesmod archives through the launcher.
# snesrecomp/docs/MOD_PACKAGES.md.
#
# It used to be `snesrecomp_target_stage_dir(... ${CMAKE_SOURCE_DIR}/mods mods)`
# -- the destination spelled per title, which is how three ports in the same
# workspace ended up shipping an empty Mods page and nothing said so. Declaring
# it is now also enforced: a project with packages that never calls this fails
# to configure.
snesrecomp_target_mod_catalog(RockNRollRacingSNESRecomp ${CMAKE_SOURCE_DIR}/mods/preloaded)
# Any other directory the game reads exe-relative (a translations/ table, ...)
# is staged with snesrecomp_target_stage_dir, never with a separate ALL target.

target_include_directories(RockNRollRacingSNESRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    # common_rtl.c includes "config.h": the desktop key-map / Config contract
    # the framework host supplies (snesrecomp_target_desktop_host below).
    ${SNESRECOMP_ROOT}/runner/src/desktop
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/recomp
)
target_link_libraries(RockNRollRacingSNESRecomp PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES})
target_compile_definitions(RockNRollRacingSNESRecomp PRIVATE
    SNES_GAME_VERSION="${SNESRECOMP_BUILD_VERSION}"
    SNES_GAME_TITLE="Rock N' Roll Racing"
)
snesrecomp_target_sdl(RockNRollRacingSNESRecomp)

# The desktop host: launcher flow, ROM resolution, config.ini + [KeyMap],
# window + SDL/OpenGL presenters, audio, gamepads, the save-state browser and
# rewind filmstrip, the pacing clock, crash handlers and the post-mortem
# report. src/main.c is the shim that names this title and supplies its
# hooks; the host is the framework's and improves on a submodule pull.
# Add TIER2 once this port reads the interpreter-gap manifest after each run.
snesrecomp_target_desktop_host(RockNRollRacingSNESRecomp)
# Only this port's own image filters and the launcher's fonts are staged; the
# framework's shared preset collection is not used.
add_custom_command(TARGET RockNRollRacingSNESRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/shaders" "$<TARGET_FILE_DIR:RockNRollRacingSNESRecomp>/assets/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/fonts" "$<TARGET_FILE_DIR:RockNRollRacingSNESRecomp>/assets/fonts"
    VERBATIM)
# rom.cfg (the ROM the wizard set this project up from; git-ignored) is
# staged beside the executable when none is there yet, so the first launch
# opens on that dump rather than asking for one.
snesrecomp_target_rom_cache(RockNRollRacingSNESRecomp)

# A windowed app: no console window behind the launcher or the game, with the
# port's own icon (tools/make_icon.py draws assets/icon/rnrr.ico).
if(WIN32)
    target_link_options(RockNRollRacingSNESRecomp PRIVATE -mwindows)
    target_link_libraries(RockNRollRacingSNESRecomp PRIVATE winhttp)
    enable_language(RC)
    target_sources(RockNRollRacingSNESRecomp PRIVATE src/rnrr.rc)
    set_source_files_properties(src/rnrr.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/rnrr.manifest;${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/rnrr.ico")
endif()
# The launcher (src/rnrr_launcher.cpp) is built on Dear ImGui (MIT), vendored
# in src/third_party/imgui with its SDL3 and OpenGL3 backends.
set(RNRR_IMGUI "${CMAKE_SOURCE_DIR}/src/third_party/imgui")
target_sources(RockNRollRacingSNESRecomp PRIVATE
    ${RNRR_IMGUI}/imgui.cpp
    ${RNRR_IMGUI}/imgui_draw.cpp
    ${RNRR_IMGUI}/imgui_tables.cpp
    ${RNRR_IMGUI}/imgui_widgets.cpp
    ${RNRR_IMGUI}/backends/imgui_impl_sdl3.cpp
    ${RNRR_IMGUI}/backends/imgui_impl_opengl3.cpp)
target_include_directories(RockNRollRacingSNESRecomp PRIVATE ${RNRR_IMGUI} ${RNRR_IMGUI}/backends)
find_package(OpenGL REQUIRED)
target_link_libraries(RockNRollRacingSNESRecomp PRIVATE OpenGL::GL)

# Netplay is not built into this target.
# No multitap: two seats, one controller per port.
