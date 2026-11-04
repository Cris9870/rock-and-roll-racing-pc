#pragma once
// In-place updates of the portable folder from GitHub Releases.
//
// check() asks the GitHub API, in the background, for the latest release and
// compares its tag with this build's version (SNES_GAME_VERSION). install()
// downloads the release zip, checks it against the SHA-256 GitHub publishes
// for the asset, unpacks every file into a staging folder and only then swaps
// them in. The running exe is renamed aside first (Windows allows renaming a
// running image); the leftover is deleted on the next start.
//
// Nothing the player owns ships in the zip -- rom/, config.ini, keybinds.ini,
// engine.cfg, saves/, assets/launcher/ and mods/preloaded/state.toml -- so an
// update never touches them.
//
// Test hooks: RNRR_UPDATE_API=<url> replaces the GitHub API endpoint, and
// RNRR_UPDATE_FAKE_VERSION=<x.y.z> pretends this build is that version.

#include <memory>
#include <string>

struct RnrrUpdateShared;

class RnrrUpdater {
public:
    enum State { kIdle, kChecking, kUpToDate, kAvailable, kDownloading, kInstalling, kInstalled, kFailed };

    RnrrUpdater();
    ~RnrrUpdater();

    void check();      // no-op for development builds
    void install();    // only after kAvailable (or kFailed, to retry)

    State state() const;
    float progress() const;          // 0..1 while downloading
    std::string latest() const;      // "1.0.2"
    std::string page_url() const;    // the release page on GitHub
    std::string error() const;       // why kFailed, in Spanish

    static bool can_install();       // public builds replace themselves; others only link to GitHub
    static void cleanup_old();       // delete the exe an earlier update left aside
    static bool relaunch();          // start the (new) exe, without arguments

private:
    std::shared_ptr<RnrrUpdateShared> sh_;
};
