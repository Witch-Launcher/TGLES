// EXT entry points through the real host ABI (dlopen, like MobileGL):
// all 16 optional names resolve + agree with eglGetProcAddress, and the new
// draws/views/aliases execute through a current context (validation-only
// without a bridge, real Mock-bridge draws where noted).

#include "test_framework.h"

#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "tgles/host/abi.h"

#ifndef TGLES_HOST_LIBRARY_PATH
#define TGLES_HOST_LIBRARY_PATH "./libtgles.dylib"
#endif

namespace {

struct Lib {
  explicit Lib(const char* path) : h(dlopen(path, RTLD_NOW)) {}
  ~Lib() {
    if (h != nullptr) dlclose(h);
  }
  bool ok() const { return h != nullptr; }
  template <typename Fn>
  Fn sym(const char* n) const {
    return reinterpret_cast<Fn>(dlsym(h, n));
  }
  void* handle() const { return h; }

 private:
  void* h;
};

bool MakeCurrent(Lib& lib) {
  auto gd = lib.sym<EGLDisplay (*)(EGLNativeDisplayType)>("eglGetDisplay");
  auto init = lib.sym<EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)>(
      "eglInitialize");
  auto choose = lib.sym<EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*,
                                       EGLint, EGLint*)>("eglChooseConfig");
  auto cc = lib.sym<EGLContext (*)(EGLDisplay, EGLConfig, EGLContext,
                                   const EGLint*)>("eglCreateContext");
  auto cp = lib.sym<EGLSurface (*)(EGLDisplay, EGLConfig, const EGLint*)>(
      "eglCreatePbufferSurface");
  auto mc = lib.sym<EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface,
                                   EGLContext)>("eglMakeCurrent");
  if (!gd || !init || !choose || !cc || !cp || !mc) return false;
  EGLDisplay d = gd((EGLNativeDisplayType)0);
  if (d == nullptr) return false;
  if (!init(d, nullptr, nullptr)) return false;
  const EGLint ca[] = {0x3040, 0x40, 0x3038};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  if (!choose(d, ca, &cfg, 1, &n)) return false;
  const EGLint xa[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  EGLContext c = cc(d, cfg, nullptr, xa);
  if (c == nullptr) return false;
  const EGLint pa[] = {0x3057, 16, 0x3056, 16, 0x3038};
  EGLSurface s = cp(d, cfg, pa);
  if (s == nullptr) return false;
  return mc(d, s, s, c) != 0;
}

const char* kOptional[16] = {
    "glTextureViewEXT", "glTextureViewOES", "glTexBufferEXT",
    "glTexBufferOES", "glTexBufferRangeEXT", "glTexBufferRangeOES",
    "glPolygonModeNV", "glPolygonModeANGLE", "glColorMaskiEXT",
    "glColorMaskiOES", "glMultiDrawArraysIndirectEXT",
    "glMultiDrawElementsIndirectEXT", "glMultiDrawElementsBaseVertexEXT",
    "glDrawArraysInstancedBaseInstanceEXT",
    "glDrawElementsInstancedBaseInstanceEXT",
    "glDrawElementsInstancedBaseVertexBaseInstanceEXT"};

}  // namespace

TEST(ExtAbi, AllOptionalResolveAndAgree) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  auto proc = lib.sym<void* (*)(const char*)>("eglGetProcAddress");
  EXPECT_TRUE(proc != nullptr);
  if (proc == nullptr) return;
  for (const char* n : kOptional) {
    void* direct = dlsym(lib.handle(), n);
    if (direct == nullptr) {
      std::printf("  optional not exported: %s\n", n);
      EXPECT_TRUE(false);
      continue;
    }
    EXPECT_TRUE(proc(n) == direct);
  }
}

TEST(ExtAbi, AliasesAndPolygonModeBehave) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  auto getError = lib.sym<GLenum (*)()>("glGetError");
  auto genTextures = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bindTexture =
      lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto texBufferEXT =
      lib.sym<void (*)(GLenum, GLenum, GLuint)>("glTexBufferEXT");
  auto texBufferOES =
      lib.sym<void (*)(GLenum, GLenum, GLuint)>("glTexBufferOES");
  auto genBuffers = lib.sym<void (*)(GLsizei, GLuint*)>("glGenBuffers");
  auto bindBuffer = lib.sym<void (*)(GLenum, GLuint)>("glBindBuffer");
  auto bufferData = lib.sym<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>(
      "glBufferData");
  auto colorMaskiEXT =
      lib.sym<void (*)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean)>(
          "glColorMaskiEXT");
  auto polygonNV = lib.sym<void (*)(GLenum, GLenum)>("glPolygonModeNV");
  auto polygonANGLE = lib.sym<void (*)(GLenum, GLenum)>("glPolygonModeANGLE");
  EXPECT_TRUE(getError && genTextures && texBufferEXT && polygonNV);
  if (!getError) return;
  GLuint tb = 0, buf = 0;
  genBuffers(1, &buf);
  bindBuffer(0x8892u, buf);
  static const float kZeros[16] = {0};
  bufferData(0x8892u, sizeof(kZeros), kZeros, 0x88E4u);
  genTextures(1, &tb);
  bindTexture(0x8C2Au /*TEXTURE_BUFFER*/, tb);
  texBufferEXT(0x8C2Au, 0x8058u /*RGBA8*/, buf);
  EXPECT_EQ(getError(), (GLenum)0);
  texBufferOES(0x8C2Au, 0x8058u, buf);
  EXPECT_EQ(getError(), (GLenum)0);
  colorMaskiEXT(0, 1, 1, 1, 1);
  EXPECT_EQ(getError(), (GLenum)0);
  polygonNV(0x0408u /*FRONT_AND_BACK*/, 0x1B02u /*FILL*/);
  EXPECT_EQ(getError(), (GLenum)0);
  polygonANGLE(0x0408u, 0x1B02u);
  EXPECT_EQ(getError(), (GLenum)0);
  // LINE rasterization does not exist: fail-closed, not silent.
  polygonNV(0x0408u, 0x1B01u /*LINE*/);
  EXPECT_EQ(getError(), 0x0502u);
  EXPECT_EQ(getError(), (GLenum)0);
}

TEST(ExtAbi, TextureViewAliasingThroughAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  auto getError = lib.sym<GLenum (*)()>("glGetError");
  auto genTextures = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bindTexture = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto texStorage2D = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei,
                                       GLsizei)>("glTexStorage2D");
  auto texSubImage2D =
      lib.sym<void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                       GLenum, const void*)>("glTexSubImage2D");
  auto textureViewEXT =
      lib.sym<void (*)(GLuint, GLenum, GLuint, GLenum, GLuint, GLuint,
                       GLuint, GLuint)>("glTextureViewEXT");
  auto getTexLevel = lib.sym<void (*)(GLenum, GLint, GLenum, GLint*)>(
      "glGetTexLevelParameteriv");
  EXPECT_TRUE(getError && textureViewEXT && getTexLevel);
  if (!getError) return;
  GLuint orig = 0, view = 0;
  genTextures(1, &orig);
  genTextures(1, &view);
  bindTexture(0x0DE1u, orig);
  texStorage2D(0x0DE1u, 1, 0x8058u, 4, 4);
  EXPECT_EQ(getError(), (GLenum)0);
  textureViewEXT(view, 0x0DE1u, orig, 0x8C43u /*SRGB8_ALPHA8*/, 0, 1, 0, 1);
  EXPECT_EQ(getError(), (GLenum)0);
  // View of a mutable texture is illegal.
  GLuint mut = 0, v2 = 0;
  genTextures(1, &mut);
  genTextures(1, &v2);
  bindTexture(0x0DE1u, mut);
  static const std::uint8_t kPx[64] = {0};
  texSubImage2D(0x0DE1u, 0, 0, 0, 2, 2, 0x1908u, 0x1401u, kPx);
  textureViewEXT(v2, 0x0DE1u, mut, 0x8058u, 0, 1, 0, 1);
  EXPECT_EQ(getError(), 0x0502u);
  EXPECT_EQ(getError(), (GLenum)0);
  // Format query sees the reinterpretation through the view.
  bindTexture(0x0DE1u, view);
  GLint fmt = 0;
  getTexLevel(0x0DE1u, 0, 0x1003u /*INTERNAL_FORMAT*/, &fmt);
  EXPECT_EQ(fmt, (GLint)0x8C43u);
  EXPECT_EQ(getError(), (GLenum)0);
}

TEST(ExtAbi, MultidrawValidationThroughAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  auto getError = lib.sym<GLenum (*)()>("glGetError");
  auto multiArrays = lib.sym<void (*)(GLenum, const void*, GLsizei, GLsizei)>(
      "glMultiDrawArraysIndirectEXT");
  auto multiBaseVertex =
      lib.sym<void (*)(GLenum, const GLsizei*, GLenum, const void* const*,
                       GLsizei, const GLint*)>(
          "glMultiDrawElementsBaseVertexEXT");
  EXPECT_TRUE(multiArrays && multiBaseVertex);
  if (!getError) return;
  // No indirect buffer bound: fail-closed INVALID_OPERATION, not a crash.
  multiArrays(0x0004u, nullptr, 1, 0);
  EXPECT_EQ(getError(), 0x0502u);
  // Negative drawcount is INVALID_OPERATION on the draw queue.
  static const GLsizei kC[1] = {3};
  static const void* kP[1] = {nullptr};
  static const GLint kB[1] = {0};
  multiBaseVertex(0x0004u, kC, 0x1401u, kP, -1, kB);
  EXPECT_EQ(getError(), 0x0502u);
  EXPECT_EQ(getError(), (GLenum)0);
}
