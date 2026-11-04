// Rock & Roll Racing — compact Spanish launcher.
//
// One screen: what this edition improves, five settings, the controllers that
// are connected, and a PLAY button. It writes the same files the game already
// reads (config.ini and mods/preloaded/state.toml beside the executable), then
// returns so main() can boot the game with --no-launcher.

#include "rnrr_launcher.h"
#include "rnrr_art.h"
#include "rnrr_revision.h"
#include "rnrr_update.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // SetFocusID: start navigation on PLAY
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

extern "C" {
#include "host_paths.h"
#include "sha256.h"
}

namespace {

// ---------------------------------------------------------------- settings --

enum { ASPECT_4_3, ASPECT_16_9, ASPECT_21_9, ASPECT_FIT };
enum { IMAGE_SMOOTH, IMAGE_SHARP, IMAGE_CRT, IMAGE_TV };

/* Suavizada: Scale2x x2 + edge anti-aliasing + sharp bilinear (no jaggies).
 * Nitida: sharp bilinear only (even, crisp pixels at any output size). */
const char *kImageShader[] = {
    "assets/shaders/rnrr-suavizada.glslp", "assets/shaders/rnrr-nitida.glslp",
    "assets/shaders/crt-soft.glslp", "assets/shaders/ntsc-svideo.glslp"};

struct Settings {
    int aspect = ASPECT_16_9;
    bool fullscreen = true;
    int image = IMAGE_SMOOTH;
    bool smooth = true;
    int volume = 100;
    int engine = 0;  // 0 = native (recompiled), 1 = interpreter
};

std::string exe_path(const char *leaf)
{
    char buf[1024];
    if (!snesrecomp_exe_dir_path(leaf, buf, sizeof(buf))) return leaf;
    return buf;
}

bool read_file(const std::string &path, std::string &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool write_file(const std::string &path, const std::string &text)
{
    const std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) { std::remove(tmp.c_str()); return false; }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

// Value of `key` inside [section] of an INI text, or "" if absent.
std::string ini_get(const std::string &text, const char *section, const char *key, bool *found)
{
    std::string cur;
    *found = false;
    for (const std::string &raw : split_lines(text)) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') { cur = line; continue; }
        if (cur != std::string("[") + section + "]") continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (SDL_strcasecmp(trim(line.substr(0, eq)).c_str(), key) == 0) {
            *found = true;
            return trim(line.substr(eq + 1));
        }
    }
    return std::string();
}

// Replace (or add) `key = value` inside [section], keeping everything else.
std::string ini_set(const std::string &text, const char *section, const char *key, const std::string &value)
{
    std::vector<std::string> lines = split_lines(text);
    const std::string header = std::string("[") + section + "]";
    std::string cur;
    int header_at = -1, last_in_section = -1;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = trim(lines[i]);
        if (!line.empty() && line[0] == '[') {
            cur = line;
            if (cur == header) header_at = last_in_section = (int)i;
            continue;
        }
        if (cur != header) continue;
        if (!line.empty()) last_in_section = (int)i;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos && SDL_strcasecmp(trim(line.substr(0, eq)).c_str(), key) == 0) {
            lines[i] = std::string(key) + " = " + value;
            goto join;
        }
    }
    if (header_at < 0) {
        lines.push_back("");
        lines.push_back(header);
        lines.push_back(std::string(key) + " = " + value);
    } else {
        lines.insert(lines.begin() + last_in_section + 1, std::string(key) + " = " + value);
    }
join:
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) {
        out += lines[i];
        if (i + 1 < lines.size()) out += "\n";
    }
    if (out.empty() || out.back() != '\n') out += "\n";
    return out;
}

Settings load_settings(const char *default_ini)
{
    Settings s;
    std::string ini;
    if (!read_file(exe_path("config.ini"), ini)) ini = default_ini;
    bool found;
    std::string v = ini_get(ini, "Graphics", "Fullscreen", &found);
    if (found) s.fullscreen = std::atoi(v.c_str()) != 0;
    v = ini_get(ini, "Graphics", "Shader", &found);
    if (found) {
        /* The old "sharp"/plain-pixel choices both move to the new default. */
        s.image = IMAGE_SMOOTH;
        if (v.find("rnrr-nitida") != std::string::npos) s.image = IMAGE_SHARP;
        else if (v.find("crt") != std::string::npos) s.image = IMAGE_CRT;
        else if (v.find("ntsc") != std::string::npos) s.image = IMAGE_TV;
    }
    v = ini_get(ini, "Sound", "Volume", &found);
    if (found) s.volume = SDL_clamp(std::atoi(v.c_str()), 0, 100);
    v = ini_get(ini, "Sound", "EnableAudio", &found);
    if (found && (v == "0" || SDL_strcasecmp(v.c_str(), "false") == 0)) s.volume = 0;

    std::string toml;
    if (read_file(exe_path("mods/preloaded/state.toml"), toml)) {
        std::string feature;
        bool in_feature = false;
        for (const std::string &raw : split_lines(toml)) {
            std::string line = trim(raw);
            if (line == "[[feature]]") { in_feature = true; feature.clear(); continue; }
            if (line.rfind("[[", 0) == 0) { in_feature = false; continue; }
            if (!in_feature) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
            if (val.size() >= 2 && val.front() == '"') val = val.substr(1, val.size() - 2);
            if (key == "id") feature = val;
            else if (key == "enabled" && feature == "widescreen" && val == "false") s.aspect = ASPECT_4_3;
            else if (key == "enabled" && feature == "presentation-fps") s.smooth = val == "true";
            else if (key == "aspect" && feature == "widescreen" && s.aspect != ASPECT_4_3)
                s.aspect = val == "21:9" ? ASPECT_21_9 : val == "fit" ? ASPECT_FIT : ASPECT_16_9;
        }
    }
    std::string engine;
    if (read_file(exe_path("engine.cfg"), engine) && trim(engine) == "interpreter") s.engine = 1;
    return s;
}

/* Keyboard play: WASD to steer, the SNES face-button diamond on J I K L
 * (Y X B A), shoulders on Q and O. Player 2 uses the arrows and the keypad. */
const char kKeybinds[] = R"(# Controller Keybinds
# Edit values to customize. Restart the game to apply.
# Use SDL key names. Common: A B C ... Z, 0-9, F1-F12, Up Down Left Right,
# Return, Tab, Space, Left Shift, Right Shift, Left Ctrl, Right Ctrl,
# Backspace, Escape, Keypad 0-9, Keypad Enter. "None" leaves a button unbound.
#
# Rock & Roll Racing layout: WASD steer, J I K L are the SNES Y X B A
# diamond, Q and O are L and R, Enter is Start, Right Shift is Select.
# Player 2 is only read when its input source is the keyboard.

[player1]
a       = L
b       = K
x       = I
y       = J
l       = Q
r       = O
start   = Return
select  = Right Shift
up      = W
down    = S
left    = A
right   = D
l2      = None
r2      = None
l3      = None
r3      = None

[player2]
a       = Keypad 6
b       = Keypad 2
x       = Keypad 8
y       = Keypad 4
l       = Keypad 7
r       = Keypad 9
start   = Keypad Enter
select  = Keypad 0
up      = Up
down    = Down
left    = Left
right   = Right
l2      = None
r2      = None
l3      = None
r3      = None
)";

/* Install the layout above when there is no keybinds.ini yet, or when the
 * file still holds the framework's old defaults (Z/X/A/S on the arrows); a
 * layout the player edited is left alone. */
void install_keybinds()
{
    const std::string path = exe_path("keybinds.ini");
    std::string cur;
    if (read_file(path, cur)) {
        bool found;
        bool old_default = ini_get(cur, "player1", "a", &found) == "X" &&
                           ini_get(cur, "player1", "b", &found) == "Z" &&
                           ini_get(cur, "player1", "up", &found) == "Up";
        if (!old_default) return;
    }
    write_file(path, kKeybinds);
}

bool save_settings(const Settings &s, const char *default_ini)
{
    install_keybinds();
    std::string ini;
    if (!read_file(exe_path("config.ini"), ini)) ini = default_ini;
    ini = ini_set(ini, "Graphics", "Fullscreen", s.fullscreen ? "1" : "0");
    ini = ini_set(ini, "Graphics", "Shader", kImageShader[s.image]);
    ini = ini_set(ini, "Graphics", "LinearFiltering", "0");
    /* SNES pixels are 7:6 on a TV; 8:7/1:1 squeeze the picture and make the
     * widescreen field wider than the game can fill. */
    ini = ini_set(ini, "Graphics", "DisplayAspect", "4:3");
    ini = ini_set(ini, "Graphics", "IgnoreAspectRatio", "0");
    ini = ini_set(ini, "Sound", "EnableAudio", s.volume > 0 ? "1" : "0");
    ini = ini_set(ini, "Sound", "Volume", std::to_string(s.volume > 0 ? s.volume : 100));
    bool ok = write_file(exe_path("config.ini"), ini);
    ok = write_file(exe_path("engine.cfg"), s.engine ? "interpreter\n" : "native\n") && ok;

    const char *aspect = s.aspect == ASPECT_21_9 ? "21:9" : s.aspect == ASPECT_FIT ? "fit" : "16:9";
    std::string toml =
        "format_version = 1\n"
        "\n[[package]]\nid = \"rnrr.display\"\nversion = \"1.0.0\"\n"
        "\n[[feature]]\npackage_id = \"rnrr.display\"\nid = \"overclock\"\nenabled = true\n"
        "\n[[feature]]\npackage_id = \"rnrr.display\"\nid = \"presentation-fps\"\nenabled = ";
    toml += s.smooth ? "true\n" : "false\n";
    toml += "\n[[feature]]\npackage_id = \"rnrr.display\"\nid = \"widescreen\"\nenabled = ";
    toml += s.aspect == ASPECT_4_3 ? "false\n" : "true\n";
    toml += "\n[feature.values]\naspect = \"";
    toml += aspect;
    toml += "\"\n";
    return write_file(exe_path("mods/preloaded/state.toml"), toml) && ok;
}

// --------------------------------------------------------------------- ROM --

enum { ROM_MISSING, ROM_OK, ROM_WRONG };

struct RomInfo {
    int state = ROM_MISSING;
    RnrrRevision rev = RNRR_REV_UNKNOWN;
    std::string sha;
};

RomInfo check_rom(const std::string &path)
{
    RomInfo r;
    std::string data;
    if (path.empty() || !read_file(path, data)) return r;
    char sha[65];
    r.rev = RnrrIdentifyRom((const uint8_t *)data.data(), data.size(), sha);
    r.sha = sha;
    r.state = r.rev == RNRR_REV_UNKNOWN ? ROM_WRONG : ROM_OK;
    return r;
}

bool is_absolute(const std::string &p)
{
    return p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
}

std::string load_rom_path()
{
    /* The launcher's own copy wins: rom.cfg is also rewritten by the game with
     * an absolute path, which would break once the folder is moved. */
    const std::string own = exe_path("rom/juego.sfc");
    if (SDL_GetPathInfo(own.c_str(), nullptr)) return own;
    std::string cfg;
    if (!read_file(exe_path("rom.cfg"), cfg)) return std::string();
    std::vector<std::string> lines = split_lines(cfg);
    std::string p = lines.empty() ? std::string() : trim(lines[0]);
    return p.empty() || is_absolute(p) ? p : exe_path(p.c_str());
}

/* Keeps a header-less copy of a supported ROM as rom/juego.sfc beside the
 * exe and points rom.cfg at it with a relative path, so the whole folder can
 * be moved or copied to another PC and still finds its game. */
bool adopt_rom(const std::string &src, std::string &stored, RomInfo &info)
{
    std::string data;
    info = check_rom(src);
    if (info.state != ROM_OK || !read_file(src, data)) return false;
    if (data.size() % 1024 == 512) data.erase(0, 512);
    SDL_CreateDirectory(exe_path("rom").c_str());
    stored = exe_path("rom/juego.sfc");
    if (!write_file(stored, data)) return false;
    return write_file(exe_path("rom.cfg"), "rom/juego.sfc\n");
}

SDL_Mutex *g_pick_lock;
std::string g_picked;
bool g_pick_done;

void SDLCALL on_rom_picked(void *, const char *const *files, int)
{
    SDL_LockMutex(g_pick_lock);
    g_picked = files && files[0] ? files[0] : "";
    g_pick_done = true;
    SDL_UnlockMutex(g_pick_lock);
}

// ------------------------------------------------------------------ visuals --
//
// Themed after the game: the title logo and racer illustration, a race scene
// behind everything, red-marble frames like the menus, grey metal buttons and
// flame-coloured highlights. The artwork is made from the player's ROM by
// RnrrArtJob (rnrr_art.cpp) into assets/launcher/; until then the launcher
// falls back to plain colours.

ImVec4 rgb(int hex, float a = 1.0f)
{
    return ImVec4(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, a);
}

ImU32 col32(int hex, float a = 1.0f) { return ImGui::GetColorU32(rgb(hex, a)); }

const ImVec4 kGold = rgb(0xffd23f);
const ImVec4 kAccent = rgb(0xff8a1f);
const ImVec4 kText = rgb(0xf6ece4);
const ImVec4 kMuted = rgb(0xc2aaa0);
const ImVec4 kOk = rgb(0x5be08f);
const ImVec4 kBad = rgb(0xff5a52);

struct Fonts { ImFont *small, *body, *bold, *head, *button; };

struct Tex { unsigned id = 0; int w = 0, h = 0; };

extern "C" unsigned char *stbi_load(const char *filename, int *x, int *y, int *comp, int req_comp);
extern "C" void stbi_image_free(void *retval_from_stbi_load);

Tex upload(int w, int h, const void *px, int row_px, bool repeat)
{
    Tex t;
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, repeat ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : 0x812F /* CLAMP_TO_EDGE */);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : 0x812F);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_px);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return t;
}

/* Launcher art by base name: the .bmp the art job writes, or a .png. */
Tex load_tex(const char *base, bool repeat)
{
    const std::string bmp = exe_path((std::string(base) + ".bmp").c_str());
    if (SDL_Surface *s = SDL_LoadBMP(bmp.c_str())) {
        SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(s);
        if (c) {
            Tex t = upload(c->w, c->h, c->pixels, c->pitch / 4, repeat);
            SDL_DestroySurface(c);
            return t;
        }
    }
    int w = 0, h = 0, n = 0;
    const std::string png = exe_path((std::string(base) + ".png").c_str());
    unsigned char *px = stbi_load(png.c_str(), &w, &h, &n, 4);
    if (!px) return Tex();
    Tex t = upload(w, h, px, 0, repeat);
    stbi_image_free(px);
    return t;
}

ImTextureID tid(const Tex &t) { return (ImTextureID)(intptr_t)t.id; }

struct Art { Tex logo, poster, race, marble; };

void load_art(Art &art)
{
    for (Tex *t : {&art.logo, &art.poster, &art.race, &art.marble})
        if (t->id) { glDeleteTextures(1, &t->id); *t = Tex(); }
    art.logo = load_tex("assets/launcher/logo", false);
    art.poster = load_tex("assets/launcher/poster", false);
    art.race = load_tex("assets/launcher/race", false);
    art.marble = load_tex("assets/launcher/marble", true);
}

void style(float s)
{
    ImGuiStyle &st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(12 * s, 9 * s);
    st.ItemSpacing = ImVec2(8 * s, 10 * s);
    st.FrameRounding = 5 * s;
    st.ChildRounding = 0;
    st.GrabRounding = 4 * s;
    st.GrabMinSize = 20 * s;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 0;
    st.FrameBorderSize = 1;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Border] = rgb(0x000000, 0.9f);
    c[ImGuiCol_Button] = rgb(0x4a4a4a);
    c[ImGuiCol_ButtonHovered] = rgb(0x626262);
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_FrameBg] = rgb(0x2a1212);
    c[ImGuiCol_FrameBgHovered] = rgb(0x3a1818);
    c[ImGuiCol_FrameBgActive] = rgb(0x3a1818);
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kGold;
    c[ImGuiCol_NavHighlight] = rgb(0xffffff);
    c[ImGuiCol_PopupBg] = rgb(0x1a0b0b, 0.96f);
}

// A red-marble frame like the game's menus: marble band, then a dark inset
// panel with a black outline and a grey bevel. Returns the inner rectangle.
void marble_frame(ImDrawList *dl, ImVec2 a, ImVec2 b, float border, const Art &art, float s,
                  ImVec2 *in_a, ImVec2 *in_b)
{
    if (art.marble.id) {
        float tile = art.marble.w * 0.75f * s;
        dl->AddImage(tid(art.marble), a, b, ImVec2(a.x / tile, a.y / tile), ImVec2(b.x / tile, b.y / tile),
                     col32(0xc8b8b8));
    } else {
        dl->AddRectFilled(a, b, col32(0x7a0e0e));
    }
    dl->AddRect(a, b, col32(0x000000), 0, 0, 2 * s);
    dl->AddLine(ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, a.y + 2 * s), col32(0xff8a7a, 0.35f), s);
    ImVec2 ia(a.x + border, a.y + border), ib(b.x - border, b.y - border);
    dl->AddRectFilled(ia, ib, col32(0x140808, 0.88f));
    dl->AddRect(ImVec2(ia.x - 2 * s, ia.y - 2 * s), ImVec2(ib.x + 2 * s, ib.y + 2 * s), col32(0x9a9a9a), 0, 0, 2 * s);
    dl->AddRect(ImVec2(ia.x - 1, ia.y - 1), ImVec2(ib.x + 1, ib.y + 1), col32(0x000000), 0, 0, 2 * s);
    *in_a = ia;
    *in_b = ib;
}

// Flame-coloured heading with a dark drop shadow, like the game's titles.
void heading(ImDrawList *dl, ImFont *font, ImVec2 p, const char *text, float s)
{
    ImGui::PushFont(font);
    dl->AddText(ImVec2(p.x + 2 * s, p.y + 2 * s), col32(0x3a0000), text);
    dl->AddText(p, col32(0xffb02e), text);
    ImGui::PopFont();
}

bool nav_ring_visible() { return ImGui::GetIO().NavVisible; }

// A metal button (grey) or, when selected, a flame-gradient button.
bool chip(const char *label, bool selected, ImVec2 size, float s, bool enabled = true)
{
    ImGui::PushID(label);
    bool pressed = ImGui::InvisibleButton("##chip", size) && enabled;
    bool hovered = ImGui::IsItemHovered() && enabled, held = ImGui::IsItemActive() && enabled;
    bool focused = ImGui::IsItemFocused();
    ImGui::PopID();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    float r = 5 * s;
    ImU32 top, bot, edge, txt;
    if (!enabled) {
        top = col32(0x3a3434); bot = col32(0x2a2424); edge = col32(0x000000); txt = col32(0x7a6a64);
    } else if (selected) {
        top = col32(held ? 0xffe27a : 0xffd84a); bot = col32(hovered ? 0xff8f2a : 0xff6a10);
        edge = col32(0x5a0c00); txt = col32(0x2a0700);
    } else {
        top = col32(hovered ? 0x8a8a8a : 0x6e6e6e); bot = col32(hovered ? 0x4c4c4c : 0x383838);
        edge = col32(0x000000); txt = col32(0xf2f2f2);
    }
    dl->AddRectFilled(a, b, edge, r);
    ImVec2 ia(a.x + 2 * s, a.y + 2 * s), ib(b.x - 2 * s, b.y - 2 * s);
    dl->AddRectFilledMultiColor(ia, ib, top, top, bot, bot);
    dl->AddLine(ImVec2(ia.x + 1, ia.y), ImVec2(ib.x - 1, ia.y), col32(0xffffff, selected ? 0.55f : 0.30f), s);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 tp(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f);
    if (!selected && enabled) dl->AddText(ImVec2(tp.x + s, tp.y + s), col32(0x000000, 0.8f), label);
    dl->AddText(tp, txt, label);
    if (focused && nav_ring_visible())
        dl->AddRect(ImVec2(a.x - 3 * s, a.y - 3 * s), ImVec2(b.x + 3 * s, b.y + 3 * s), col32(0xffffff), r + 2 * s, 0, 2 * s);
    return pressed;
}

// A row of mutually exclusive chips. Returns true when the choice changed.
bool choice(const char *id, const char *const *labels, int n, int *value, float width, float s,
            int *hot = nullptr)
{
    bool changed = false;
    float gap = 6 * s, w = (width - gap * (n - 1)) / n;
    float h = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    for (int i = 0; i < n; i++) {
        ImGui::PushID(i);
        if (chip(labels[i], *value == i, ImVec2(w, h), s)) { changed = *value != i; *value = i; }
        if (hot && (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && nav_ring_visible()))) *hot = i;
        ImGui::PopID();
        if (i + 1 < n) ImGui::SameLine(0, gap);
    }
    ImGui::PopID();
    return changed;
}

void dot(ImVec4 color, float s, bool framed = false)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    if (framed) {
        p.y += ImGui::GetStyle().FramePadding.y;
        ImGui::AlignTextToFramePadding();
    }
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5 * s, p.y + h * 0.5f), 4.5f * s,
                                                ImGui::GetColorU32(color));
    ImGui::Dummy(ImVec2(14 * s, h));
    ImGui::SameLine(0, 4 * s);
}

void label(const char *text, float column, float s)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(column);
    (void)s;
}

// The big PLAY button: logo-flame gradient with a dark red rim.
bool play_button(const char *label, ImVec2 size, bool enabled, ImFont *font, float s)
{
    bool pressed = ImGui::InvisibleButton("##play", size) && enabled;
    bool hovered = ImGui::IsItemHovered() && enabled, held = ImGui::IsItemActive() && enabled;
    bool focused = ImGui::IsItemFocused();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    float r = 10 * s;
    if (focused && nav_ring_visible())
        dl->AddRect(ImVec2(a.x - 4 * s, a.y - 4 * s), ImVec2(b.x + 4 * s, b.y + 4 * s), col32(0xffffff), r + 3 * s, 0, 2.5f * s);
    dl->AddRectFilled(ImVec2(a.x + 4 * s, a.y + 5 * s), ImVec2(b.x + 4 * s, b.y + 5 * s), col32(0x000000, 0.55f), r);
    dl->AddRectFilled(a, b, col32(enabled ? 0x6a0a00 : 0x2a2424), r);
    ImVec2 ia(a.x + 3 * s, a.y + 3 * s), ib(b.x - 3 * s, b.y - 3 * s);
    ImVec2 mid(ib.x, (ia.y + ib.y) * 0.5f);
    if (enabled) {
        ImU32 c0 = col32(held ? 0xfff0a0 : hovered ? 0xffec7a : 0xffe04a);
        ImU32 c1 = col32(hovered ? 0xffa22a : 0xff8a14);
        ImU32 c2 = col32(hovered ? 0xf04a10 : 0xd8340a);
        dl->AddRectFilledMultiColor(ia, mid, c0, c0, c1, c1);
        dl->AddRectFilledMultiColor(ImVec2(ia.x, mid.y), ib, c1, c1, c2, c2);
        dl->AddLine(ImVec2(ia.x + 2, ia.y + 1), ImVec2(ib.x - 2, ia.y + 1), col32(0xffffff, 0.6f), 1.5f * s);
    } else {
        dl->AddRectFilled(ia, ib, col32(0x3a3434), r - 2 * s);
    }
    ImGui::PushFont(font);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 tp(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f);
    dl->AddText(ImVec2(tp.x, tp.y + 2 * s), col32(0xffffff, enabled ? 0.45f : 0.0f), label);
    dl->AddText(tp, col32(enabled ? 0x3a0600 : 0x7a6a64), label);
    ImGui::PopFont();
    return pressed;
}

bool write_bmp(const char *path, int w, int h, const std::vector<uint8_t> &rgba)
{
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    uint32_t img = (uint32_t)w * h * 4, size = 54 + img;
    uint8_t hdr[54] = {'B', 'M'};
    std::memcpy(hdr + 2, &size, 4);
    uint32_t off = 54, hsz = 40;
    std::memcpy(hdr + 10, &off, 4);
    std::memcpy(hdr + 14, &hsz, 4);
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);  // bottom-up, matches glReadPixels
    uint16_t planes = 1, bpp = 32;
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &img, 4);
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> bgra(rgba);
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    std::fwrite(bgra.data(), 1, bgra.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace

extern "C" int RnrrLauncherRun(const char *default_ini, const char *expected_sha,
                               char *rom_out, size_t rom_cap)
{
    const char *shot = SDL_getenv("RNRR_LAUNCHER_SHOT");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "[launcher] SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    g_pick_lock = SDL_CreateMutex();

    float s = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (s < 1.0f || s > 4.0f) s = 1.0f;
    int win_w = (int)(1100 * s), win_h = (int)(680 * s);
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)) {
        if (win_w > usable.w * 0.92f || win_h > usable.h * 0.92f) {
            float k = SDL_min(usable.w * 0.92f / win_w, usable.h * 0.92f / win_h);
            s *= k;
            win_w = (int)(win_w * k);
            win_h = (int)(win_h * k);
        }
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL |
        (shot || SDL_getenv("RNRR_LAUNCHER_UPDATE_TEST") ? SDL_WINDOW_HIDDEN : 0);
    SDL_Window *window = SDL_CreateWindow("Rock & Roll Racing", win_w, win_h, flags);
    if (!window) { SDL_Quit(); return 2; }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { SDL_DestroyWindow(window); SDL_Quit(); return 2; }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    style(s);
    Fonts fonts{};
    const std::string regular = exe_path("assets/fonts/LatoLatin-Regular.ttf");
    const std::string bold = exe_path("assets/fonts/LatoLatin-Bold.ttf");
    fonts.body = io.Fonts->AddFontFromFileTTF(regular.c_str(), 16 * s);
    if (!fonts.body) fonts.body = io.Fonts->AddFontDefault();
    fonts.small = io.Fonts->AddFontFromFileTTF(regular.c_str(), 14 * s);
    fonts.bold = io.Fonts->AddFontFromFileTTF(bold.c_str(), 16 * s);
    fonts.head = io.Fonts->AddFontFromFileTTF(bold.c_str(), 22 * s);
    fonts.button = io.Fonts->AddFontFromFileTTF(bold.c_str(), 32 * s);
    if (!fonts.small) fonts.small = fonts.body;
    if (!fonts.bold) fonts.bold = fonts.head = fonts.button = fonts.body;
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 130");
    Art art;
    load_art(art);

    Settings set = load_settings(default_ini);
    if (const char *t = SDL_getenv("RNRR_LAUNCHER_SAVE_TEST")) {
        int a, f, i, m, v;
        if (std::sscanf(t, "%d,%d,%d,%d,%d", &a, &f, &i, &m, &v) == 5) {
            set.aspect = a; set.fullscreen = f != 0; set.image = i; set.smooth = m != 0; set.volume = v;
            save_settings(set, default_ini);
        }
    }
    (void)expected_sha;  /* any supported revision is accepted (rnrr_revision.c) */
    std::string rom = load_rom_path();
    RomInfo rinfo = check_rom(rom);
    std::string exe_dir = exe_path("");
    if (!exe_dir.empty() && exe_dir.back() != '/' && exe_dir.back() != '\\') exe_dir += '/';
    RnrrArtJob artjob;
    if (rinfo.state == ROM_OK && !SDL_getenv("RNRR_LAUNCHER_NO_ART")) artjob.start(exe_dir, rom, rinfo.sha);
    /* Updates: ask GitHub in the background. Screenshot runs stay offline
     * unless a test points the check somewhere (RNRR_UPDATE_*). */
    RnrrUpdater::cleanup_old();
    RnrrUpdater updater;
    const bool update_hooks = SDL_getenv("RNRR_UPDATE_FAKE_VERSION") || SDL_getenv("RNRR_UPDATE_API");
    const bool update_test = SDL_getenv("RNRR_LAUNCHER_UPDATE_TEST") != nullptr;  /* headless: check, install, quit */
    if (!shot || update_hooks || update_test) updater.check();
    bool relaunch = false;
    bool pick_failed = false;
    if (const char *t = SDL_getenv("RNRR_LAUNCHER_PICK")) {  /* test hook: as if the file was dropped */
        g_picked = t;
        g_pick_done = true;
    }
    const bool shot_waits_for_art = SDL_getenv("RNRR_LAUNCHER_SHOT_WAIT_ART") != nullptr;
    Uint64 shot_deadline = SDL_GetTicks() + 180000;
    std::vector<std::string> pads;
    int result = -1, frame = 0;
    bool first_focus = true;
    bool show_controls = SDL_getenv("RNRR_LAUNCHER_SHOW_CONTROLS") != nullptr;  /* screenshot hook */

    while (result < 0) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) result = 0;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                if (show_controls) show_controls = false;
                else result = 0;
            }
            if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && show_controls &&
                (ev.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || ev.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH))
                show_controls = false;
            else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev.gbutton.button == SDL_GAMEPAD_BUTTON_START &&
                     rinfo.state == ROM_OK)
                result = 1;
            /* Dropping the ROM file on the window is the same as picking it. */
            if (ev.type == SDL_EVENT_DROP_FILE && ev.drop.data) {
                SDL_LockMutex(g_pick_lock);
                g_picked = ev.drop.data;
                g_pick_done = true;
                SDL_UnlockMutex(g_pick_lock);
            }

        }
        SDL_LockMutex(g_pick_lock);
        if (g_pick_done) {
            g_pick_done = false;
            if (!g_picked.empty()) {
                std::string stored;
                RomInfo picked;
                if (adopt_rom(g_picked, stored, picked)) {
                    rom = stored;
                    rinfo = check_rom(rom);
                    pick_failed = false;
                    artjob.cancel();
                    if (rinfo.state == ROM_OK) artjob.start(exe_dir, rom, rinfo.sha);
                } else {
                    pick_failed = true;
                }
            }
        }
        SDL_UnlockMutex(g_pick_lock);
        if (artjob.running() && !artjob.poll() && artjob.finished_ok()) load_art(art);
        if (frame++ % 30 == 0) {
            pads.clear();
            int n = 0;
            SDL_JoystickID *ids = SDL_GetGamepads(&n);
            for (int i = 0; ids && i < n; i++) {
                const char *name = SDL_GetGamepadNameForID(ids[i]);
                pads.push_back(name && *name ? name : "Mando");
            }
            SDL_free(ids);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImVec2 size = io.DisplaySize;
        ImDrawList *bg = ImGui::GetBackgroundDrawList();

        // Backdrop: a race scene, darkened, with a vignette and a red cast.
        bg->AddRectFilled(ImVec2(0, 0), size, col32(0x0a0404));
        if (art.race.id) {
            float img_aspect = (float)art.race.w / art.race.h, win_aspect = size.x / size.y;
            ImVec2 uv0(0, 0), uv1(1, 1);
            if (img_aspect > win_aspect) {
                float f = win_aspect / img_aspect;
                uv0.x = (1 - f) * 0.5f; uv1.x = uv0.x + f;
            } else {
                float f = img_aspect / win_aspect;
                uv0.y = (1 - f) * 0.5f; uv1.y = uv0.y + f;
            }
            bg->AddImage(tid(art.race), ImVec2(0, 0), size, uv0, uv1, col32(0xa89898));
        }
        bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x * 0.5f, size.y), col32(0x050202, 0.80f),
                                    col32(0x050202, 0.35f), col32(0x050202, 0.35f), col32(0x050202, 0.80f));
        bg->AddRectFilledMultiColor(ImVec2(0, size.y * 0.70f), size, col32(0x050202, 0.0f),
                                    col32(0x050202, 0.0f), col32(0x050202, 0.85f), col32(0x050202, 0.85f));
        bg->AddRectFilled(ImVec2(0, 0), size, col32(0x400000, 0.16f));
        bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, 5 * s), col32(0xffd84a), col32(0xd8340a),
                                    col32(0xd8340a), col32(0xffd84a));

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float pad = 30 * s;
        const float wx = ImGui::GetWindowPos().x;
        const float left_w = 400 * s;
        const float right_x = pad + left_w + 26 * s;
        const float border = 14 * s;
        ImVec2 ia, ib;
        if (show_controls) ImGui::BeginDisabled();

        // ---- Left: identity and the one primary action. ----
        float y = 22 * s;
        if (art.logo.id) {
            float lw = left_w, lh = lw * art.logo.h / art.logo.w;
            dl->AddImage(tid(art.logo), ImVec2(pad, y), ImVec2(pad + lw, y + lh));
            y += lh + 6 * s;
        } else {
            heading(dl, fonts.button, ImVec2(pad, y + 20 * s), "ROCK & ROLL RACING", s);
            y += 80 * s;
        }
        ImGui::PushFont(fonts.small);
        {
            std::string sub = "Edición PC";
            if (rinfo.state == ROM_OK) {
                sub += "  ·  ROM: ";
                sub += RnrrRevisionName(rinfo.rev);
                if (!RnrrRevisionVerified(rinfo.rev)) sub += " (sin verificar)";
            }
            dl->AddText(ImVec2(pad + 4 * s, y), ImGui::GetColorU32(kMuted), sub.c_str());
        }
        ImGui::PopFont();
        y += 30 * s;

        const float play_h = 66 * s;
        const float sec_h = 40 * s;
        const float bottom = size.y - pad;
        float poster_b = bottom - play_h - sec_h - 2 * 14 * s;
        marble_frame(dl, ImVec2(pad, y), ImVec2(pad + left_w, poster_b), border, art, s, &ia, &ib);
        if (art.poster.id) {
            // Fit the illustration inside the frame, centred on its focal point.
            float fw = ib.x - ia.x, fh = ib.y - ia.y;
            float ar = (float)art.poster.w / art.poster.h;
            ImVec2 uv0(0, 0), uv1(1, 1);
            if (fw / fh > ar) { float vis = ar / (fw / fh); uv0.y = (1 - vis) * 0.35f; uv1.y = uv0.y + vis; }
            else { float vis = (fw / fh) / ar; uv0.x = (1 - vis) * 0.5f; uv1.x = uv0.x + vis; }
            dl->AddImage(tid(art.poster), ia, ib, uv0, uv1);
        }

        if (rinfo.state != ROM_OK) {
            // First run: the one thing to do is give the launcher a ROM.
            const float tx = ia.x + 22 * s, tw = ib.x - ia.x - 44 * s;
            float ty = ia.y + 24 * s;
            heading(dl, fonts.head, ImVec2(tx, ty), "TU ROM DEL JUEGO", s);
            ty += 40 * s;
            ImGui::PushFont(fonts.body);
            const char *msg = "Para jugar necesitas tu propia copia de Rock & Roll Racing para Super Nintendo "
                              "(archivo .sfc o .smc). Se aceptan la versión europea y la de EE. UU.";
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, ty), ImGui::GetColorU32(kText), msg,
                        nullptr, tw);
            ty += ImGui::CalcTextSize(msg, nullptr, false, tw).y + 18 * s;
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(tx, ty));
            ImGui::PushFont(fonts.bold);
            if (chip("Elegir archivo...", true, ImVec2(tw, 44 * s), s)) {
                static const SDL_DialogFileFilter filters[] = {{"ROM de SNES (.sfc, .smc)", "sfc;smc"}};
                SDL_ShowOpenFileDialog(on_rom_picked, nullptr, window, filters, 1, nullptr, false);
            }
            if (first_focus) { ImGui::SetItemDefaultFocus(); first_focus = false; }
            ImGui::PopFont();
            ty += 56 * s;
            ImGui::PushFont(fonts.small);
            dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(kMuted), "o arrastra el archivo a esta ventana.");
            ty += 30 * s;
            if (pick_failed || rinfo.state == ROM_WRONG) {
                const char *err = "Ese archivo no es Rock & Roll Racing (Europa o EE. UU.) o está dañado. "
                                  "Prueba con otra copia.";
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, ty), ImGui::GetColorU32(kBad), err,
                            nullptr, tw);
            }
            ImGui::PopFont();
        } else if (artjob.running()) {
            // First launch with this ROM: the launcher's look is being made from it.
            dl->AddRectFilled(ia, ib, col32(0x000000, art.poster.id ? 0.55f : 0.0f));
            const float tx = ia.x + 22 * s, tw = ib.x - ia.x - 44 * s;
            float ty = ia.y + (ib.y - ia.y) * 0.5f - 46 * s;
            ImGui::PushFont(fonts.bold);
            dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(kText), "Preparando el aspecto del lanzador");
            ImGui::PopFont();
            ty += 30 * s;
            dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + 10 * s), col32(0x2a1414), 5 * s);
            float p = SDL_max(0.04f, artjob.progress());
            dl->AddRectFilledMultiColor(ImVec2(tx, ty), ImVec2(tx + tw * p, ty + 10 * s), col32(0xffd84a),
                                        col32(0xff6a10), col32(0xff6a10), col32(0xffd84a));
            ty += 22 * s;
            ImGui::PushFont(fonts.small);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, ty), ImGui::GetColorU32(kMuted),
                        "Se crea a partir de tu ROM, solo la primera vez (unos segundos). Puedes jugar ya.",
                        nullptr, tw);
            ImGui::PopFont();
        }

        float by = poster_b + 14 * s;
        ImGui::SetCursorScreenPos(ImVec2(pad, by));
        const RnrrUpdater::State ust = updater.state();
        const bool updating = ust == RnrrUpdater::kDownloading || ust == RnrrUpdater::kInstalling ||
                              ust == RnrrUpdater::kInstalled;
        if (play_button("JUGAR", ImVec2(left_w, play_h), rinfo.state == ROM_OK && !updating, fonts.button, s))
            result = 1;
        /* Keyboard/pad navigation starts on PLAY (Enter or A plays). */
        if (first_focus && rinfo.state == ROM_OK) {
            ImGui::SetItemDefaultFocus();
            ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
            first_focus = false;
        }
        by += play_h + 14 * s;
        const float half = (left_w - 12 * s) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(pad, by));
        ImGui::PushFont(fonts.bold);
        if (chip("Controles", false, ImVec2(half, sec_h), s)) { show_controls = true; ImGui::SetWindowFocus(nullptr); }
        ImGui::SameLine(0, 12 * s);
        if (chip("Salir", false, ImVec2(half, sec_h), s)) result = 0;
        ImGui::PopFont();

        // ---- Right: settings, grouped, each with a line that says what the
        // option under the cursor (or the chosen one) does. ----
        marble_frame(dl, ImVec2(right_x, 22 * s), ImVec2(size.x - pad, bottom), border, art, s, &ia, &ib);
        const float row_x = ia.x + 22 * s - wx;
        const float col = row_x + 132 * s;
        const float ctl_w = ib.x - wx - 22 * s - col;
        float cy = ia.y + 16 * s;

        static const char *kAspect[] = {"4:3", "16:9", "21:9", "Ajustar"};
        static const char *kAspectHelp[] = {
            "Proporción original de la consola.",
            "Panorámico: en carrera ves más pista a los lados.",
            "Ultrapanorámico: el máximo que el juego puede rellenar.",
            "Se adapta a tu monitor, hasta 21:9."};
        static const char *kScreen[] = {"Pantalla completa", "Ventana"};
        static const char *kScreenHelp[] = {
            "Ocupa todo el monitor. Alt+Enter alterna durante el juego.",
            "En una ventana que puedes mover y redimensionar."};
        static const char *kImage[] = {"Suavizada", "Nítida", "CRT", "TV clásica"};
        static const char *kImageHelp[] = {
            "Bordes y diagonales sin dientes de sierra.",
            "Píxeles limpios y uniformes, como el original.",
            "Líneas de un monitor de tubo.",
            "Color y suavidad de un televisor de la época."};
        static const char *kMotion[] = {"Suave", "Original"};
        static const char *kMotionHelp[] = {
            "Interpola las ~20 imágenes/s del juego en carrera hasta los Hz de tu monitor.",
            "Solo las imágenes que genera el juego, sin interpolar."};
        static const char *kEngine[] = {"Nativo", "Intérprete"};
        static const char *kEngineHelp[] = {
            "Código recompilado para PC: el más rápido. Recomendado.",
            "Ejecuta la consola instrucción a instrucción. Respaldo si algo fallara."};
        int screen = set.fullscreen ? 0 : 1, motion = set.smooth ? 0 : 1;

        auto section = [&](const char *title) {
            heading(dl, fonts.bold, ImVec2(ia.x + 22 * s, cy), title, s);
            float ly = cy + ImGui::GetFontSize() * 0.5f + 1 * s;
            float tx = ia.x + 22 * s + 14 * s;
            ImGui::PushFont(fonts.bold);
            tx += ImGui::CalcTextSize(title).x;
            ImGui::PopFont();
            dl->AddLine(ImVec2(tx, ly), ImVec2(ib.x - 22 * s, ly), col32(0x6a2a2a), 1.5f * s);
            cy += 30 * s;
        };
        auto row = [&](const char *name, const char *id, const char *const *labels,
                       const char *const *help, int n, int *value, float width) {
            ImGui::SetCursorScreenPos(ImVec2(wx + row_x, cy));
            label(name, col, s);
            int hot = -1;
            bool changed = choice(id, labels, n, value, width, s, &hot);
            float hy = cy + ImGui::GetFrameHeight() + 5 * s;
            const char *text = help[hot >= 0 ? hot : *value];
            ImGui::PushFont(fonts.small);
            float tw = ib.x - 22 * s - (wx + col);
            float th = ImGui::CalcTextSize(text, nullptr, false, tw).y;
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(wx + col, hy),
                        ImGui::GetColorU32(hot >= 0 && hot != *value ? kText : kMuted), text, nullptr, tw);
            ImGui::PopFont();
            cy += ImGui::GetFrameHeight() + 18 * s + SDL_max(th, 16 * s);
            return changed;
        };

        section("IMAGEN");
        row("Formato", "aspect", kAspect, kAspectHelp, 4, &set.aspect, ctl_w);
        if (row("Pantalla", "screen", kScreen, kScreenHelp, 2, &screen, ctl_w)) set.fullscreen = screen == 0;
        row("Filtro", "image", kImage, kImageHelp, 4, &set.image, ctl_w);
        cy += 4 * s;
        section("RENDIMIENTO");
        if (row("Movimiento", "motion", kMotion, kMotionHelp, 2, &motion, ctl_w * 0.6f)) set.smooth = motion == 0;
#if !defined(RNRR_PUBLIC_BUILD)
        /* Only builds compiled from a player's own ROM carry native code. */
        row("Motor", "engine", kEngine, kEngineHelp, 2, &set.engine, ctl_w * 0.6f);
#else
        (void)kEngine; (void)kEngineHelp;
#endif
        cy += 4 * s;
        section("SONIDO");
        ImGui::SetCursorScreenPos(ImVec2(wx + row_x, cy));
        label("Volumen", col, s);
        ImGui::SetNextItemWidth(ctl_w);
        ImGui::SliderInt("##vol", &set.volume, 0, 100, set.volume ? "%d%%" : "Silencio");

        if (ust == RnrrUpdater::kAvailable || updating || ust == RnrrUpdater::kFailed) {
            // A newer release on GitHub: the tip's place becomes the update box.
            const bool can = RnrrUpdater::can_install();
            const std::string ver = updater.latest();
            std::string title, body;
            const char *btn = nullptr;
            bool show_bar = false;
            switch (ust) {
            case RnrrUpdater::kAvailable:
                title = "Nueva versión " + ver + " disponible";
                body = can ? "Se descarga e instala sola. Tu ROM, tus opciones y tus partidas se conservan."
                           : "Descárgala desde la página del proyecto en GitHub.";
                btn = can ? "Actualizar" : "Ver en GitHub";
                break;
            case RnrrUpdater::kDownloading:
                title = "Descargando la versión " + ver + "...";
                show_bar = true;
                break;
            case RnrrUpdater::kInstalling:
                title = "Instalando la versión " + ver + "...";
                show_bar = true;
                break;
            case RnrrUpdater::kInstalled:
                title = "Versión " + ver + " instalada. Reiniciando...";
                break;
            default:
                title = "No se pudo actualizar";
                body = updater.error() + ". También puedes descargarla desde GitHub.";
                btn = can ? "Reintentar" : "Ver en GitHub";
                break;
            }
            const float bl = ia.x + 12 * s, br = ib.x - 12 * s, bb = ib.y - 12 * s;
            const float btn_w = btn ? 150 * s : 0, btn_h = 36 * s;
            const float tx = bl + 20 * s, tw = br - tx - 14 * s - (btn ? btn_w + 16 * s : 0);
            ImGui::PushFont(fonts.bold);
            const float title_h = ImGui::GetFontSize();
            ImGui::PopFont();
            ImGui::PushFont(fonts.small);
            const float body_h = body.empty() ? 0 : ImGui::CalcTextSize(body.c_str(), nullptr, false, tw).y;
            ImGui::PopFont();
            const float content_h = title_h + (body.empty() ? 0 : 6 * s + body_h) + (show_bar ? 14 * s : 0);
            const float h = SDL_max(content_h, btn_h) + 20 * s;
            const float bt = bb - h;
            dl->AddRectFilled(ImVec2(bl, bt), ImVec2(br, bb), col32(0x000000, 0.45f), 6 * s);
            dl->AddRectFilled(ImVec2(bl, bt), ImVec2(bl + 3 * s, bb), col32(0xffd23f), 0);
            float ty = bt + (h - content_h) * 0.5f;
            ImGui::PushFont(fonts.bold);
            dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ust == RnrrUpdater::kFailed ? kBad : kGold), title.c_str());
            ImGui::PopFont();
            ty += title_h;
            if (!body.empty()) {
                ty += 6 * s;
                ImGui::PushFont(fonts.small);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, ty), ImGui::GetColorU32(kMuted),
                            body.c_str(), nullptr, tw);
                ImGui::PopFont();
            }
            if (show_bar) {
                ty += 6 * s;
                dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + 8 * s), col32(0x2a1414), 4 * s);
                float p = ust == RnrrUpdater::kInstalling ? 1.0f : SDL_max(0.03f, updater.progress());
                dl->AddRectFilledMultiColor(ImVec2(tx, ty), ImVec2(tx + tw * p, ty + 8 * s), col32(0xffd84a),
                                            col32(0xff6a10), col32(0xff6a10), col32(0xffd84a));
            }
            if (btn) {
                ImGui::SetCursorScreenPos(ImVec2(br - 14 * s - btn_w, bt + (h - btn_h) * 0.5f));
                ImGui::PushFont(fonts.bold);
                if (chip(btn, true, ImVec2(btn_w, btn_h), s)) {
                    if (can) updater.install();
                    else SDL_OpenURL(updater.page_url().c_str());
                }
                ImGui::PopFont();
            }
        } else
        {
            // The one tip worth keeping on screen: progress can be saved anywhere.
            ImGui::PushFont(fonts.small);
            const char *tip = "Consejo: guarda tu progreso en cualquier momento con Select (View) + RB en el mando "
                              "o F11 en el teclado. Todos los controles, en el botón Controles.";
            float tw = ib.x - ia.x - 44 * s;
            float th = ImGui::CalcTextSize(tip, nullptr, false, tw).y;
            ImVec2 ta(ia.x + 22 * s, ib.y - th - 22 * s);
            dl->AddRectFilled(ImVec2(ta.x - 10 * s, ta.y - 8 * s), ImVec2(ib.x - 12 * s, ib.y - 12 * s),
                              col32(0x000000, 0.35f), 6 * s);
            dl->AddRectFilled(ImVec2(ta.x - 10 * s, ta.y - 8 * s), ImVec2(ta.x - 7 * s, ib.y - 12 * s),
                              col32(0xffb02e), 0);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ta, ImGui::GetColorU32(kMuted), tip, nullptr, tw);
            ImGui::PopFont();
        }

        if (show_controls) ImGui::EndDisabled();

        // ---- Controls sheet: opened on demand, closed with Esc or B. ----
        if (show_controls) {
            ImDrawList *fg = ImGui::GetForegroundDrawList();
            fg->AddRectFilled(ImVec2(0, 0), size, col32(0x000000, 0.70f));
            const float w = 760 * s, h = 470 * s;
            ImVec2 a((size.x - w) * 0.5f, (size.y - h) * 0.5f), b(a.x + w, a.y + h);
            marble_frame(fg, a, b, border, art, s, &ia, &ib);
            ImGui::PushFont(fonts.head);
            fg->AddText(ImVec2(ia.x + 26 * s, ia.y + 18 * s), col32(0xffb02e), "CONTROLES");
            ImGui::PopFont();
            const float c0 = ia.x + 26 * s, c1 = c0 + 230 * s, c2 = c1 + 220 * s;
            float ry = ia.y + 62 * s;
            auto line = [&](const char *a0, const char *a1, const char *a2, bool head) {
                ImGui::PushFont(head ? fonts.bold : fonts.body);
                ImU32 c = head ? col32(0xffb02e) : ImGui::GetColorU32(kText);
                fg->AddText(ImVec2(c0, ry), head ? c : ImGui::GetColorU32(kMuted), a0);
                fg->AddText(ImVec2(c1, ry), c, a1);
                fg->AddText(ImVec2(c2, ry), c, a2);
                ImGui::PopFont();
                ry += (head ? 30 : 25) * s;
            };
            line("", "Mando (Xbox)", "Teclado", true);
            line("Dirección", "Cruceta", "W  A  S  D", false);
            line("Botones SNES  B · A", "A · B", "K · L", false);
            line("Botones SNES  Y · X", "X · Y", "J · I", false);
            line("L · R", "LB · RB", "Q · O", false);
            line("Start · Select", "Start · Select (View)", "Enter · Mayús derecha", false);
            ry += 10 * s;
            line("Partidas guardadas", "", "", true);
            line("Menú de partidas", "Select (View) + RB", "F11", false);
            line("Rebobinar", "Select (View) + R3", "F12", false);
            line("Carga / guardado rápido", "en el menú", "F1-F10 / Mayús+F1-F10", false);
            ImGui::PushFont(fonts.small);
            fg->AddText(ImVec2(c0, ib.y - 34 * s), ImGui::GetColorU32(kMuted),
                        "En el menú de partidas: arriba/abajo elige ranura. Con mando: B carga, Y guarda, A vuelve.");
            fg->AddText(ImVec2(c0, ib.y - 16 * s), ImGui::GetColorU32(kMuted),
                        "Esc, B del mando o un clic para cerrar.");
            ImGui::PopFont();
            /* Widgets underneath were disabled this frame, so the click that
             * closes the sheet cannot also press a button behind it. */
            if (ImGui::IsMouseClicked(0)) show_controls = false;
        }

        ImGui::End();
        ImGui::Render();
        int fw, fh;
        SDL_GetWindowSizeInPixels(window, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0.05f, 0.02f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (ust == RnrrUpdater::kInstalled && !update_test) {
            relaunch = true;  /* the new version opens in place of this one */
            result = 0;
        }
        if (update_test) {
            if (ust == RnrrUpdater::kAvailable && RnrrUpdater::can_install()) {
                std::fprintf(stderr, "[update] %s available, installing\n", updater.latest().c_str());
                updater.install();
            } else if (ust == RnrrUpdater::kAvailable) {
                std::fprintf(stderr, "[update] %s available (link only: %s)\n", updater.latest().c_str(),
                             updater.page_url().c_str());
                result = 0;
            } else if (ust == RnrrUpdater::kInstalled || ust == RnrrUpdater::kFailed || ust == RnrrUpdater::kUpToDate) {
                std::fprintf(stderr, "[update] %s %s\n",
                             ust == RnrrUpdater::kInstalled ? "installed" : ust == RnrrUpdater::kFailed ? "failed:" : "up to date",
                             ust == RnrrUpdater::kFailed ? updater.error().c_str() : updater.latest().c_str());
                result = 0;
                /* The relaunched exe inherits the environment: pair this with
                 * RNRR_LAUNCHER_SHOT so it comes up hidden. */
                relaunch = ust == RnrrUpdater::kInstalled && SDL_getenv("RNRR_UPDATE_TEST_RELAUNCH");
            }
        }
        const bool shot_waits_update = (update_hooks || update_test) && ust == RnrrUpdater::kChecking;
        if (shot && !update_test && frame >= 6 && !shot_waits_update &&
            (!shot_waits_for_art || !artjob.running() || SDL_GetTicks() > shot_deadline)) {
            std::vector<uint8_t> px((size_t)fw * fh * 4);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            write_bmp(shot, fw, fh, px);
            result = SDL_getenv("RNRR_LAUNCHER_AUTOPLAY") && rinfo.state == ROM_OK ? 1 : 0;
        }
        SDL_GL_SwapWindow(window);
    }

    artjob.cancel();  /* the game must not share the mod state file with a capture run */
    if (result == 1) save_settings(set, default_ini);
    if (result == 1) {
        SDL_strlcpy(rom_out, rom.c_str(), rom_cap);
    }
    for (Tex *t : {&art.logo, &art.poster, &art.race, &art.marble})
        if (t->id) glDeleteTextures(1, &t->id);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_DestroyMutex(g_pick_lock);
    SDL_Quit();
    if (relaunch) RnrrUpdater::relaunch();
    return result;
}
