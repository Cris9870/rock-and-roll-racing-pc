// Launcher artwork from the player's ROM (see rnrr_art.h).
#include "rnrr_art.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

/* A race frame; turbo presents only some frames, so dump a window of them and
 * take the first one that was presented. */
const int kRaceFrame = 2860, kRaceFrameEnd = 2960;

const char kCaptureScript[] =
    "turbo on\nwait 840\ndump title\nwait 400\npress start 3\nwait 120\npress b 3\nwait 120\n"
    "dump logo\npress b 3\nwait 120\ndump h1\npress b 3\nwait 150\ndump h2\npress b 3\nwait 150\n"
    "dump h3\npress b 3\nwait 150\ndump h4\nquit\n";

std::string race_script()
{
    std::string s = "turbo on\nwait 300\n";
    for (int i = 0; i < 15; i++) s += (i % 2 == 0 ? "press start 3\nwait 150\n" : "press b 3\nwait 150\n");
    s += "press b+right 30\npress b 20\npress b+left 20\npress b 20\nwait 400\nquit\n";
    return s;
}

const char kChildIni[] = "[Sound]\nEnableAudio = 0\n[Graphics]\nFullscreen = 0\nDisplayAspect = 4:3\n";

std::string self_exe()
{
    wchar_t w[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, w, (DWORD)(sizeof(w) / sizeof(w[0])));
    if (!n) return std::string();
    char u[MAX_PATH * 4];
    int m = WideCharToMultiByte(CP_UTF8, 0, w, (int)n, u, (int)sizeof(u) - 1, nullptr, nullptr);
    return std::string(u, (size_t)(m > 0 ? m : 0));
}

bool write_text(const std::string &path, const std::string &text)
{
    SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "wb");
    if (!io) return false;
    bool ok = SDL_WriteIO(io, text.data(), text.size()) == text.size();
    return SDL_CloseIO(io) && ok;
}

std::string read_text(const std::string &path)
{
    size_t n = 0;
    void *d = SDL_LoadFile(path.c_str(), &n);
    if (!d) return std::string();
    std::string s((const char *)d, n);
    SDL_free(d);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// RGBA8 image in memory.
struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    uint8_t *at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
    const uint8_t *at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

bool load_bmp(const std::string &path, Img &out)
{
    SDL_Surface *s = SDL_LoadBMP(path.c_str());
    if (!s) return false;
    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return false;
    out.w = c->w;
    out.h = c->h;
    out.px.resize((size_t)c->w * c->h * 4);
    for (int y = 0; y < c->h; y++)
        std::memcpy(&out.px[(size_t)y * c->w * 4], (uint8_t *)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 4);
    SDL_DestroySurface(c);
    return true;
}

bool save_bmp(const std::string &path, const Img &img)
{
    SDL_Surface *s = SDL_CreateSurfaceFrom(img.w, img.h, SDL_PIXELFORMAT_RGBA32,
                                           (void *)img.px.data(), img.w * 4);
    if (!s) return false;
    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(s);
    if (!c) return false;
    bool ok = SDL_SaveBMP(c, path.c_str());
    SDL_DestroySurface(c);
    return ok;
}

Img crop(const Img &src, int x0, int y0, int x1, int y1)
{
    Img o;
    x0 = std::max(0, x0); y0 = std::max(0, y0);
    x1 = std::min(src.w, x1); y1 = std::min(src.h, y1);
    if (x1 <= x0 || y1 <= y0) return o;
    o.w = x1 - x0; o.h = y1 - y0;
    o.px.resize((size_t)o.w * o.h * 4);
    for (int y = 0; y < o.h; y++)
        std::memcpy(o.at(0, y), src.at(x0, y0 + y), (size_t)o.w * 4);
    return o;
}

// Title logo: black background keyed to alpha (outlines stay partly opaque),
// then trimmed to what is left.
bool make_logo(const Img &shaded, Img &out)
{
    Img band = crop(shaded, 0, shaded.h * 24 / 896, shaded.w, shaded.h * 330 / 896);
    if (!band.w) return false;
    int bx0 = band.w, by0 = band.h, bx1 = -1, by1 = -1;
    for (int y = 0; y < band.h; y++)
        for (int x = 0; x < band.w; x++) {
            uint8_t *p = band.at(x, y);
            int a = std::min(255, std::max({p[0], p[1], p[2]}) * 5);
            p[3] = (uint8_t)a;
            if (a > 40) { bx0 = std::min(bx0, x); by0 = std::min(by0, y); bx1 = std::max(bx1, x); by1 = std::max(by1, y); }
        }
    if (bx1 - bx0 < band.w / 3 || by1 - by0 < band.h / 4) return false;  // no logo there
    out = crop(band, bx0, by0, bx1 + 1, by1 + 1);
    return true;
}

bool colourful(const Img &img)
{
    size_t n = 0, c = 0;
    for (int y = 0; y < img.h; y += 4)
        for (int x = 0; x < img.w; x += 4) {
            const uint8_t *p = img.at(x, y);
            int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
            n++;
            if (mx - mn > 60) c++;
        }
    return n && c * 4 > n;  // a quarter of it saturated: an illustration, not a black screen
}

// Seamless marble tile at the game's pixel grain, coloured with the palette of
// the menus' marble border.
Img make_marble(const std::vector<std::array<uint8_t, 3>> &palette)
{
    const int size = 96;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    auto periodic = [&](int cells) {
        std::vector<float> grid((size_t)cells * cells), out((size_t)size * size);
        for (float &g : grid) g = uni(rng);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                float fx = x * (float)cells / size, fy = y * (float)cells / size;
                int x0 = (int)fx, y0 = (int)fy, x1 = (x0 + 1) % cells, y1 = (y0 + 1) % cells;
                float tx = fx - x0, ty = fy - y0;
                tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
                float a = grid[(size_t)y0 * cells + x0] * (1 - tx) + grid[(size_t)y0 * cells + x1] * tx;
                float b = grid[(size_t)y1 * cells + x0] * (1 - tx) + grid[(size_t)y1 * cells + x1] * tx;
                out[(size_t)y * size + x] = a * (1 - ty) + b * ty;
            }
        return out;
    };
    std::vector<float> fine((size_t)size * size);
    for (float &f : fine) f = uni(rng);
    std::vector<float> mid = periodic(48), low = periodic(8);
    std::vector<std::pair<float, int>> v((size_t)size * size);
    for (size_t i = 0; i < v.size(); i++) v[i] = {0.62f * fine[i] + 0.22f * mid[i] + 0.16f * low[i], (int)i};
    std::sort(v.begin(), v.end());
    std::vector<int> idx(v.size());
    for (size_t r = 0; r < v.size(); r++)
        idx[(size_t)v[r].second] = std::min((int)palette.size() - 1, (int)(r * palette.size() / v.size()));
    Img o;
    o.w = o.h = size * 3;
    o.px.resize((size_t)o.w * o.h * 4);
    for (int y = 0; y < o.h; y++)
        for (int x = 0; x < o.w; x++) {
            const auto &c = palette[(size_t)idx[(size_t)(y / 3) * size + x / 3]];
            uint8_t *p = o.at(x, y);
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
        }
    return o;
}

}  // namespace

bool RnrrArtJob::start(const std::string &exe_dir, const std::string &rom_path, const std::string &rom_sha)
{
    if (running()) return false;
    exe_dir_ = exe_dir;
    rom_ = rom_path;
    sha_ = rom_sha;
    out_ = exe_dir + "assets/launcher/";
    work_ = out_ + "work/";
    ok_ = false;
    if (read_text(out_ + "source.txt") == sha_ || read_text(out_ + "failed.txt") == sha_) return false;
    SDL_CreateDirectory(work_.c_str());
    SDL_CreateDirectory((work_ + "fd").c_str());  /* the frame dumper does not create it */
    if (!write_text(work_ + "child.ini", kChildIni) || !write_text(work_ + "capture.txt", kCaptureScript) ||
        !write_text(work_ + "race.txt", race_script()))
        return false;
    step_ = 1;
    return poll();
}

bool RnrrArtJob::spawn(const char *const *args, const char *const *env_pairs)
{
    SDL_Environment *env = SDL_CreateEnvironment(true);
    for (const char *const *e = env_pairs; e && e[0]; e += 2) SDL_SetEnvironmentVariable(env, e[0], e[1], true);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    proc_ = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    return proc_ != nullptr;
}

bool RnrrArtJob::poll()
{
    if (!running()) return false;
    if (proc_) {
        int code = 0;
        if (!SDL_WaitProcess((SDL_Process *)proc_, false, &code)) return true;  // still working
        SDL_DestroyProcess((SDL_Process *)proc_);
        proc_ = nullptr;
        step_++;
    }
    const std::string exe = self_exe();
    const std::string ini = work_ + "child.ini";
    bool started = false;
    switch (step_) {
    case 1: {
        const std::string script = work_ + "capture.txt";
        const char *args[] = {exe.c_str(), "--config", ini.c_str(), "--no-launcher", "--script",
                              script.c_str(), rom_.c_str(), nullptr};
        const char *env[] = {"SNESRECOMP_DUMP_DIR", work_.c_str(), "RNRR_INTERPRETER", "1", nullptr};
        started = spawn(args, env);
        break;
    }
    case 2: {
        const std::string script = work_ + "race.txt", fd = work_ + "fd";
        const std::string frame = std::to_string(kRaceFrame), frame_end = std::to_string(kRaceFrameEnd);
        const char *args[] = {exe.c_str(), "--config", ini.c_str(), "--no-launcher", "--script",
                              script.c_str(), "--framedump", fd.c_str(), rom_.c_str(), nullptr};
        const char *env[] = {"SNESRECOMP_FRAMEDUMP_PIXELS", "1", "SNESRECOMP_FRAMEDUMP_START", frame.c_str(),
                             "SNESRECOMP_FRAMEDUMP_END", frame_end.c_str(), "RNRR_INTERPRETER", "1",
                             "SNESRECOMP_DUMP_DIR", work_.c_str(), nullptr};
        started = spawn(args, env);
        break;
    }
    case 3: case 4: case 5: {
        // The three images, through the port's own Suavizada shader at HD size.
        std::string src, dst;
        int w = 1194, h = 896;
        if (step_ == 3) { src = work_ + "logo.fb.bmp"; dst = work_ + "logo_hd.bmp"; }
        if (step_ == 4) { src = work_ + "title.fb.bmp"; dst = work_ + "poster_hd.bmp"; }
        if (step_ == 5) {
            for (int f = kRaceFrame; f <= kRaceFrameEnd && src.empty(); f++) {
                char name[64];
                std::snprintf(name, sizeof(name), "fd/frame_%06d.bmp", f);
                if (SDL_GetPathInfo((work_ + name).c_str(), nullptr)) src = work_ + name;
            }
            dst = work_ + "race_hd.bmp";
            Img probe;
            if (load_bmp(src, probe)) { w = probe.w * 7 / 2; h = 672; }  // 7:6 pixels at 3x
        }
        const std::string spec = "assets/shaders/rnrr-suavizada.glslp|" + src + "|" + dst + "|" +
                                 std::to_string(w) + "|" + std::to_string(h);
        const char *args[] = {exe.c_str(), nullptr};
        const char *env[] = {"RNRR_SHADER_TEST", spec.c_str(), nullptr};
        started = SDL_GetPathInfo(src.c_str(), nullptr) && spawn(args, env);
        if (!started) { step_++; return poll(); }  // missing input: skip this image
        break;
    }
    case 6:
        ok_ = finish();
        write_text(out_ + (ok_ ? "source.txt" : "failed.txt"), sha_);
        step_ = kDone;
        return false;
    default:
        return false;
    }
    if (!started) {
        write_text(out_ + "failed.txt", sha_);
        step_ = kDone;
        return false;
    }
    return true;
}

bool RnrrArtJob::finish()
{
    int made = 0;
    Img img, out;
    if (load_bmp(work_ + "logo_hd.bmp", img) && make_logo(img, out) && save_bmp(out_ + "logo.bmp", out)) made++;
    if (load_bmp(work_ + "poster_hd.bmp", img) && colourful(img) && save_bmp(out_ + "poster.bmp", img)) made++;
    if (load_bmp(work_ + "race_hd.bmp", img) && img.h >= 672) {
        out = crop(img, 0, 200, img.w, img.h);
        if (save_bmp(out_ + "race.bmp", out)) made++;
    }
    // Marble palette: the reddest of the menu frames' left border.
    float best = -1;
    std::vector<std::array<uint8_t, 3>> palette;
    for (const char *name : {"h1", "h2", "h3", "h4"}) {
        Img m;
        if (!load_bmp(work_ + name + ".fb.bmp", m) || m.w < 8 || m.h < 190) continue;
        double r = 0, gb = 0;
        std::set<std::array<uint8_t, 3>> cols;
        for (int y = 0; y < 190; y++)
            for (int x = 0; x < 8; x++) {
                const uint8_t *p = m.at(x, y);
                r += p[0];
                gb += (p[1] + p[2]) / 2.0;
                if (p[0] > 40 && p[0] > 1.8 * std::max({(int)p[1], (int)p[2], 1})) cols.insert({p[0], p[1], p[2]});
            }
        float score = (float)((r - gb) / (8 * 190));
        if (score > best && cols.size() >= 3) { best = score; palette.assign(cols.begin(), cols.end()); }
    }
    if (best >= 40) {
        std::sort(palette.begin(), palette.end(),
                  [](const auto &a, const auto &b) { return a[0] + a[1] + a[2] < b[0] + b[1] + b[2]; });
        if (save_bmp(out_ + "marble.bmp", make_marble(palette))) made++;
    }
    // The work files hold raw dumps of the player's ROM session; drop them.
    SDL_EnumerateDirectory(work_.c_str(), [](void *ud, const char *dir, const char *name) -> SDL_EnumerationResult {
        (void)ud;
        std::string p = std::string(dir) + name;
        SDL_PathInfo info;
        if (SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            SDL_EnumerateDirectory((p + "/").c_str(), [](void *, const char *d, const char *n) -> SDL_EnumerationResult {
                SDL_RemovePath((std::string(d) + n).c_str());
                return SDL_ENUM_CONTINUE;
            }, nullptr);
        }
        SDL_RemovePath(p.c_str());
        return SDL_ENUM_CONTINUE;
    }, nullptr);
    SDL_RemovePath(work_.c_str());
    return made > 0;
}

void RnrrArtJob::cancel()
{
    if (proc_) {
        SDL_KillProcess((SDL_Process *)proc_, true);
        SDL_WaitProcess((SDL_Process *)proc_, true, nullptr);
        SDL_DestroyProcess((SDL_Process *)proc_);
        proc_ = nullptr;
    }
    step_ = 0;
}

float RnrrArtJob::progress() const
{
    static const float kAt[] = {0.0f, 0.02f, 0.40f, 0.85f, 0.90f, 0.95f, 0.98f, 1.0f};
    return step_ >= 0 && step_ <= kDone ? kAt[step_] : 0.0f;
}
