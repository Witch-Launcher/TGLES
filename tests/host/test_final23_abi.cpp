// Type-2 final23: direct ES 3.2 API -> TGL via dlopen for the last 23 gaps.
// Written BEFORE fix (red). Ground truth: docs.gl/es3 + docs/reference/gl32.h
// (signatures) + Khronos refpages (errors). Desktop-only names (LogicOp,
// PointSize) are validated fail-closed (INVALID_ENUM/VALUE), same rule as
// glTexImage1D in gl_real.cpp.

#include "test_framework.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#include "tgles/host/abi.h"

#ifndef TGLES_HOST_LIBRARY_PATH
#define TGLES_HOST_LIBRARY_PATH "./libtgles.dylib"
#endif

namespace {

struct Lib {
  explicit Lib(const char* p) : h(dlopen(p, RTLD_NOW)) {}
  ~Lib() {
    if (h) dlclose(h);
  }
  bool ok() const { return h != nullptr; }
  template <typename F>
  F sym(const char* n) const {
    return reinterpret_cast<F>(dlsym(h, n));
  }
  void* handle() const { return h; }

 private:
  void* h;
};

bool MakeCurrent(Lib& l) {
  auto gd = l.sym<EGLDisplay (*)(EGLNativeDisplayType)>("eglGetDisplay");
  auto init = l.sym<EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)>("eglInitialize");
  auto ch = l.sym<EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*)>("eglChooseConfig");
  auto cc = l.sym<EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*)>("eglCreateContext");
  auto cp = l.sym<EGLSurface (*)(EGLDisplay, EGLConfig, const EGLint*)>("eglCreatePbufferSurface");
  auto mc = l.sym<EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext)>("eglMakeCurrent");
  if (!gd || !init || !ch || !cc || !cp || !mc) return false;
  EGLDisplay d = gd((EGLNativeDisplayType)0);
  if (!d || !init(d, nullptr, nullptr)) return false;
  const EGLint ca[] = {0x3040, 0x40, 0x3038};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  if (!ch(d, ca, &cfg, 1, &n)) return false;
  const EGLint xa[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  EGLContext ctx = cc(d, cfg, nullptr, xa);
  const EGLint pa[] = {0x3057, 32, 0x3056, 32, 0x3038};
  EGLSurface s = cp(d, cfg, pa);
  return mc(d, s, s, ctx) != 0;
}
unsigned long long GapCalls(Lib& l, const char* n) {
  auto f = l.sym<unsigned long long (*)(const char*)>("tglesAbiGapCalls");
  return f ? f(n) : 0;
}
void ResetLedger(Lib& l) {
  auto f = l.sym<void (*)()>("tglesAbiResetGapLedger");
  if (f) f();
}

}  // namespace

TEST(Final23Abi, CompressedTexIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto c2d = lib.sym<void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*)>("glCompressedTexImage2D");
  auto cs2d = lib.sym<void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void*)>("glCompressedTexSubImage2D");
  auto c3d = lib.sym<void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const void*)>("glCompressedTexImage3D");
  auto cs3d = lib.sym<void (*)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const void*)>("glCompressedTexSubImage3D");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && c2d && cs2d && c3d && cs3d && err);
  if (!c2d) return;
  GLuint t = 0;
  gen(1, &t);
  bind(0x0DE1u, t);
  // ETC2 RGB8 4x4 needs 8 bytes (docs.gl table).
  unsigned char blk[8] = {};
  c2d(0x0DE1u, 0, 0x9274u /*ETC2_RGB8*/, 4, 4, 0, 8, blk);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glCompressedTexImage2D"), 0ull);
  c2d(0x0DE1u, 0, 0x9999u, 4, 4, 0, 8, blk);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  c2d(0x0DE1u, 0, 0x9274u, 4, 4, 1, 8, blk);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
  cs2d(0x0DE1u, 0, 0, 0, 2, 2, 0x9274u, 8, blk);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  // 3D needs its own texture object (target is fixed at first bind per spec).
  GLuint t3 = 0;
  gen(1, &t3);
  bind(0x806Fu /*TEXTURE_3D*/, t3);
  c3d(0x806Fu, 0, 0x9274u, 4, 4, 4, 0, 32, nullptr);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glCompressedTexImage3D"), 0ull);
  cs3d(0x806Fu, 0, 0, 0, 0, 2, 2, 2, 0x9274u, 16, nullptr);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
}

TEST(Final23Abi, CopyTexIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto genT = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bindT = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto stor = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>("glTexStorage2D");
  auto genF = lib.sym<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
  auto bindF = lib.sym<void (*)(GLenum, GLuint)>("glBindFramebuffer");
  auto attach = lib.sym<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
  auto copy2d = lib.sym<void (*)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)>("glCopyTexImage2D");
  auto copyS3 = lib.sym<void (*)(GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)>("glCopyTexSubImage3D");
  auto stor3d = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei)>("glTexStorage3D");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(genT && bindT && stor && genF && bindF && attach && copy2d && copyS3 && err);
  if (!copy2d) return;
  GLuint t = 0, f = 0;
  genT(1, &t);
  bindT(0x0DE1u, t);
  stor(0x0DE1u, 1, 0x8058u, 16, 16);
  genF(1, &f);
  bindF(0x8CA9u, f);
  attach(0x8CA9u, 0x8CE0u, 0x0DE1u, t, 0);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  // Copy destination must be mutable (TexStorage makes immutable); use a fresh
  // texture object for the define.
  GLuint td = 0;
  genT(1, &td);
  bindT(0x0DE1u, td);
  copy2d(0x0DE1u, 0, 0x8058u, 0, 0, 8, 8, 0);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glCopyTexImage2D"), 0ull);
  copy2d(0x0DE1u, 0, 0x8058u, 0, 0, 8, 8, 1);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
  // 3D sub-copy needs its own 3D texture with storage.
  GLuint t3 = 0;
  genT(1, &t3);
  bindT(0x806Fu, t3);
  EXPECT_TRUE(stor3d != nullptr);
  if (stor3d) stor3d(0x806Fu, 1, 0x8058u, 8, 8, 8);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  copyS3(0x806Fu, 0, 0, 0, 0, 0, 0, 4, 4);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glCopyTexSubImage3D"), 0ull);
}

TEST(Final23Abi, ClearBufferIntIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto genT = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bindT = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto stor = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>("glTexStorage2D");
  auto genF = lib.sym<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
  auto bindF = lib.sym<void (*)(GLenum, GLuint)>("glBindFramebuffer");
  auto attach = lib.sym<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
  auto civ = lib.sym<void (*)(GLenum, GLint, const GLint*)>("glClearBufferiv");
  auto cuv = lib.sym<void (*)(GLenum, GLint, const GLuint*)>("glClearBufferuiv");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(genT && civ && cuv && err);
  if (!civ) return;
  GLuint t = 0, f = 0;
  genT(1, &t);
  bindT(0x0DE1u, t);
  stor(0x0DE1u, 1, 0x8058u, 8, 8);
  genF(1, &f);
  bindF(0x8CA9u, f);
  attach(0x8CA9u, 0x8CE0u, 0x0DE1u, t, 0);
  const GLint iv[4] = {1, 2, 3, 4};
  civ(0x1800u /*COLOR*/, 0, iv);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glClearBufferiv"), 0ull);
  const GLuint uv[4] = {5, 6, 7, 8};
  cuv(0x1800u, 0, uv);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  civ(0x9999u, 0, iv);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  civ(0x1800u, 0, nullptr);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
}

TEST(Final23Abi, ReadPixelsIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto genT = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bindT = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto stor = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>("glTexStorage2D");
  auto genF = lib.sym<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
  auto bindF = lib.sym<void (*)(GLenum, GLuint)>("glBindFramebuffer");
  auto attach = lib.sym<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
  auto read = lib.sym<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)>("glReadPixels");
  auto readn = lib.sym<void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLsizei, void*)>("glReadnPixels");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(genT && stor && genF && attach && read && readn && err);
  if (!read) return;
  GLuint t = 0, f = 0;
  genT(1, &t);
  bindT(0x0DE1u, t);
  stor(0x0DE1u, 1, 0x8058u, 8, 8);
  genF(1, &f);
  // FRAMEBUFFER (0x8D40) binds read+draw: glReadPixels reads through the
  // read binding, and a DRAW-only bind would leave it at the window FBO.
  bindF(0x8D40u, f);
  attach(0x8CA9u, 0x8CE0u, 0x0DE1u, t, 0);
  unsigned char px[4 * 4 * 4] = {};
  read(0, 0, 4, 4, 0x1908u /*RGBA*/, 0x1401u /*UBYTE*/, px);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glReadPixels"), 0ull);
  read(0, 0, 4, 4, 0x9999u, 0x1401u, px);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  readn(0, 0, 4, 4, 0x1908u, 0x1401u, sizeof(px), px);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  readn(0, 0, 4, 4, 0x1908u, 0x1401u, 4, px);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_OPERATION);
}

TEST(Final23Abi, IndexedEnableIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto ena = lib.sym<void (*)(GLenum, GLuint)>("glEnablei");
  auto dis = lib.sym<void (*)(GLenum, GLuint)>("glDisablei");
  auto is = lib.sym<GLboolean (*)(GLenum, GLuint)>("glIsEnabledi");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(ena && dis && is && err);
  if (!ena) return;
  ena(0x0BE2u /*BLEND*/, 0);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(is(0x0BE2u, 0) == GL_TRUE);
  EXPECT_EQ(GapCalls(lib, "glEnablei"), 0ull);
  dis(0x0BE2u, 0);
  EXPECT_TRUE(is(0x0BE2u, 0) == GL_FALSE);
  ena(0x9999u, 0);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  ena(0x0BE2u, 99u);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
}

TEST(Final23Abi, RasterMiscIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto logic = lib.sym<void (*)(GLenum)>("glLogicOp");
  auto point = lib.sym<void (*)(GLfloat)>("glPointSize");
  auto barrier = lib.sym<void (*)()>("glBlendBarrier");
  auto minss = lib.sym<void (*)(GLfloat)>("glMinSampleShading");
  auto bbox = lib.sym<void (*)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)>("glPrimitiveBoundingBox");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(logic && point && barrier && minss && bbox && err);
  if (!logic) return;
  logic(0x1503u /*COPY*/);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glLogicOp"), 0ull);
  logic(0x9999u);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  point(3.0f);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  point(0.0f);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
  barrier();
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  minss(0.5f);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  minss(2.0f);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
  bbox(0, 0, 0, 1, 1, 1, 1, 1);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
}

TEST(Final23Abi, FboMiscIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto genT = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bindT = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto stor = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>("glTexStorage2D");
  auto genF = lib.sym<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
  auto bindF = lib.sym<void (*)(GLenum, GLuint)>("glBindFramebuffer");
  auto ftex = lib.sym<void (*)(GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture");
  auto invSub = lib.sym<void (*)(GLenum, GLsizei, const GLenum*, GLint, GLint, GLsizei, GLsizei)>("glInvalidateSubFramebuffer");
  auto ms2d = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean)>("glTexStorage2DMultisample");
  auto ms3d = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei, GLboolean)>("glTexStorage3DMultisample");
  auto shbin = lib.sym<void (*)(GLsizei, const GLuint*, GLenum, const void*, GLsizei)>("glShaderBinary");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(genT && bindT && stor && genF && bindF && ftex && invSub && ms2d && ms3d && shbin && err);
  if (!ftex) return;
  GLuint t = 0, f = 0;
  genT(1, &t);
  bindT(0x0DE1u, t);
  stor(0x0DE1u, 1, 0x8058u, 8, 8);
  genF(1, &f);
  bindF(0x8CA9u, f);
  ftex(0x8CA9u, 0x8CE0u, t, 0);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glFramebufferTexture"), 0ull);
  const GLenum att = 0x8CE0u;
  invSub(0x8CA9u, 1, &att, 0, 0, 4, 4);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  // Multisample storage needs its own (immutable) texture objects.
  GLuint tms = 0;
  genT(1, &tms);
  bindT(0x9100u /*TEXTURE_2D_MULTISAMPLE*/, tms);
  ms2d(0x9100u, 4, 0x8058u, 8, 8, GL_TRUE);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glTexStorage2DMultisample"), 0ull);
  GLuint tms3 = 0;
  genT(1, &tms3);
  bindT(0x9102u, tms3);
  ms3d(0x9102u, 4, 0x8058u, 8, 8, 4, GL_TRUE);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  shbin(0, nullptr, 0, nullptr, 0);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glShaderBinary"), 0ull);
}
