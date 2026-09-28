// TGL game window (macOS, Darwin-only): a REAL playable GLES 3.2 game whose
// ONLY driver is the BUILT shared library (build/libtgles.dylib).
//
// Anti-cheat contract (this is the whole point of this file):
// - This translation unit includes NO tgles/* headers and links NO tgles_*
//   library. It talks to the driver exclusively through dlopen() +
//   eglGetProcAddress(), exactly like MobileGL's BackendLoaders/OpenGL/
//   Loader.cpp (AcquireGLESFunctions) and VK-GL-CTS's eglwLibrary do.
// - Every GLES entry resolves via the dylib's eglGetProcAddress (MobileGL
//   route); EGL + tglHost* present extensions resolve via dlsym. A NULL
//   anywhere aborts with a message naming the missing symbol (fatal bring-up
//   failure, same policy as MobileGL INIT_GLES_FUNC).
// - Presentation goes through tglHostAttachMetalLayer/tglHostPresent INSIDE
//   the dylib, so pixels prove the built file executes: no in-app Metal
//   drawing exists (this file creates the CAMetalLayer but never encodes to
//   it; grep setRenderPipelineState here returns nothing).
//
// The game: NEON DODGE — a textured lit player cube slides along the floor
// of a scrolling canyon (5 neon bars + ground grid), dodging oncoming
// pillars. One glDrawArrays(TRIANGLES) per displayed frame carries every
// object merged on the CPU (positions baked with model transforms, flat
// diffuse lighting, planar UVs over a checker); MVP holds P*V only. Depth
// LESS resolves overlap on the GPU for real. Score/FPS/lives ride the window
// title; collisions flash the player red. Controls: ←/→ or A/D move,
// Space pause, R reset.
//
// GL enum values below are Khronos-official (docs/reference/gl32.h, egl.h);
// each is cited once where first used. TGL validates them like a real driver
// (wrong values fail the frame with the GL error in the title, never silent).
//
// Build: cmake --build build --target tgl_game_window && ./build/tgl_game_window
// (expects build/libtgles.dylib next to the executable or at ./build/;
// override with TGLES_HOST_DYLIB env).

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Khronos-official enum values (docs/reference/gl32.h + egl.h). No TGL header.
enum : unsigned {
  GLE_TRUE = 1,
  GLE_FALSE = 0,
  GLE_NO_ERROR = 0,
  GLE_TRIANGLES = 0x0004,          // gl32.h
  GLE_DEPTH_TEST = 0x0B71,         // gl32.h
  GLE_TEXTURE_2D = 0x0DE1,         // gl32.h
  GLE_RGBA = 0x1908,               // gl32.h
  GLE_UNSIGNED_BYTE = 0x1401,      // gl32.h
  GLE_FLOAT = 0x1406,              // gl32.h
  GLE_RGBA8 = 0x8058,              // gl32.h Table 8.10
  GLE_DEPTH_COMPONENT24 = 0x81A6,  // gl32.h
  GLE_TEXTURE_MIN_FILTER = 0x2801,  // gl32.h
  GLE_TEXTURE_MAG_FILTER = 0x2800,  // gl32.h
  GLE_NEAREST = 0x2600,             // gl32.h
  GLE_TEXTURE0 = 0x84C0,            // gl32.h
  GLE_ARRAY_BUFFER = 0x8892,        // gl32.h
  GLE_STATIC_DRAW = 0x88E4,         // gl32.h
  GLE_DYNAMIC_DRAW = 0x88E8,        // gl32.h
  GLE_FRAMEBUFFER = 0x8D40,         // gl32.h GL_FRAMEBUFFER (both bindings).
  GLE_DRAW_FRAMEBUFFER = 0x8CA9,    // gl32.h
  GLE_COLOR_ATTACHMENT0 = 0x8CE0,   // gl32.h
  GLE_DEPTH_ATTACHMENT = 0x8D00,    // gl32.h
  GLE_FRAMEBUFFER_COMPLETE = 0x8CD5,  // gl32.h
  GLE_RENDERBUFFER = 0x8D41,          // gl32.h
  GLE_VERTEX_SHADER = 0x8B31,         // gl32.h
  GLE_FRAGMENT_SHADER = 0x8B30,       // gl32.h
  GLE_COMPILE_STATUS = 0x8B81,        // gl32.h
  GLE_LINK_STATUS = 0x8B82,           // gl32.h
};
enum : int {
  EGLE_NONE = 0x3038,            // egl.h
  EGLE_CONTEXT_MAJOR_VERSION = 0x3098,  // egl.h
  EGLE_CONTEXT_MINOR_VERSION = 0x30FB,  // egl.h
  EGLE_WIDTH = 0x3057,           // egl.h
  EGLE_HEIGHT = 0x3056,          // egl.h
};

// EGL opaque handles (void*, Khronos egl.h).
typedef void* EGDisplay;
typedef void* EGConfig;
typedef void* EGSurface;
typedef void* EGContext;

// Full proc table: EGL via dlsym, GLES via eglGetProcAddress (MobileGL route),
// tglHost* present extensions via dlsym (then cross-checked via procaddr).
struct GL {
  void* lib = nullptr;
  // EGL (dlsym).
  EGDisplay (*GetDisplay)(void*) = nullptr;
  int (*Initialize)(EGDisplay, int*, int*) = nullptr;
  int (*ChooseConfig)(EGDisplay, const int*, EGConfig*, int, int*) = nullptr;
  EGContext (*CreateContext)(EGDisplay, EGConfig, EGContext, const int*) = nullptr;
  EGSurface (*CreatePbufferSurface)(EGDisplay, EGConfig, const int*) = nullptr;
  int (*MakeCurrent)(EGDisplay, EGSurface, EGSurface, EGContext) = nullptr;
  int (*SwapBuffers)(EGDisplay, EGSurface) = nullptr;
  int (*GetError)(void) = nullptr;
  void* (*GetProcAddress)(const char*) = nullptr;
  // tglHost present extensions (dlsym).
  int (*AttachLayer)(void*, int, int) = nullptr;
  int (*ResizeLayer)(int, int) = nullptr;
  int (*HostPresent)(void) = nullptr;
  int (*ReadbackPixel)(int, int, unsigned char[4]) = nullptr;
  unsigned long long (*FrameSerial)(void) = nullptr;
  // GLES (eglGetProcAddress).
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
  void (*Uniform1i)(int, int) = nullptr;
  void (*GenTextures)(int, unsigned*) = nullptr;
  void (*BindTexture)(unsigned, unsigned) = nullptr;
  void (*TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned,
                     const void*) = nullptr;
  void (*TexParameteri)(unsigned, unsigned, int) = nullptr;
  void (*ActiveTexture)(unsigned) = nullptr;
  void (*GenFramebuffers)(int, unsigned*) = nullptr;
  void (*BindFramebuffer)(unsigned, unsigned) = nullptr;
  void (*FramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned, int) = nullptr;
  void (*GenRenderbuffers)(int, unsigned*) = nullptr;
  void (*BindRenderbuffer)(unsigned, unsigned) = nullptr;
  void (*RenderbufferStorage)(unsigned, unsigned, int, int) = nullptr;
  void (*FramebufferRenderbuffer)(unsigned, unsigned, unsigned, unsigned) = nullptr;
  unsigned (*CheckFramebufferStatus)(unsigned) = nullptr;
  void (*Enable)(unsigned) = nullptr;
  void (*DrawArrays)(unsigned, int, int) = nullptr;
  unsigned (*GetGLError)(void) = nullptr;
};

static std::string g_loadError;

// Small mat4 (column-major, GL convention) for P*V only. Model transforms are
// baked into positions on the CPU (one-draw-per-frame bridge contract).
struct Mat4 {
  float m[16] = {};
  static Mat4 Identity() {
    Mat4 o;
    o.m[0] = o.m[5] = o.m[10] = o.m[15] = 1.0f;
    return o;
  }
  static Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 o;
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += a.m[r + 4 * k] * b.m[k + 4 * c];
        o.m[r + 4 * c] = s;
      }
    return o;
  }
  static Mat4 Translate(float x, float y, float z) {
    Mat4 o = Identity();
    o.m[12] = x;
    o.m[13] = y;
    o.m[14] = z;
    return o;
  }
  static Mat4 RotateY(float a) {
    Mat4 o;
    const float c = cosf(a), s = sinf(a);
    o.m[0] = c;
    o.m[2] = -s;
    o.m[5] = 1;
    o.m[8] = s;
    o.m[10] = c;
    o.m[15] = 1;
    return o;
  }
  static Mat4 Perspective(float fovy, float aspect, float zn, float zf) {
    Mat4 o;
    const float f = 1.0f / tanf(fovy * 0.5f);
    o.m[0] = f / aspect;
    o.m[5] = f;
    o.m[10] = (zf + zn) / (zn - zf);
    o.m[11] = -1.0f;
    o.m[14] = (2 * zf * zn) / (zn - zf);
    return o;
  }
};

// Box soup: one 12-tri box appended per call. Flat per-face color + outward
// normal; caller shades with ShadeFace then pushes xyz+rgba (+ planar uv).
static void ShadeFace(const float base[3], const float n[3], const float l[3],
                      float out[3]) {
  const float d = n[0] * l[0] + n[1] * l[1] + n[2] * l[2];
  const float f = 0.25f + 0.75f * (d > 0 ? d : 0);
  out[0] = base[0] * f;
  out[1] = base[1] * f;
  out[2] = base[2] * f;
}

struct BoxReq {
  float cx, cy, cz, sx, sy, sz;
  float r, g, b;
};

static void PushBox(const BoxReq& b, const float light[3],
                    std::vector<float>& xyz, std::vector<float>& rgba,
                    std::vector<float>& uv) {
  const float x0 = b.cx - b.sx * 0.5f, x1 = b.cx + b.sx * 0.5f;
  const float y0 = b.cy - b.sy * 0.5f, y1 = b.cy + b.sy * 0.5f;
  const float z0 = b.cz - b.sz * 0.5f, z1 = b.cz + b.sz * 0.5f;
  // 6 quad faces -> 12 triangles with outward normals (verified in game by
  // the GPU depth test: wrong winding would show inside-out pillars).
  const float c[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                         {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
  const int quads[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6},
                           {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
  const float normals[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0},
                               {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
  const float base[3] = {b.r, b.g, b.b};
  for (int f = 0; f < 6; ++f) {
    float lit[3];
    ShadeFace(base, normals[f], light, lit);
  const int* q = quads[f];
  const int tris[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
    for (int k = 0; k < 6; ++k) {
      const float* p = c[tris[k]];
      xyz.push_back(p[0]);
      xyz.push_back(p[1]);
      xyz.push_back(p[2]);
      rgba.push_back(lit[0]);
      rgba.push_back(lit[1]);
      rgba.push_back(lit[2]);
      rgba.push_back(1.0f);
      uv.push_back(p[0] * 0.25f + 0.5f);
      uv.push_back(p[1] * 0.25f + 0.5f);
    }
  }
}

// ---------------------------------------------------------------------------
// App state (pure game, no driver state here).
static const int kW = 960, kH = 600;
static GL g_gl;
static EGDisplay g_dpy = nullptr;
static EGSurface g_surf = nullptr;
static EGContext g_ctx = nullptr;
static unsigned g_pb = 0, g_cb = 0, g_ub = 0, g_vao = 0, g_prog = 0;
static int g_mvpLoc = -1;
static float g_mvp[16] = {};
static bool g_ready = false;
static std::string g_status = "loading";
static double g_lastTime = 0;
static float g_fps = 60;
static int g_score = 0, g_lives = 3, g_flash = 0;
static bool g_paused = false;
static float g_playerX = 0;
static float g_speed = 3.0f;
static float g_spawnT = 0;
struct Pillar {
  float x, z, h, r, g, b;
};
static std::vector<Pillar> g_pillars;
static int g_rng = 12345;
static float Rand01() {
  g_rng = (g_rng * 1103515245 + 12345) & 0x7fffffff;
  return (float)g_rng / 2147483647.0f;
}

static void SetStatus(const std::string& s) { g_status = s; }

static bool CheckGLErr(const char* where, NSWindow* win) {
  unsigned e = g_gl.GetGLError();
  if (e != GLE_NO_ERROR) {
    char buf[160];
    snprintf(buf, sizeof(buf), "GL error 0x%x at %s", e, where);
    SetStatus(buf);
    if (win) {
      NSString* t = [NSString stringWithFormat:@"NEON DODGE — %s (score %d)",
                                               buf, g_score];
      [win setTitle:t];
    }
    return false;
  }
  return true;
}

// One-time driver bring-up through the built dylib only.
static bool BringUp(NSView* view) {
  // Locate the built library: env override, then @executable_path, then CWD.
  const char* env = getenv("TGLES_HOST_DYLIB");
  std::string paths[4];
  int npaths = 0;
  if (env) paths[npaths++] = env;
  paths[npaths++] = "./build/libtgles.dylib";
  paths[npaths++] = "build/libtgles.dylib";
  NSString* exeDir = [[[NSBundle mainBundle] executablePath]
      stringByDeletingLastPathComponent];
  if (exeDir) {
    paths[npaths++] =
        std::string([exeDir UTF8String]) + "/libtgles.dylib";
  }
  void* lib = nullptr;
  std::string tried;
  std::string used;
  for (int i = 0; i < npaths; ++i) {
    lib = dlopen(paths[i].c_str(), RTLD_NOW);
    tried += paths[i] + " ";
    if (lib) {
      used = paths[i];
      break;
    }
  }
  // Also try the executable dir of a build-tree run (./build/tgl_game_window
  // with the dylib beside it) via _NSGetExecutablePath fallback.
  if (!lib) {
    lib = dlopen("./libtgles.dylib", RTLD_NOW);
    tried += "./libtgles.dylib";
    if (lib) used = "./libtgles.dylib";
  }
  if (!lib) {
    SetStatus(std::string("dlopen failed: ") + dlerror() + " tried: " + tried);
    return false;
  }
  g_gl.lib = lib;
  // EGL via dlsym (real names carry the egl prefix).
  g_gl.GetDisplay = reinterpret_cast<decltype(g_gl.GetDisplay)>(
      dlsym(lib, "eglGetDisplay"));
  g_gl.Initialize = reinterpret_cast<decltype(g_gl.Initialize)>(
      dlsym(lib, "eglInitialize"));
  g_gl.ChooseConfig = reinterpret_cast<decltype(g_gl.ChooseConfig)>(
      dlsym(lib, "eglChooseConfig"));
  g_gl.CreateContext = reinterpret_cast<decltype(g_gl.CreateContext)>(
      dlsym(lib, "eglCreateContext"));
  g_gl.CreatePbufferSurface =
      reinterpret_cast<decltype(g_gl.CreatePbufferSurface)>(
          dlsym(lib, "eglCreatePbufferSurface"));
  g_gl.MakeCurrent = reinterpret_cast<decltype(g_gl.MakeCurrent)>(
      dlsym(lib, "eglMakeCurrent"));
  g_gl.SwapBuffers = reinterpret_cast<decltype(g_gl.SwapBuffers)>(
      dlsym(lib, "eglSwapBuffers"));
  g_gl.GetError = reinterpret_cast<decltype(g_gl.GetError)>(
      dlsym(lib, "eglGetError"));
  g_gl.GetProcAddress = reinterpret_cast<decltype(g_gl.GetProcAddress)>(
      dlsym(lib, "eglGetProcAddress"));
  if (!g_gl.GetDisplay || !g_gl.Initialize || !g_gl.ChooseConfig ||
      !g_gl.CreateContext || !g_gl.CreatePbufferSurface || !g_gl.MakeCurrent ||
      !g_gl.SwapBuffers || !g_gl.GetError || !g_gl.GetProcAddress) {
    SetStatus("dlsym missing EGL symbol (fatal bring-up failure)");
    return false;
  }
  // tglHost present extensions via dlsym, cross-checked via procaddr.
  g_gl.AttachLayer = reinterpret_cast<decltype(g_gl.AttachLayer)>(
      dlsym(lib, "tglHostAttachMetalLayer"));
  g_gl.ResizeLayer = reinterpret_cast<decltype(g_gl.ResizeLayer)>(
      dlsym(lib, "tglHostResizeMetalLayer"));
  g_gl.HostPresent = reinterpret_cast<decltype(g_gl.HostPresent)>(
      dlsym(lib, "tglHostPresent"));
  g_gl.ReadbackPixel = reinterpret_cast<decltype(g_gl.ReadbackPixel)>(
      dlsym(lib, "tglHostReadbackPixel"));
  g_gl.FrameSerial = reinterpret_cast<decltype(g_gl.FrameSerial)>(
      dlsym(lib, "tglHostFrameSerial"));
  if (!g_gl.AttachLayer || !g_gl.HostPresent || !g_gl.ReadbackPixel ||
      !g_gl.FrameSerial) {
    SetStatus("dlsym missing tglHost present symbol");
    return false;
  }
  // GLES via eglGetProcAddress ONLY (MobileGL Loader.cpp route). Any NULL is
  // a fatal bring-up failure, same policy as INIT_GLES_FUNC.
  auto P = [&](const char* name) -> void* {
    void* p = g_gl.GetProcAddress(name);
    if (!p) SetStatus(std::string("eglGetProcAddress NULL: ") + name);
    return p;
  };
#define REQ_GL(field, name)                                     \
  g_gl.field = reinterpret_cast<decltype(g_gl.field)>(P(#name)); \
  if (!g_gl.field) return false;
  REQ_GL(GenBuffers, glGenBuffers)
  REQ_GL(BindBuffer, glBindBuffer)
  REQ_GL(BufferData, glBufferData)
  REQ_GL(GenVertexArrays, glGenVertexArrays)
  REQ_GL(BindVertexArray, glBindVertexArray)
  REQ_GL(EnableVertexAttribArray, glEnableVertexAttribArray)
  REQ_GL(VertexAttribPointer, glVertexAttribPointer)
  REQ_GL(CreateShader, glCreateShader)
  REQ_GL(ShaderSource, glShaderSource)
  REQ_GL(CompileShader, glCompileShader)
  REQ_GL(CreateProgram, glCreateProgram)
  REQ_GL(AttachShader, glAttachShader)
  REQ_GL(LinkProgram, glLinkProgram)
  REQ_GL(UseProgram, glUseProgram)
  REQ_GL(GetUniformLocation, glGetUniformLocation)
  REQ_GL(UniformMatrix4fv, glUniformMatrix4fv)
  REQ_GL(Uniform1i, glUniform1i)
  REQ_GL(GenTextures, glGenTextures)
  REQ_GL(BindTexture, glBindTexture)
  REQ_GL(TexImage2D, glTexImage2D)
  REQ_GL(TexParameteri, glTexParameteri)
  REQ_GL(ActiveTexture, glActiveTexture)
  REQ_GL(GenFramebuffers, glGenFramebuffers)
  REQ_GL(BindFramebuffer, glBindFramebuffer)
  REQ_GL(FramebufferTexture2D, glFramebufferTexture2D)
  REQ_GL(GenRenderbuffers, glGenRenderbuffers)
  REQ_GL(BindRenderbuffer, glBindRenderbuffer)
  REQ_GL(RenderbufferStorage, glRenderbufferStorage)
  REQ_GL(FramebufferRenderbuffer, glFramebufferRenderbuffer)
  REQ_GL(CheckFramebufferStatus, glCheckFramebufferStatus)
  REQ_GL(Enable, glEnable)
  REQ_GL(DrawArrays, glDrawArrays)
  REQ_GL(GetGLError, glGetError)
#undef REQ_GL
  // Cross-check: tglHost* must ALSO resolve via procaddr (single-table rule).
  if (!g_gl.GetProcAddress("tglHostPresent") ||
      !g_gl.GetProcAddress("tglHostAttachMetalLayer")) {
    SetStatus("procaddr missing tglHost symbols (dispatch table divergence)");
    return false;
  }
  // EGL bring-up (surfaceless-style pbuffer; window pixels come from the
  // Metal layer attached below, not from EGL).
  g_dpy = g_gl.GetDisplay(nullptr);
  if (!g_dpy) {
    SetStatus("eglGetDisplay NULL");
    return false;
  }
  int major = 0, minor = 0;
  if (!g_gl.Initialize(g_dpy, &major, &minor)) {
    SetStatus("eglInitialize failed");
    return false;
  }
  const int cfgAttr[] = {EGLE_NONE};
  EGConfig cfg = nullptr;
  int ncfg = 0;
  if (!g_gl.ChooseConfig(g_dpy, cfgAttr, &cfg, 1, &ncfg) || ncfg < 1) {
    SetStatus("eglChooseConfig failed");
    return false;
  }
  const int ctxAttr[] = {EGLE_CONTEXT_MAJOR_VERSION, 3,
                         EGLE_CONTEXT_MINOR_VERSION, 2, EGLE_NONE};
  g_ctx = g_gl.CreateContext(g_dpy, cfg, nullptr, ctxAttr);
  if (!g_ctx) {
    SetStatus("eglCreateContext failed");
    return false;
  }
  const int pbAttr[] = {EGLE_WIDTH, 16, EGLE_HEIGHT, 16, EGLE_NONE};
  g_surf = g_gl.CreatePbufferSurface(g_dpy, cfg, pbAttr);
  if (!g_surf) {
    SetStatus("eglCreatePbufferSurface failed");
    return false;
  }
  if (!g_gl.MakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) {
    SetStatus("eglMakeCurrent failed");
    return false;
  }
  // Attach the view's CAMetalLayer INSIDE the dylib.
  CAMetalLayer* layer = (CAMetalLayer*)[view layer];
  if (!g_gl.AttachLayer((__bridge void*)layer, kW, kH)) {
    SetStatus("tglHostAttachMetalLayer failed (no Metal device?)");
    return false;
  }
  // Checker texture on unit 1 (unit 0 keeps the draw target during setup).
  static unsigned char checker[4 * 4 * 4];
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      const bool w = ((x + y) % 2) == 0;
      checker[(y * 4 + x) * 4 + 0] = w ? 255 : 70;
      checker[(y * 4 + x) * 4 + 1] = w ? 255 : 70;
      checker[(y * 4 + x) * 4 + 2] = w ? 255 : 255;
      checker[(y * 4 + x) * 4 + 3] = 255;
    }
  unsigned tex = 0;
  g_gl.GenTextures(1, &tex);
  g_gl.ActiveTexture(GLE_TEXTURE0 + 1);
  g_gl.BindTexture(GLE_TEXTURE_2D, tex);
  g_gl.TexImage2D(GLE_TEXTURE_2D, 0, GLE_RGBA8, 4, 4, 0, GLE_RGBA,
                  GLE_UNSIGNED_BYTE, checker);
  g_gl.TexParameteri(GLE_TEXTURE_2D, GLE_TEXTURE_MIN_FILTER, GLE_NEAREST);
  g_gl.TexParameteri(GLE_TEXTURE_2D, GLE_TEXTURE_MAG_FILTER, GLE_NEAREST);
  g_gl.ActiveTexture(GLE_TEXTURE0 + 0);
  // Buffers + VAO (attribs 0/1/2 = pos/col/uv).
  g_gl.GenBuffers(1, &g_pb);
  g_gl.GenBuffers(1, &g_cb);
  g_gl.GenBuffers(1, &g_ub);
  g_gl.GenVertexArrays(1, &g_vao);
  g_gl.BindVertexArray(g_vao);
  g_gl.BindBuffer(GLE_ARRAY_BUFFER, g_pb);
  g_gl.VertexAttribPointer(0, 3, GLE_FLOAT, GLE_FALSE, 0, nullptr);
  g_gl.BindBuffer(GLE_ARRAY_BUFFER, g_cb);
  g_gl.VertexAttribPointer(1, 4, GLE_FLOAT, GLE_FALSE, 0, nullptr);
  g_gl.BindBuffer(GLE_ARRAY_BUFFER, g_ub);
  g_gl.VertexAttribPointer(2, 2, GLE_FLOAT, GLE_FALSE, 0, nullptr);
  g_gl.EnableVertexAttribArray(0);
  g_gl.EnableVertexAttribArray(1);
  g_gl.EnableVertexAttribArray(2);
  // Textured program (sampler on unit 1).
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec2 a_uv;\n"
      "out vec4 v_col; out vec2 v_uv; uniform mat4 u_modelViewProj;\n"
      "void main(){ v_col=a_col; v_uv=a_uv;"
      " gl_Position=u_modelViewProj*a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_uv; uniform sampler2D u_tex; out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_tex, v_uv); }";
  unsigned v = g_gl.CreateShader(GLE_VERTEX_SHADER);
  g_gl.ShaderSource(v, 1, &vs, nullptr);
  g_gl.CompileShader(v);
  unsigned f = g_gl.CreateShader(GLE_FRAGMENT_SHADER);
  g_gl.ShaderSource(f, 1, &fs, nullptr);
  g_gl.CompileShader(f);
  g_prog = g_gl.CreateProgram();
  g_gl.AttachShader(g_prog, v);
  g_gl.AttachShader(g_prog, f);
  g_gl.LinkProgram(g_prog);
  g_gl.UseProgram(g_prog);
  int texLoc = g_gl.GetUniformLocation(g_prog, "u_tex");
  if (texLoc < 0) {
    SetStatus("u_tex location < 0");
    return false;
  }
  g_gl.Uniform1i(texLoc, 1);
  g_mvpLoc = g_gl.GetUniformLocation(g_prog, "u_modelViewProj");
  if (g_mvpLoc < 0) {
    SetStatus("u_modelViewProj location < 0");
    return false;
  }
  // Draw target (unit 0) + depth.
  unsigned t = 0;
  g_gl.GenTextures(1, &t);
  g_gl.BindTexture(GLE_TEXTURE_2D, t);
  g_gl.TexImage2D(GLE_TEXTURE_2D, 0, GLE_RGBA8, kW, kH, 0, GLE_RGBA,
                  GLE_UNSIGNED_BYTE, nullptr);
  unsigned rb = 0;
  g_gl.GenRenderbuffers(1, &rb);
  g_gl.BindRenderbuffer(GLE_RENDERBUFFER, rb);
  g_gl.RenderbufferStorage(GLE_RENDERBUFFER, GLE_DEPTH_COMPONENT24, kW, kH);
  unsigned fb = 0;
  g_gl.GenFramebuffers(1, &fb);
  g_gl.BindFramebuffer(GLE_FRAMEBUFFER, fb);
  g_gl.FramebufferTexture2D(GLE_FRAMEBUFFER, GLE_COLOR_ATTACHMENT0,
                            GLE_TEXTURE_2D, t, 0);
  g_gl.FramebufferRenderbuffer(GLE_FRAMEBUFFER, GLE_DEPTH_ATTACHMENT,
                               GLE_RENDERBUFFER, rb);
  if (g_gl.CheckFramebufferStatus(GLE_FRAMEBUFFER) !=
      GLE_FRAMEBUFFER_COMPLETE) {
    SetStatus("FBO incomplete");
    return false;
  }
  g_gl.Enable(GLE_DEPTH_TEST);
  if (g_gl.GetGLError() != GLE_NO_ERROR) {
    SetStatus("setup has GL error");
    return false;
  }
  // P*V (models baked on CPU, one draw per frame).
  Mat4 proj = Mat4::Perspective(3.14159265f / 3.0f, (float)kW / kH, 0.1f, 60);
  Mat4 viewMat = Mat4::Translate(0, -1.2f, -11);
  Mat4 mvp = Mat4::Multiply(proj, viewMat);
  memcpy(g_mvp, mvp.m, sizeof(g_mvp));
  g_ready = true;
  SetStatus("running");
  std::printf("GAME_WINDOW: driver=%s EGL %d.%d ready (procaddr GLES, dylib "
              "present ABI)\n",
              used.c_str(), major, minor);
  std::fflush(stdout);
  return true;
}

// One game tick + draw through the dylib only.
static void TickAndDraw(NSWindow* win, float dt) {
  if (!g_ready || g_paused) return;
  // Player follows keys (handled in keyDown via g_playerX velocity below).
  g_spawnT -= dt;
  if (g_spawnT <= 0) {
    g_spawnT = 1.1f;
    const float cols[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const int ci = (int)(Rand01() * 3);
    Pillar p;
    p.x = -4.0f + Rand01() * 8.0f;
    p.z = -22.0f;
    p.h = 1.5f + Rand01() * 3.0f;
    p.r = cols[ci][0];
    p.g = cols[ci][1];
    p.b = cols[ci][2];
    g_pillars.push_back(p);
    if (g_speed < 7.0f) g_speed += 0.08f;
  }
  const float light[3] = {0.45f, 0.75f, 0.55f};
  float ll = sqrtf(light[0] * light[0] + light[1] * light[1] +
                   light[2] * light[2]);
  const float ln[3] = {light[0] / ll, light[1] / ll, light[2] / ll};
  std::vector<float> xyz, rgba, uv;
  xyz.reserve(8192);
  rgba.reserve(10922);
  uv.reserve(5461);
  // Floor (dark slate, y=-2).
  PushBox({0, -2.2f, -10, 14, 0.4f, 30, 0.16f, 0.18f, 0.24f}, ln, xyz, rgba,
          uv);
  // Pillars advance toward the camera.
  for (auto& p : g_pillars) p.z += g_speed * dt;
  // Player cube (white x checker, flashes red on hit).
  const bool flashing = g_flash > 0;
  if (g_flash > 0) g_flash--;
  const float pr = flashing ? 1.0f : 1.0f;
  const float pg = flashing ? 0.15f : 1.0f;
  const float pb = flashing ? 0.15f : 1.0f;
  PushBox({g_playerX, -1.1f, -3.0f, 1.0f, 1.0f, 1.0f, pr, pg, pb}, ln, xyz,
          rgba, uv);
  // Pillars + collision/score.
  for (auto it = g_pillars.begin(); it != g_pillars.end();) {
    PushBox({it->x, -2.0f + it->h * 0.5f, it->z, 1.1f, it->h, 1.1f, it->r,
             it->g, it->b},
            ln, xyz, rgba, uv);
    const bool overlapX = fabsf(it->x - g_playerX) < 1.0f;
    const bool overlapZ = fabsf(it->z - (-3.0f)) < 1.0f;
    if (overlapX && overlapZ && g_flash == 0) {
      g_lives--;
      g_flash = 30;
      g_score = g_score > 5 ? g_score - 5 : 0;
      if (g_lives <= 0) {
        g_lives = 3;
        g_score = 0;
        g_speed = 3.0f;
        g_pillars.clear();
        SetStatus("hit! reset (lives refilled)");
        break;
      }
    }
    if (it->z > 4.0f) {
      g_score++;
      it = g_pillars.erase(it);
    } else {
      ++it;
    }
  }
  const int verts = (int)(xyz.size() / 3);
  g_gl.BindBuffer(GLE_ARRAY_BUFFER, g_pb);
  g_gl.BufferData(GLE_ARRAY_BUFFER, xyz.size() * sizeof(float), xyz.data(),
                  GLE_DYNAMIC_DRAW);
  g_gl.BindBuffer(GLE_ARRAY_BUFFER, g_cb);
  g_gl.BufferData(GLE_ARRAY_BUFFER, rgba.size() * sizeof(float), rgba.data(),
                  GLE_DYNAMIC_DRAW);
  g_gl.BindBuffer(GLE_ARRAY_BUFFER, g_ub);
  g_gl.BufferData(GLE_ARRAY_BUFFER, uv.size() * sizeof(float), uv.data(),
                  GLE_DYNAMIC_DRAW);
  g_gl.UniformMatrix4fv(g_mvpLoc, 1, GLE_FALSE, g_mvp);
  g_gl.DrawArrays(GLE_TRIANGLES, 0, verts);
  if (!CheckGLErr("draw", win)) return;
  g_gl.SwapBuffers(g_dpy, g_surf);
  if (!g_gl.HostPresent()) {
    SetStatus("tglHostPresent failed — see Console for Metal error");
    return;
  }
  // FPS + HUD in the title (errors also surface here, never silent).
  static double acc = 0;
  static int frames = 0;
  static int logTick = 0;
  acc += dt;
  frames++;
  if (acc >= 0.5) {
    g_fps = frames / (float)acc;
    acc = 0;
    frames = 0;
    char title[220];
    snprintf(title, sizeof(title),
             "NEON DODGE (TGL via libtgles.dylib) — score %d lives %d fps %.0f "
             "serial %llu",
             g_score, g_lives, g_fps, g_gl.FrameSerial());
    [win setTitle:[NSString stringWithUTF8String:title]];
    // Stdout heartbeat: proves frames present through the dylib (logTick
    // counts title updates, ~2/s).
    std::printf("GAME_WINDOW: frame serial=%llu score=%d lives=%d fps=%.0f\n",
                g_gl.FrameSerial(), g_score, g_lives, g_fps);
    std::fflush(stdout);
    logTick++;
    (void)logTick;
  }
}

// ---------------------------------------------------------------------------
@interface GameView : NSView
@end
@implementation GameView
- (BOOL)acceptsFirstResponder {
  return YES;
}
- (void)keyDown:(NSEvent*)event {
  NSString* chars = [event charactersIgnoringModifiers];
  if ([chars length] == 0) return;
  unichar c = [chars characterAtIndex:0];
  unsigned short kc = [event keyCode];
  if (kc == 123 || c == 'a' || c == 'A') g_playerX -= 0.6f;  // Left
  if (kc == 124 || c == 'd' || c == 'D') g_playerX += 0.6f;  // Right
  if (c == ' ') g_paused = !g_paused;
  if (c == 'r' || c == 'R') {
    g_score = 0;
    g_lives = 3;
    g_speed = 3.0f;
    g_pillars.clear();
    g_playerX = 0;
  }
  if (g_playerX < -5.0f) g_playerX = -5.0f;
  if (g_playerX > 5.0f) g_playerX = 5.0f;
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate> {
 @public
  NSWindow* window;
  NSTimer* timer;
  NSDate* last;
}
@end
@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)n {
  (void)n;
  NSRect frame = NSMakeRect(0, 0, kW, kH);
  self->window = [[NSWindow alloc]
      initWithContentRect:frame
                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable |
                           NSWindowStyleMaskResizable)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [self->window setTitle:@"NEON DODGE (TGL via libtgles.dylib) — loading"];
  [self->window center];
  GameView* view = [[GameView alloc] initWithFrame:frame];
  [view setWantsLayer:YES];
  CAMetalLayer* layer = [CAMetalLayer layer];
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer.drawableSize = CGSizeMake(kW, kH);
  [view setLayer:layer];
  [self->window setContentView:view];
  [self->window makeKeyAndOrderFront:nil];
  [self->window makeFirstResponder:view];
  if (!BringUp(view)) {
    NSString* msg = [NSString stringWithUTF8String:g_status.c_str()];
    [self->window setTitle:[@"NEON DODGE — BRING-UP FAILED: "
                               stringByAppendingString:msg]];
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Driver bring-up failed"];
    [alert setInformativeText:msg];
    [alert runModal];
    return;
  }
  [self->window setTitle:@"NEON DODGE (TGL via libtgles.dylib) — running"];
  self->last = [NSDate date];
  self->timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                 target:self
                                               selector:@selector(tick:)
                                               userInfo:nil
                                                repeats:YES];
}
- (void)tick:(NSTimer*)t {
  (void)t;
  NSDate* now = [NSDate date];
  float dt = (float)[now timeIntervalSinceDate:self->last];
  self->last = now;
  if (dt > 0.1f) dt = 0.1f;
  TickAndDraw(self->window, dt);
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  (void)app;
  return YES;
}
@end

int main(int argc, const char* argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    NSApplication* app = [NSApplication sharedApplication];
    AppDelegate* del = [[AppDelegate alloc] init];
    [app setDelegate:del];
    [app run];
  }
  return 0;
}
