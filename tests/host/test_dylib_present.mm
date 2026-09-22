// Automated proof of the game-window driver path (Apple only): the test talks
// to the BUILT shared library exclusively through dlopen() + dlsym() +
// eglGetProcAddress() — the exact route apps/tgl_game_window.mm and MobileGL
// use — then draws a red triangle and reads back a red pixel through the
// in-dylib present ABI. No tgles::* C++ call appears in this file; a failure
// names the missing symbol / GL error instead of failing silently.
//
// Ground truth: MobileGL MG_Util/BackendLoaders/OpenGL/Loader.cpp
// (AcquireGLESFunctions via eglGetProcAddress; null is fatal) + VK-GL-CTS
// framework/egl/eglwLibrary.cpp (dlopen libEGL + procaddr). The tglHost*
// extensions additionally resolve through BOTH routes (single-table rule).

#import <Foundation/Foundation.h>
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_DYLIB_HAS_QUARTZ 1
#else
#define TGLES_DYLIB_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include <dlfcn.h>
#include <string>

#if TGLES_DYLIB_HAS_QUARTZ

#ifndef TGLES_HOST_LIBRARY_PATH
#define TGLES_HOST_LIBRARY_PATH "./libtgles.dylib"
#endif

namespace {

// Minimal C-ABI typedefs (Khronos egl.h / GLES3 shapes, values verified in
// docs/reference/egl.h + gl32.h). Defined locally so this file cannot
// accidentally depend on tgles/* headers.
typedef void* Hdpy;
typedef void* Hcfg;
typedef void* Hsurf;
typedef void* Hctx;
typedef void* (*GetProcFn)(const char*);

struct Dylib {
  void* lib = nullptr;
  Hdpy (*GetDisplay)(void*) = nullptr;
  int (*Initialize)(Hdpy, int*, int*) = nullptr;
  int (*ChooseConfig)(Hdpy, const int*, Hcfg*, int, int*) = nullptr;
  Hctx (*CreateContext)(Hdpy, Hcfg, Hctx, const int*) = nullptr;
  Hsurf (*CreatePbufferSurface)(Hdpy, Hcfg, const int*) = nullptr;
  int (*MakeCurrent)(Hdpy, Hsurf, Hsurf, Hctx) = nullptr;
  int (*SwapBuffers)(Hdpy, Hsurf) = nullptr;
  GetProcFn GetProcAddress = nullptr;
  int (*AttachLayer)(void*, int, int) = nullptr;
  int (*HostPresent)(void) = nullptr;
  int (*ReadbackPixel)(int, int, unsigned char[4]) = nullptr;
  void (*GenBuffers)(int, unsigned*) = nullptr;
  void (*BindBuffer)(unsigned, unsigned) = nullptr;
  void (*BufferData)(unsigned, long, const void*, unsigned) = nullptr;
  void (*GenVertexArrays)(int, unsigned*) = nullptr;
  void (*BindVertexArray)(unsigned) = nullptr;
  void (*EnableVertexAttribArray)(unsigned) = nullptr;
  void (*VertexAttribPointer)(unsigned, int, unsigned, unsigned char, int,
                              const void*) = nullptr;
  unsigned (*CreateShader)(unsigned) = nullptr;
  void (*ShaderSource)(unsigned, int, const char* const*, const int*) = nullptr;
  void (*CompileShader)(unsigned) = nullptr;
  unsigned (*CreateProgram)(void) = nullptr;
  void (*AttachShader)(unsigned, unsigned) = nullptr;
  void (*LinkProgram)(unsigned) = nullptr;
  void (*UseProgram)(unsigned) = nullptr;
  int (*GetUniformLocation)(unsigned, const char*) = nullptr;
  void (*UniformMatrix4fv)(int, int, unsigned char, const float*) = nullptr;
  void (*GenTextures)(int, unsigned*) = nullptr;
  void (*BindTexture)(unsigned, unsigned) = nullptr;
  void (*TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned,
                     const void*) = nullptr;
  void (*GenFramebuffers)(int, unsigned*) = nullptr;
  void (*BindFramebuffer)(unsigned, unsigned) = nullptr;
  void (*FramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned,
                               int) = nullptr;
  void (*DrawArrays)(unsigned, int, int) = nullptr;
  unsigned (*GetGLError)(void) = nullptr;
};

bool LoadDylib(Dylib& d, std::string& missing) {
  d.lib = dlopen(TGLES_HOST_LIBRARY_PATH, RTLD_NOW);
  if (d.lib == nullptr) {
    missing = dlerror() ? dlerror() : "dlopen failed";
    return false;
  }
#define REQ_DSYM(field, name)                                             \
  d.field = reinterpret_cast<decltype(d.field)>(dlsym(d.lib, #name));     \
  if (d.field == nullptr) {                                               \
    missing = "dlsym missing: " #name;                                    \
    return false;                                                         \
  }
  REQ_DSYM(GetDisplay, eglGetDisplay)
  REQ_DSYM(Initialize, eglInitialize)
  REQ_DSYM(ChooseConfig, eglChooseConfig)
  REQ_DSYM(CreateContext, eglCreateContext)
  REQ_DSYM(CreatePbufferSurface, eglCreatePbufferSurface)
  REQ_DSYM(MakeCurrent, eglMakeCurrent)
  REQ_DSYM(SwapBuffers, eglSwapBuffers)
  REQ_DSYM(GetProcAddress, eglGetProcAddress)
  REQ_DSYM(AttachLayer, tglHostAttachMetalLayer)
  REQ_DSYM(HostPresent, tglHostPresent)
  REQ_DSYM(ReadbackPixel, tglHostReadbackPixel)
#undef REQ_DSYM
  // GLES strictly through eglGetProcAddress (MobileGL route).
#define REQ_PROC(field, name)                                               \
  d.field = reinterpret_cast<decltype(d.field)>(d.GetProcAddress(#name));   \
  if (d.field == nullptr) {                                                 \
    missing = "eglGetProcAddress NULL: " #name;                             \
    return false;                                                           \
  }
  REQ_PROC(GenBuffers, glGenBuffers)
  REQ_PROC(BindBuffer, glBindBuffer)
  REQ_PROC(BufferData, glBufferData)
  REQ_PROC(GenVertexArrays, glGenVertexArrays)
  REQ_PROC(BindVertexArray, glBindVertexArray)
  REQ_PROC(EnableVertexAttribArray, glEnableVertexAttribArray)
  REQ_PROC(VertexAttribPointer, glVertexAttribPointer)
  REQ_PROC(CreateShader, glCreateShader)
  REQ_PROC(ShaderSource, glShaderSource)
  REQ_PROC(CompileShader, glCompileShader)
  REQ_PROC(CreateProgram, glCreateProgram)
  REQ_PROC(AttachShader, glAttachShader)
  REQ_PROC(LinkProgram, glLinkProgram)
  REQ_PROC(UseProgram, glUseProgram)
  REQ_PROC(GetUniformLocation, glGetUniformLocation)
  REQ_PROC(UniformMatrix4fv, glUniformMatrix4fv)
  REQ_PROC(GenTextures, glGenTextures)
  REQ_PROC(BindTexture, glBindTexture)
  REQ_PROC(TexImage2D, glTexImage2D)
  REQ_PROC(GenFramebuffers, glGenFramebuffers)
  REQ_PROC(BindFramebuffer, glBindFramebuffer)
  REQ_PROC(FramebufferTexture2D, glFramebufferTexture2D)
  REQ_PROC(DrawArrays, glDrawArrays)
  REQ_PROC(GetGLError, glGetError)
#undef REQ_PROC
  return true;
}

}  // namespace

TEST(DylibPresent, RedTriangleThroughBuiltLibrary) {
  Dylib d;
  std::string missing;
  EXPECT_TRUE(LoadDylib(d, missing));
  if (d.lib == nullptr) return;
  EXPECT_TRUE(missing.empty());
  // tglHost* must ALSO resolve via procaddr (single dispatch table rule).
  EXPECT_TRUE(d.GetProcAddress("tglHostPresent") != nullptr);
  EXPECT_TRUE(d.GetProcAddress("tglHostAttachMetalLayer") != nullptr);
  EXPECT_TRUE(d.GetProcAddress("glTotallyMadeUpName") == nullptr);

  Hdpy dpy = d.GetDisplay(nullptr);
  EXPECT_TRUE(dpy != nullptr);
  if (dpy == nullptr) {
    dlclose(d.lib);
    return;
  }
  EXPECT_TRUE(d.Initialize(dpy, nullptr, nullptr));
  const int cfgAttr[] = {0x3038};
  Hcfg cfg = nullptr;
  int ncfg = 0;
  EXPECT_TRUE(d.ChooseConfig(dpy, cfgAttr, &cfg, 1, &ncfg));
  const int ctxAttr[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  Hctx ctx = d.CreateContext(dpy, cfg, nullptr, ctxAttr);
  EXPECT_TRUE(ctx != nullptr);
  const int pbAttr[] = {0x3057, 16, 0x3056, 16, 0x3038};
  Hsurf surf = d.CreatePbufferSurface(dpy, cfg, pbAttr);
  EXPECT_TRUE(surf != nullptr);
  EXPECT_TRUE(d.MakeCurrent(dpy, surf, surf, ctx));

  // Headless layer (no window): proves the in-dylib bridge presents.
  CAMetalLayer* layer = [CAMetalLayer layer];
  EXPECT_TRUE(d.AttachLayer((__bridge void*)layer, 16, 16));

  // Red fullscreen triangle (pos@0 + color@1, MVP identity via uniform).
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  unsigned pb = 0, cb = 0;
  d.GenBuffers(1, &pb);
  d.GenBuffers(1, &cb);
  d.BindBuffer(0x8892u, pb);
  d.BufferData(0x8892u, sizeof(kPos), kPos, 0x88E4u);
  d.BindBuffer(0x8892u, cb);
  d.BufferData(0x8892u, sizeof(kCol), kCol, 0x88E4u);
  unsigned vao = 0;
  d.GenVertexArrays(1, &vao);
  d.BindVertexArray(vao);
  d.BindBuffer(0x8892u, pb);
  d.VertexAttribPointer(0, 3, 0x1406u, 0, 0, nullptr);
  d.BindBuffer(0x8892u, cb);
  d.VertexAttribPointer(1, 4, 0x1406u, 0, 0, nullptr);
  d.EnableVertexAttribArray(0);
  d.EnableVertexAttribArray(1);
  const char* vs =
      "#version 320 es\nuniform mat4 u_modelViewProj;\nvoid main() {}";
  const char* fs = "#version 320 es\nvoid main() {}";
  unsigned v = d.CreateShader(0x8B31u);
  d.ShaderSource(v, 1, &vs, nullptr);
  d.CompileShader(v);
  unsigned f = d.CreateShader(0x8B30u);
  d.ShaderSource(f, 1, &fs, nullptr);
  d.CompileShader(f);
  unsigned prog = d.CreateProgram();
  d.AttachShader(prog, v);
  d.AttachShader(prog, f);
  d.LinkProgram(prog);
  d.UseProgram(prog);
  const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                               0, 0, 1, 0, 0, 0, 0, 1};
  int mvpLoc = d.GetUniformLocation(prog, "u_modelViewProj");
  EXPECT_TRUE(mvpLoc >= 0);
  d.UniformMatrix4fv(mvpLoc, 1, 0, kIdentity);
  unsigned tex = 0, fbo = 0;
  d.GenTextures(1, &tex);
  d.BindTexture(0x0DE1u, tex);
  d.TexImage2D(0x0DE1u, 0, 0x8058, 16, 16, 0, 0x1908u, 0x1401u, nullptr);
  d.GenFramebuffers(1, &fbo);
  d.BindFramebuffer(0x8CA9u, fbo);
  d.FramebufferTexture2D(0x8CA9u, 0x8CE0u, 0x0DE1u, tex, 0);
  EXPECT_EQ(d.GetGLError(), 0u);

  d.DrawArrays(0x0004u, 0, 3);
  EXPECT_EQ(d.GetGLError(), 0u);
  d.SwapBuffers(dpy, surf);
  EXPECT_TRUE(d.HostPresent());
  unsigned char px[4] = {0};
  EXPECT_TRUE(d.ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
  dlclose(d.lib);
}

#else

TEST(DylibPresent, NoQuartzHostSkips) { EXPECT_TRUE(true); }

#endif
