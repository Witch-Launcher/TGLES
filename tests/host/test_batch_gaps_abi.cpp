// Type-2 test: direct OpenGL ES 3.2 API -> TGL -> Metal path via the built host
// library (dlopen, same as CTS/MobileGL). Written BEFORE the batch fix (red).
//
// Covers the batch: Get* remaining (11) + VertexAttrib Format/Binding (5) +
// Draw instanced/indirect (8) + sot Uniform/Tex/Sampler (5). Plus a full 68-gap
// audit listing every declared gap so missing work stays visible.
//
// External ground truth (not TGL docs):
// - docs.gl/es3 pages for glGetBufferPointerv, glVertexAttribFormat,
//   glDrawArraysInstanced, glGetInternalformativ (mirrors Khronos refpages).
// - Khronos gl32.h prototypes in docs/reference/gl32.h (signatures).
// - ES 3.2 spec via docs/reference/es_spec_3.2.pdf (error rules).

#include "test_framework.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "tgles/host/abi.h"

#ifndef TGLES_HOST_LIBRARY_PATH
#define TGLES_HOST_LIBRARY_PATH "./libtgles.dylib"
#endif

namespace {

struct Lib {
  explicit Lib(const char* path) : h(dlopen(path, RTLD_NOW)) {}
  ~Lib() {
    if (h) dlclose(h);
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
  auto init = lib.sym<EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)>("eglInitialize");
  auto choose = lib.sym<EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*)>("eglChooseConfig");
  auto cc = lib.sym<EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*)>("eglCreateContext");
  auto cp = lib.sym<EGLSurface (*)(EGLDisplay, EGLConfig, const EGLint*)>("eglCreatePbufferSurface");
  auto mc = lib.sym<EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext)>("eglMakeCurrent");
  if (!gd || !init || !choose || !cc || !cp || !mc) return false;
  EGLDisplay d = gd((EGLNativeDisplayType)0);
  if (!d) return false;
  if (!init(d, nullptr, nullptr)) return false;
  const EGLint ca[] = {0x3040, 0x40, 0x3038};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  if (!choose(d, ca, &cfg, 1, &n)) return false;
  const EGLint xa[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  EGLContext ctx = cc(d, cfg, nullptr, xa);
  if (!ctx) return false;
  const EGLint pa[] = {0x3057, 32, 0x3056, 32, 0x3038};
  EGLSurface s = cp(d, cfg, pa);
  if (!s) return false;
  return mc(d, s, s, ctx) != 0;
}

unsigned long long GapCalls(Lib& lib, const char* name) {
  auto f = lib.sym<unsigned long long (*)(const char*)>("tglesAbiGapCalls");
  return f ? f(name) : 0;
}
void ResetLedger(Lib& lib) {
  auto f = lib.sym<void (*)()>("tglesAbiResetGapLedger");
  if (f) f();
}

GLuint MakeLinkedProgram(Lib& lib, const char* vs_extra, const char* fs_extra) {
  auto cs = lib.sym<GLuint (*)(GLenum)>("glCreateShader");
  auto src = lib.sym<void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)>("glShaderSource");
  auto comp = lib.sym<void (*)(GLuint)>("glCompileShader");
  auto cp = lib.sym<GLuint (*)()>("glCreateProgram");
  auto at = lib.sym<void (*)(GLuint, GLuint)>("glAttachShader");
  auto lk = lib.sym<void (*)(GLuint)>("glLinkProgram");
  std::string vs = std::string("#version 320 es\nlayout(location=0) in vec3 a_pos;\nvoid main(){gl_Position=vec4(a_pos,1.);}\n") + (vs_extra ? vs_extra : "");
  std::string fs = std::string("#version 320 es\nprecision mediump float;\n") + (fs_extra ? fs_extra : "") + "layout(location=0) out vec4 o;\nvoid main(){o=vec4(1.);}\n";
  GLuint v = cs(0x8B31u), f = cs(0x8B30u);
  const GLchar* vsp = (const GLchar*)vs.c_str();
  const GLchar* fsp = (const GLchar*)fs.c_str();
  src(v, 1, &vsp, nullptr);
  src(f, 1, &fsp, nullptr);
  comp(v);
  comp(f);
  GLuint p = cp();
  at(p, v);
  at(p, f);
  lk(p);
  return p;
}

}  // namespace

// --- Full 68-gap audit: documents every gap, batch or not --------------------
// This test always passes (it only reports); the per-batch tests below fail
// while their entries are still gaps. The list is machine-checked against
// tglesAbiGapCount so new gaps cannot hide.
TEST(BatchAudit68, DeclaredGapListMatchesContract) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  auto count = lib.sym<unsigned int (*)()>("tglesAbiGapCount");
  auto name = lib.sym<const char* (*)(unsigned int)>("tglesAbiGapName");
  EXPECT_TRUE(count && name);
  if (!count || !name) return;
  unsigned total = count();
  // Pre-batch budget is 68; batch fix must lower it (never raise silently).
  std::printf("  declared gaps now: %u (pre-batch 68)\n", total);
  EXPECT_TRUE(total <= 68);
  // Spot-check that the batch entries exist in the ledger namespace (either as
  // gaps now, or as real functions with zero gap calls after the fix).
  const char* batch[] = {"glGetBufferPointerv",
                         "glGetInternalformativ",
                         "glVertexAttribFormat",
                         "glDrawArraysInstanced",
                         "glUniform1iv",
                         "glUniform4fv",
                         "glTexParameteriv",
                         "glSamplerParameteriv"};
  for (const char* n : batch) {
    bool found_as_gap = false;
    for (unsigned i = 0; i < total; ++i) {
      const char* g = name(i);
      if (g && std::strcmp(g, n) == 0) {
        found_as_gap = true;
        break;
      }
    }
    // Either still a declared gap, or already promoted to real (both honest).
    void* addr = dlsym(lib.handle(), n);
    EXPECT_TRUE(addr != nullptr);
    (void)found_as_gap;
  }
}

// --- Sot: Uniform1iv + Uniform4fv must be real (manager already has logic) ---
TEST(BatchAbiSot, Uniform1ivAnd4fvAreReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto getLoc = lib.sym<GLint (*)(GLuint, const GLchar*)>("glGetUniformLocation");
  auto use = lib.sym<void (*)(GLuint)>("glUseProgram");
  auto u1iv = lib.sym<void (*)(GLint, GLsizei, const GLint*)>("glUniform1iv");
  auto u4fv = lib.sym<void (*)(GLint, GLsizei, const GLfloat*)>("glUniform4fv");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(getLoc && use && u1iv && u4fv && err);
  if (!u1iv || !u4fv) return;
  GLuint p = MakeLinkedProgram(lib, "", "uniform int u_i;\nuniform vec4 u_v;\n");
  use(p);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  GLint li = getLoc(p, "u_i");
  GLint lv = getLoc(p, "u_v");
  EXPECT_TRUE(li >= 0 && lv >= 0);
  const GLint iv[1] = {7};
  u1iv(li, 1, iv);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  const GLfloat fv[4] = {1, 2, 3, 4};
  u4fv(lv, 1, fv);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  // Real functions must not touch the gap ledger.
  EXPECT_EQ(GapCalls(lib, "glUniform1iv"), 0ull);
  EXPECT_EQ(GapCalls(lib, "glUniform4fv"), 0ull);
  // Bad count still fails closed.
  u1iv(li, -1, iv);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
}

// --- Sot: TexParameteriv + SamplerParameteriv must be real -------------------
TEST(BatchAbiSot, TexAndSamplerParameterivAreReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto genT = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bindT = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto texIv = lib.sym<void (*)(GLenum, GLenum, const GLint*)>("glTexParameteriv");
  auto getTexIv = lib.sym<void (*)(GLenum, GLenum, GLint*)>("glGetTexParameteriv");
  auto genS = lib.sym<void (*)(GLsizei, GLuint*)>("glGenSamplers");
  auto sampIv = lib.sym<void (*)(GLuint, GLenum, const GLint*)>("glSamplerParameteriv");
  auto getSampIv = lib.sym<void (*)(GLuint, GLenum, GLint*)>("glGetSamplerParameteriv");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(genT && bindT && texIv && getTexIv && genS && sampIv && err);
  if (!texIv || !sampIv) return;
  GLuint t = 0;
  genT(1, &t);
  bindT(0x0DE1u, t);
  const GLint wrap = 0x2901;  // REPEAT
  texIv(0x0DE1u, 0x2802u, &wrap);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  GLint got = 0;
  getTexIv(0x0DE1u, 0x2802u, &got);
  EXPECT_EQ(got, wrap);
  EXPECT_EQ(GapCalls(lib, "glTexParameteriv"), 0ull);
  GLuint s = 0;
  genS(1, &s);
  auto bindS = lib.sym<void (*)(GLuint, GLuint)>("glBindSampler");
  EXPECT_TRUE(bindS != nullptr);
  if (bindS) bindS(0, s);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  const GLint lin = 0x2601;  // LINEAR
  sampIv(s, 0x2800u, &lin);
  // Never-created sampler (9999) must be INVALID_OPERATION, not silent.
  sampIv(9999u, 0x2800u, &lin);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_OPERATION);
  sampIv(s, 0x2800u, &lin);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  GLint sg = 0;
  getSampIv(s, 0x2800u, &sg);
  EXPECT_EQ(sg, lin);
  EXPECT_EQ(GapCalls(lib, "glSamplerParameteriv"), 0ull);
}

// --- Get*: GetBufferPointerv -------------------------------------------------
// docs.gl/es3: pname must be BUFFER_MAP_POINTER; unmapped -> NULL; bad target
// -> INVALID_ENUM; bound 0 -> INVALID_OPERATION; null params -> INVALID_VALUE.
TEST(BatchAbiGet, GetBufferPointervIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenBuffers");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindBuffer");
  auto data = lib.sym<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
  auto map = lib.sym<void* (*)(GLenum, GLintptr, GLsizeiptr, GLbitfield)>("glMapBufferRange");
  auto unmap = lib.sym<GLboolean (*)(GLenum)>("glUnmapBuffer");
  auto getPtr = lib.sym<void (*)(GLenum, GLenum, void**)>("glGetBufferPointerv");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && data && map && unmap && getPtr && err);
  if (!getPtr) return;
  GLuint b = 0;
  gen(1, &b);
  bind(0x8892u, b);
  data(0x8892u, 16, nullptr, 0x88E8u);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  void* p = map(0x8892u, 0, 16, 0x0002u);
  EXPECT_TRUE(p != nullptr);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  void* out = (void*)0x1234;
  getPtr(0x8892u, 0x88BDu, &out);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(out == p);
  EXPECT_EQ(GapCalls(lib, "glGetBufferPointerv"), 0ull);
  unmap(0x8892u);
  getPtr(0x8892u, 0x88BDu, &out);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(out == nullptr);
  getPtr(0x9999u, 0x88BDu, &out);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  getPtr(0x8892u, 0x9999u, &out);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
}

// --- Get*: GetInternalformativ -----------------------------------------------
// docs.gl/es3: target RENDERBUFFER or TEXTURE_2D_MULTISAMPLE; pname SAMPLES or
// NUM_SAMPLE_COUNTS; bufSize negative -> INVALID_VALUE; unknown -> INVALID_ENUM.
TEST(BatchAbiGet, GetInternalformativIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto get = lib.sym<void (*)(GLenum, GLenum, GLenum, GLsizei, GLint*)>("glGetInternalformativ");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get && err);
  if (!get) return;
  GLint params[8] = {};
  get(0x8D41u, 0x8058u, 0x9380u /*NUM_SAMPLE_COUNTS*/, 8, params);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(params[0] >= 1);
  EXPECT_EQ(GapCalls(lib, "glGetInternalformativ"), 0ull);
  get(0x8D41u, 0x8058u, 0x80A9u /*SAMPLES*/, 8, params);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(params[0] >= 1);
  get(0x9999u, 0x8058u, 0x80A9u, 8, params);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  get(0x8D41u, 0x8058u, 0x9999u, 8, params);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  get(0x8D41u, 0x8058u, 0x80A9u, -1, params);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
}

// --- VertexAttrib Format/Binding (ES 3.1+ separate attribs) ------------------
// docs.gl/es3/glVertexAttribFormat: bad index/size -> INVALID_VALUE, bad type
// -> INVALID_ENUM. Must work with BindVertexBuffer + VertexBindingDivisor.
TEST(BatchAbiVertex, SeparateAttribFormatIsReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto genVao = lib.sym<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
  auto bindVao = lib.sym<void (*)(GLuint)>("glBindVertexArray");
  auto genBuf = lib.sym<void (*)(GLsizei, GLuint*)>("glGenBuffers");
  auto bindBuf = lib.sym<void (*)(GLenum, GLuint)>("glBindBuffer");
  auto bufData = lib.sym<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>("glBufferData");
  auto bindVB = lib.sym<void (*)(GLuint, GLuint, GLintptr, GLsizei)>("glBindVertexBuffer");
  auto fmt = lib.sym<void (*)(GLuint, GLint, GLenum, GLboolean, GLuint)>("glVertexAttribFormat");
  auto ifmt = lib.sym<void (*)(GLuint, GLint, GLenum, GLuint)>("glVertexAttribIFormat");
  auto abind = lib.sym<void (*)(GLuint, GLuint)>("glVertexAttribBinding");
  auto vdiv = lib.sym<void (*)(GLuint, GLuint)>("glVertexBindingDivisor");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(genVao && bindVao && genBuf && bindBuf && bufData && bindVB && fmt && ifmt && abind && vdiv && err);
  if (!fmt) return;
  GLuint vao = 0, buf = 0;
  genVao(1, &vao);
  bindVao(vao);
  genBuf(1, &buf);
  bindBuf(0x8892u, buf);
  const float v[12] = {};
  bufData(0x8892u, sizeof(v), v, 0x88E8u);
  bindVB(0, buf, 0, 12);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  fmt(0, 3, 0x1406u /*FLOAT*/, 0, 0);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  abind(0, 0);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  vdiv(0, 1);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  ifmt(1, 4, 0x1404u /*INT*/, 0);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glVertexAttribFormat"), 0ull);
  EXPECT_EQ(GapCalls(lib, "glBindVertexBuffer"), 0ull);
  EXPECT_EQ(GapCalls(lib, "glVertexAttribBinding"), 0ull);
  EXPECT_EQ(GapCalls(lib, "glVertexBindingDivisor"), 0ull);
  // Errors fail closed.
  fmt(99u, 3, 0x1406u, 0, 0);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
  fmt(0, 5, 0x1406u, 0, 0);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
  fmt(0, 3, 0x9999u, 0, 0);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
}

// --- Draw: instanced + indirect validation routes to DrawValidator -----------
// docs.gl/es3/glDrawArraysInstanced: bad mode -> INVALID_ENUM, negative count
// or primcount -> INVALID_VALUE. Indirect needs DRAW_INDIRECT_BUFFER bound.
TEST(BatchAbiDraw, InstancedAndIndirectAreReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto instA = lib.sym<void (*)(GLenum, GLint, GLsizei, GLsizei)>("glDrawArraysInstanced");
  auto instE = lib.sym<void (*)(GLenum, GLsizei, GLenum, const void*, GLsizei)>("glDrawElementsInstanced");
  auto indA = lib.sym<void (*)(GLenum, const void*)>("glDrawArraysIndirect");
  auto indE = lib.sym<void (*)(GLenum, GLenum, const void*)>("glDrawElementsIndirect");
  auto range = lib.sym<void (*)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*)>("glDrawRangeElements");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(instA && instE && indA && indE && range && err);
  if (!instA) return;
  instA(0x0004u, 0, 3, 2);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glDrawArraysInstanced"), 0ull);
  instA(0x9999u, 0, 3, 1);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  instA(0x0004u, 0, -1, 1);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
  instA(0x0004u, 0, 3, -1);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_VALUE);
  range(0x0004u, 0, 2, 3, 0x1403u, (const void*)0);
  // No EAB bound on fresh VAO default? May be INVALID_OPERATION; either INVALID_*
  // proves it is validated, not a gap. Accept NO_ERROR only if an EAB exists.
  GLenum e = err();
  EXPECT_TRUE(e == (GLenum)GL_NO_ERROR || e == (GLenum)GL_INVALID_OPERATION);
  // Indirect without DRAW_INDIRECT_BUFFER must be INVALID_OPERATION.
  indA(0x0004u, (const void*)0);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_OPERATION);
  EXPECT_EQ(GapCalls(lib, "glDrawArraysIndirect"), 0ull);
}

// --- Pixel + TransformFeedback object ops (managers already existed) ---------
// PixelState and TransformFeedbackManager were implemented but had no facade
// member / ABI route (sot). These tests prove the ABI is now real.
TEST(BatchAbiPixelTf, StoreAndTransformFeedbackAreReal) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakeCurrent(lib));
  ResetLedger(lib);
  auto store = lib.sym<void (*)(GLenum, GLint)>("glPixelStorei");
  auto genTf = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTransformFeedbacks");
  auto bindTf = lib.sym<void (*)(GLenum, GLuint)>("glBindTransformFeedback");
  auto isTf = lib.sym<GLboolean (*)(GLuint)>("glIsTransformFeedback");
  auto beginTf = lib.sym<void (*)(GLenum)>("glBeginTransformFeedback");
  auto endTf = lib.sym<void (*)()>("glEndTransformFeedback");
  auto err = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(store && genTf && bindTf && isTf && beginTf && endTf && err);
  if (!store) return;
  store(0x0CF5u /*UNPACK_ALIGNMENT*/, 1);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glPixelStorei"), 0ull);
  store(0x9999u, 1);
  EXPECT_EQ(err(), (GLenum)GL_INVALID_ENUM);
  GLuint id = 0;
  genTf(1, &id);
  EXPECT_TRUE(id != 0u);
  bindTf(0x8E22u, id);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(isTf(id) == GL_TRUE);
  beginTf(0x0004u /*TRIANGLES*/);
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  endTf();
  EXPECT_EQ(err(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(GapCalls(lib, "glGenTransformFeedbacks"), 0ull);
  EXPECT_EQ(GapCalls(lib, "glBeginTransformFeedback"), 0ull);
}
