// In-place updates from GitHub Releases. See rnrr_update.h.

#include "rnrr_update.h"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include "sha256.h"

/* The engine's stb_image (compiled in glsl_shader.c) carries a raw-deflate
 * decoder: zip entries are raw deflate, so no extra library is needed. */
extern "C" char *stbi_zlib_decode_noheader_malloc(const char *buffer, int len, int *outlen);

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

#ifndef SNES_GAME_VERSION
#define SNES_GAME_VERSION "dev"
#endif

namespace fs = std::filesystem;

namespace {

const char kRepo[] = "Cris9870/rock-and-roll-racing-pc";
const char kAsset[] = "RockAndRollRacingPC-windows.zip";
const char kExeName[] = "RockNRollRacingSNESRecomp.exe";
const char kStaging[] = "update-tmp";
const size_t kMaxDownload = 64u << 20;

std::wstring widen(const std::string &s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring self_exe()
{
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return n ? std::wstring(buf, n) : std::wstring();
}

bool parse_version(const std::string &s, int v[3])
{
    const char *p = s.c_str();
    if (*p == 'v' || *p == 'V') p++;
    v[0] = v[1] = v[2] = 0;
    return std::sscanf(p, "%d.%d.%d", &v[0], &v[1], &v[2]) >= 2;
}

bool newer(const std::string &latest, const std::string &current)
{
    int a[3], b[3];
    if (!parse_version(latest, a) || !parse_version(current, b)) return false;
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

/* The string value of the first `"key"` in [from, to); false when the key is
 * missing there or its value is not a string (e.g. null). */
bool json_str(const std::string &j, size_t from, size_t to, const char *key, std::string &out)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos || p >= to) return false;
    p += k.size();
    while (p < j.size() && (std::isspace((unsigned char)j[p]) || j[p] == ':')) p++;
    if (p >= j.size() || j[p] != '"') return false;
    out.clear();
    for (p++; p < j.size() && j[p] != '"'; p++) {
        if (j[p] == '\\' && p + 1 < j.size()) p++;
        out += j[p];
    }
    return p < j.size();
}

bool http_get(const std::string &url8, const wchar_t *accept, std::vector<uint8_t> &out,
              std::atomic<int> *permille, size_t expected, std::string &err)
{
    out.clear();
    const std::wstring url = widen(url8);
    URL_COMPONENTS uc;
    std::memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { err = "dirección no válida"; return false; }
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring object(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) object.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    HINTERNET ses = WinHttpOpen(L"RockAndRollRacingPC-updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses)
        ses = WinHttpOpen(L"RockAndRollRacingPC-updater/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = "sin conexión"; return false; }
    WinHttpSetTimeouts(ses, 10000, 10000, 15000, 30000);
    HINTERNET con = WinHttpConnect(ses, host.c_str(), uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                        : nullptr;
    bool ok = false;
    if (req) {
        std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\n";
        WinHttpAddRequestHeaders(req, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, len = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
            if (status == 200) {
                ok = true;
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; break; }
                    if (!avail) break;
                    if (out.size() + avail > kMaxDownload) { ok = false; break; }
                    size_t at = out.size();
                    out.resize(at + avail);
                    DWORD got = 0;
                    if (!WinHttpReadData(req, out.data() + at, avail, &got)) { ok = false; break; }
                    out.resize(at + got);
                    if (permille && expected)
                        permille->store((int)SDL_min((size_t)1000, out.size() * 1000 / expected));
                }
                if (!ok) err = "la descarga se interrumpió";
            } else {
                err = "GitHub respondió " + std::to_string(status);
            }
        } else {
            err = "sin conexión con GitHub";
        }
    } else {
        err = "sin conexión con GitHub";
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

uint32_t crc32_of(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static bool init;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct ZipEntry {
    std::string name;
    uint16_t method;
    uint32_t crc, csize, usize, local;
};

uint16_t rd16(const std::vector<uint8_t> &z, size_t p) { return (uint16_t)(z[p] | z[p + 1] << 8); }
uint32_t rd32(const std::vector<uint8_t> &z, size_t p) { return (uint32_t)rd16(z, p) | (uint32_t)rd16(z, p + 2) << 16; }

bool zip_list(const std::vector<uint8_t> &z, std::vector<ZipEntry> &out)
{
    if (z.size() < 22) return false;
    size_t eocd = std::string::npos;
    size_t lowest = z.size() > 22 + 65535 ? z.size() - 22 - 65535 : 0;
    for (size_t i = z.size() - 22 + 1; i-- > lowest;)
        if (rd32(z, i) == 0x06054b50u) { eocd = i; break; }
    if (eocd == std::string::npos) return false;
    size_t count = rd16(z, eocd + 10), p = rd32(z, eocd + 16);
    for (size_t n = 0; n < count; n++) {
        if (p + 46 > z.size() || rd32(z, p) != 0x02014b50u) return false;
        ZipEntry e;
        e.method = rd16(z, p + 10);
        e.crc = rd32(z, p + 16);
        e.csize = rd32(z, p + 20);
        e.usize = rd32(z, p + 24);
        size_t nlen = rd16(z, p + 28), elen = rd16(z, p + 30), clen = rd16(z, p + 32);
        e.local = rd32(z, p + 42);
        if (p + 46 + nlen > z.size()) return false;
        e.name.assign((const char *)&z[p + 46], nlen);
        out.push_back(e);
        p += 46 + nlen + elen + clen;
    }
    return true;
}

bool zip_read(const std::vector<uint8_t> &z, const ZipEntry &e, std::vector<uint8_t> &out)
{
    size_t lh = e.local;
    if (lh + 30 > z.size() || rd32(z, lh) != 0x04034b50u) return false;
    size_t data = lh + 30 + rd16(z, lh + 26) + rd16(z, lh + 28);
    if (data + e.csize > z.size() || e.usize > kMaxDownload) return false;
    if (e.method == 0) {
        if (e.csize != e.usize) return false;
        out.assign(z.begin() + (ptrdiff_t)data, z.begin() + (ptrdiff_t)(data + e.csize));
    } else if (e.method == 8) {
        /* stb's inflater reads a few bytes ahead and calls a stream that ends
         * flush with its buffer corrupt ("bad huffman code"): PNG streams
         * always carry a 4-byte Adler trailer, zip entries do not. Pad it;
         * the final-block flag still ends the decode, and the CRC below
         * checks the result. */
        std::vector<uint8_t> in(z.begin() + (ptrdiff_t)data, z.begin() + (ptrdiff_t)(data + e.csize));
        in.resize(in.size() + 8, 0);
        int n = 0;
        char *p = stbi_zlib_decode_noheader_malloc((const char *)in.data(), (int)in.size(), &n);
        if (!p) return false;
        out.assign((uint8_t *)p, (uint8_t *)p + n);
        std::free(p);
    } else {
        return false;
    }
    return out.size() == e.usize && crc32_of(out.data(), out.size()) == e.crc;
}

/* A zip path as a relative path under the install, or empty if it would land
 * anywhere else. */
std::string safe_relative(const std::string &name)
{
    if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos)
        return std::string();
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        const std::string part = name.substr(start, end - start);
        if (part == ".." || part == ".") return std::string();
        start = end + 1;
    }
    return name;
}

bool write_file(const fs::path &path, const std::vector<uint8_t> &data)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    FILE *f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

std::string hex_of(const uint8_t *p, size_t n)
{
    static const char d[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

}  // namespace

struct RnrrUpdateShared {
    std::atomic<int> state{RnrrUpdater::kIdle};
    std::atomic<int> permille{0};
    std::mutex lock;
    std::string latest, tag, asset_url, digest, error;
    size_t size = 0;
};

namespace {

void fail(RnrrUpdateShared &sh, const std::string &why)
{
    {
        std::lock_guard<std::mutex> g(sh.lock);
        sh.error = why;
    }
    sh.state.store(RnrrUpdater::kFailed);
}

void run_check(std::shared_ptr<RnrrUpdateShared> sh, std::string current)
{
    RnrrUpdater::cleanup_old();  /* the previous process has had time to exit */
    const char *api = SDL_getenv("RNRR_UPDATE_API");
    const std::string url = api && *api ? std::string(api)
                                        : std::string("https://api.github.com/repos/") + kRepo + "/releases/latest";
    std::vector<uint8_t> body;
    std::string err;
    if (!http_get(url, L"application/vnd.github+json", body, nullptr, 0, err)) {
        sh->state.store(RnrrUpdater::kUpToDate);  /* offline: say nothing */
        return;
    }
    const std::string j(body.begin(), body.end());
    std::string tag;
    if (!json_str(j, 0, j.size(), "tag_name", tag) || !newer(tag, current)) {
        sh->state.store(RnrrUpdater::kUpToDate);
        return;
    }
    /* Our asset: the browser_download_url ending in the zip's name. Its object
     * starts at the asset's API url, so digest and size are looked up there. */
    std::string asset_url, digest, size_s;
    size_t at = 0;
    for (;;) {
        size_t p = j.find("\"browser_download_url\"", at);
        if (p == std::string::npos) break;
        std::string u;
        if (json_str(j, p, j.size(), "browser_download_url", u) && u.size() > sizeof(kAsset) &&
            u.compare(u.size() - (sizeof(kAsset) - 1), sizeof(kAsset) - 1, kAsset) == 0 &&
            u[u.size() - sizeof(kAsset)] == '/') {
            size_t obj = j.rfind("/releases/assets/", p);
            if (obj == std::string::npos) obj = 0;
            asset_url = u;
            json_str(j, obj, p, "digest", digest);
            size_t sp = j.find("\"size\"", obj);
            if (sp != std::string::npos && sp < p) {
                sp = j.find(':', sp);
                if (sp != std::string::npos) sh->size = (size_t)std::strtoull(j.c_str() + sp + 1, nullptr, 10);
            }
            break;
        }
        at = p + 1;
    }
    if (asset_url.empty()) {
        sh->state.store(RnrrUpdater::kUpToDate);
        return;
    }
    {
        std::lock_guard<std::mutex> g(sh->lock);
        sh->tag = tag;
        sh->latest = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
        sh->asset_url = asset_url;
        sh->digest = digest;
    }
    sh->state.store(RnrrUpdater::kAvailable);
}

void run_install(std::shared_ptr<RnrrUpdateShared> sh)
{
    std::string url, digest;
    size_t size;
    {
        std::lock_guard<std::mutex> g(sh->lock);
        url = sh->asset_url;
        digest = sh->digest;
        size = sh->size;
    }
    std::vector<uint8_t> zip;
    std::string err;
    if (!http_get(url, L"application/octet-stream", zip, &sh->permille, size, err)) {
        fail(*sh, "no se pudo descargar (" + err + ")");
        return;
    }
    sh->state.store(RnrrUpdater::kInstalling);
    if (digest.rfind("sha256:", 0) == 0) {
        uint8_t h[32];
        sha256_compute(zip.data(), zip.size(), h);
        if (hex_of(h, 32) != digest.substr(7)) {
            fail(*sh, "la descarga llegó dañada");
            return;
        }
    }

    std::vector<ZipEntry> entries;
    if (!zip_list(zip, entries) || entries.empty()) { fail(*sh, "el paquete no es válido"); return; }
    /* Every file sits under one top folder (RockAndRollRacingPC-x.y.z/). */
    const size_t slash = entries[0].name.find('/');
    if (slash == std::string::npos) { fail(*sh, "el paquete no es válido"); return; }
    const std::string top = entries[0].name.substr(0, slash + 1);

    const fs::path exe(self_exe());
    const fs::path dir = exe.parent_path();
    const fs::path staging = dir / kStaging;
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (!fs::create_directories(staging, ec)) {
        fail(*sh, "no se puede escribir en la carpeta del juego; muévela a Documentos o al Escritorio");
        return;
    }

    std::vector<std::string> files;
    bool have_exe = false;
    for (const ZipEntry &e : entries) {
        if (e.name.compare(0, top.size(), top) != 0) { fs::remove_all(staging, ec); fail(*sh, "el paquete no es válido"); return; }
        if (e.name.back() == '/') continue;  /* directory entry */
        const std::string rel = safe_relative(e.name.substr(top.size()));
        if (rel.empty()) { fs::remove_all(staging, ec); fail(*sh, "el paquete no es válido"); return; }
        std::vector<uint8_t> data;
        if (!zip_read(zip, e, data)) {
            std::fprintf(stderr, "[update] bad entry %s method=%u csize=%u usize=%u got=%zu\n", e.name.c_str(),
                         (unsigned)e.method, (unsigned)e.csize, (unsigned)e.usize, data.size());
            fs::remove_all(staging, ec);
            fail(*sh, "el paquete está dañado");
            return;
        }
        if (!write_file(staging / fs::path(widen(rel)), data)) {
            fs::remove_all(staging, ec);
            fail(*sh, "no se puede escribir en la carpeta del juego; muévela a Documentos o al Escritorio");
            return;
        }
        if (rel == kExeName) have_exe = true;
        files.push_back(rel);
    }
    if (!have_exe) { fs::remove_all(staging, ec); fail(*sh, "el paquete no es válido"); return; }

    /* Swap. The exe goes first and is the one step that is rolled back: a
     * folder whose exe failed to update must keep the old, working one. */
    const fs::path old = fs::path(exe.native() + L".old");
    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        fail(*sh, "no se pudo reemplazar el programa");
        return;
    }
    if (!MoveFileExW((staging / kExeName).c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        fail(*sh, "no se pudo reemplazar el programa");
        return;
    }
    int failed = 0;
    for (const std::string &rel : files) {
        if (rel == kExeName) continue;
        const fs::path from = staging / fs::path(widen(rel)), to = dir / fs::path(widen(rel));
        fs::create_directories(to.parent_path(), ec);
        if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) failed++;
    }
    fs::remove_all(staging, ec);
    if (failed) {
        fail(*sh, std::to_string(failed) + " archivos no se pudieron actualizar");
        return;
    }
    sh->state.store(RnrrUpdater::kInstalled);
}

}  // namespace

RnrrUpdater::RnrrUpdater() : sh_(std::make_shared<RnrrUpdateShared>()) {}
RnrrUpdater::~RnrrUpdater() = default;  /* worker threads hold their own reference */

void RnrrUpdater::check()
{
    const char *fake = SDL_getenv("RNRR_UPDATE_FAKE_VERSION");
    const std::string current = fake && *fake ? fake : SNES_GAME_VERSION;
    int v[3];
    if (!parse_version(current, v)) return;  /* "dev": a development build */
    int expect = kIdle;
    if (!sh_->state.compare_exchange_strong(expect, kChecking)) return;
    std::thread(run_check, sh_, current).detach();
}

void RnrrUpdater::install()
{
    int s = sh_->state.load();
    if (!can_install() || (s != kAvailable && s != kFailed)) return;
    {
        std::lock_guard<std::mutex> g(sh_->lock);
        if (sh_->asset_url.empty()) return;
        sh_->error.clear();
    }
    sh_->permille.store(0);
    sh_->state.store(kDownloading);
    std::thread(run_install, sh_).detach();
}

RnrrUpdater::State RnrrUpdater::state() const { return (State)sh_->state.load(); }
float RnrrUpdater::progress() const { return sh_->permille.load() / 1000.0f; }

std::string RnrrUpdater::latest() const
{
    std::lock_guard<std::mutex> g(sh_->lock);
    return sh_->latest;
}

std::string RnrrUpdater::page_url() const
{
    std::lock_guard<std::mutex> g(sh_->lock);
    return std::string("https://github.com/") + kRepo + "/releases" +
           (sh_->tag.empty() ? std::string("/latest") : "/tag/" + sh_->tag);
}

std::string RnrrUpdater::error() const
{
    std::lock_guard<std::mutex> g(sh_->lock);
    return sh_->error;
}

bool RnrrUpdater::can_install()
{
#if defined(RNRR_PUBLIC_BUILD)
    return true;
#else
    return false;  /* a build with the player's own native code must not be replaced by the public one */
#endif
}

void RnrrUpdater::cleanup_old()
{
    const std::wstring exe = self_exe();
    if (!exe.empty()) DeleteFileW((exe + L".old").c_str());
}

bool RnrrUpdater::relaunch()
{
    const std::wstring exe = self_exe();
    if (exe.empty()) return false;
    std::wstring cmd = L"\"" + exe + L"\"";
    const std::wstring dir = fs::path(exe).parent_path().native();
    STARTUPINFOW si;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
