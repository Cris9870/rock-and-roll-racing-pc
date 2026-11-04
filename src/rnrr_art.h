#ifndef RNRR_ART_H
#define RNRR_ART_H

#include <string>

/* Builds the launcher's artwork from the player's own ROM, in the background,
 * with nothing but this executable: it runs itself headless to capture the
 * title screen, the menu logo, a menu frame (for the marble palette) and a
 * race frame, pushes them through the "Suavizada" shader, and writes
 * logo.bmp, poster.bmp, race.bmp and marble.bmp into assets/launcher/.
 * Nothing ROM-derived ever ships; every player's copy makes its own. */
class RnrrArtJob {
public:
    /* Starts unless the art for this ROM (by SHA-256) is already there or a
     * previous attempt for it failed. Returns true when a job was started. */
    bool start(const std::string &exe_dir, const std::string &rom_path, const std::string &rom_sha);
    /* Advances the job without blocking. Returns true while it is running. */
    bool poll();
    /* Stops a running job (the player pressed PLAY: the game must not share
     * the mod state file with a capture run). */
    void cancel();
    bool running() const { return step_ > 0 && step_ < kDone; }
    bool finished_ok() const { return ok_; }
    float progress() const;

private:
    enum { kDone = 7 };
    bool spawn(const char *const *args, const char *const *env_pairs);
    bool finish();
    std::string exe_dir_, rom_, sha_, work_, out_;
    void *proc_ = nullptr;
    int step_ = 0;
    bool ok_ = false;
};

#endif
