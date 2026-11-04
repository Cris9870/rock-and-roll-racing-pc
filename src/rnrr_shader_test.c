/* Offline check of a shader preset through the real GlslShader pipeline:
 * RNRR_SHADER_TEST="preset|in.bmp|out.bmp|width|height" renders the input
 * frame into an offscreen framebuffer of the given size with a hidden window
 * and writes the result. Used to verify image filters without showing UI. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl_core_3_1.h"
#include "glsl_shader.h"

static int write_bmp(const char *path, int w, int h, const unsigned char *bgra)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    unsigned img = (unsigned)w * h * 4, size = 54 + img, off = 54, hsz = 40;
    unsigned char hdr[54] = {'B', 'M'};
    unsigned short planes = 1, bpp = 32;
    memcpy(hdr + 2, &size, 4); memcpy(hdr + 10, &off, 4); memcpy(hdr + 14, &hsz, 4);
    memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
    memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2); memcpy(hdr + 34, &img, 4);
    fwrite(hdr, 1, 54, f);
    fwrite(bgra, 1, img, f);
    fclose(f);
    return 1;
}

int RnrrShaderTest(const char *spec)
{
    char buf[2048];
    SDL_strlcpy(buf, spec, sizeof(buf));
    char *parts[5] = {0};
    char *p = buf;
    for (int i = 0; i < 5 && p; i++) { parts[i] = p; p = strchr(p, '|'); if (p) *p++ = 0; }
    if (!parts[4]) { fprintf(stderr, "shader test: bad spec\n"); return 2; }
    int ow = atoi(parts[3]), oh = atoi(parts[4]);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 2;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win = SDL_CreateWindow("shader test", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : NULL;
    if (!ctx) { fprintf(stderr, "shader test: no GL context: %s\n", SDL_GetError()); return 2; }
    ogl_LoadFunctions();
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);  /* core profile: the game renderer keeps one bound */
    SDL_Surface *src = SDL_LoadBMP(parts[1]);
    SDL_Surface *img = src ? SDL_ConvertSurface(src, SDL_PIXELFORMAT_ARGB8888) : NULL;
    if (!img) { fprintf(stderr, "shader test: cannot load %s\n", parts[1]); return 2; }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, img->pitch / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img->w, img->h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, img->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    GlTextureWithSize t = {tex, (uint16)img->w, (uint16)img->h};
    GlslShader *gs = GlslShader_CreateFromFile(parts[0]);
    if (!gs) { fprintf(stderr, "shader test: preset failed: %s\n", parts[0]); return 3; }
    GLuint fbo, ctex;
    glGenTextures(1, &ctex);
    glBindTexture(GL_TEXTURE_2D, ctex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ow, oh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctex, 0);
    glViewport(0, 0, ow, oh);
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GlslShader_Render(gs, &t, 0, 0, ow, oh);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    unsigned char *out = malloc((size_t)ow * oh * 4);
    glReadPixels(0, 0, ow, oh, GL_BGRA, GL_UNSIGNED_BYTE, out);
    GLenum err = glGetError();
    int ok = write_bmp(parts[2], ow, oh, out);
    fprintf(stderr, "shader test: %s -> %s %dx%d gl_error=0x%x\n", parts[0], parts[2], ow, oh, err);
    free(out);
    GlslShader_Destroy(gs);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return ok ? 0 : 4;
}
