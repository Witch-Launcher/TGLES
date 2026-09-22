// Exam-style state path (TDD red-first, mini CTS functional.basic).
//
// Why a separate file: test_abi_contract.cpp pins the *shape* of the ABI
// (every name resolves). This file pins *behaviour* through the real built
// library via dlopen — the same way CTS and MobileGL call it. No C++
// singletons, no mocks: if an entry is a gap, the behaviour check fails and
// the entry must be implemented in src/host/abi/gl_real.cpp for real.

#include "test_framework.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "tgles/host/abi.h"

#ifndef TGLES_HOST_LIBRARY_PATH
#define TGLES_HOST_LIBRARY_PATH "./libtgles.dylib"
#endif

namespace {

// GLES 3.2 tokens the functional.basic group touches first.
constexpr GLenum kDepthTest = 0x0B71;
constexpr GLenum kBlend = 0x0BE2;

struct Lib {
  explicit Lib(const char* path) : handle_(dlopen(path, RTLD_NOW)) {}
  ~Lib() {
    if (handle_ != nullptr) dlclose(handle_);
  }
  bool ok() const { return handle_ != nullptr; }
  template <typename Fn>
  Fn sym(const char* name) const {
    return reinterpret_cast<Fn>(dlsym(handle_, name));
  }

 private:
  void* handle_;
};

bool MakePbufferCurrent(Lib& lib) {
  auto get_display =
      lib.sym<EGLDisplay (*)(EGLNativeDisplayType)>("eglGetDisplay");
  auto initialize = lib.sym<EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)>(
      "eglInitialize");
  auto choose = lib.sym<EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*,
                                       EGLint, EGLint*)>("eglChooseConfig");
  auto create_ctx = lib.sym<EGLContext (*)(EGLDisplay, EGLConfig, EGLContext,
                                           const EGLint*)>("eglCreateContext");
  auto create_pb =
      lib.sym<EGLSurface (*)(EGLDisplay, EGLConfig, const EGLint*)>(
          "eglCreatePbufferSurface");
  auto make_current = lib.sym<EGLBoolean (*)(EGLDisplay, EGLSurface,
                                             EGLSurface, EGLContext)>(
      "eglMakeCurrent");
  if (!get_display || !initialize || !choose || !create_ctx || !create_pb ||
      !make_current) {
    return false;
  }
  EGLDisplay dpy = get_display((EGLNativeDisplayType)0);
  if (dpy == nullptr) return false;
  if (initialize(dpy, nullptr, nullptr) == 0) return false;
  const EGLint cfg_attribs[] = {0x3040 /*RENDERABLE*/, 0x40 /*ES3*/,
                                0x3038 /*NONE*/};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  if (choose(dpy, cfg_attribs, &cfg, 1, &n) == 0) return false;
  const EGLint ctx_attribs[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  EGLContext ctx = create_ctx(dpy, cfg, nullptr, ctx_attribs);
  if (ctx == nullptr) return false;
  const EGLint pb[] = {0x3057, 16, 0x3056, 16, 0x3038};
  EGLSurface surf = create_pb(dpy, cfg, pb);
  if (surf == nullptr) return false;
  return make_current(dpy, surf, surf, ctx) != 0;
}

}  // namespace

TEST(AbiStatePath, EnableDisableRoundTrip) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto enable = lib.sym<void (*)(GLenum)>("glEnable");
  auto disable = lib.sym<void (*)(GLenum)>("glDisable");
  auto is_enabled = lib.sym<GLboolean (*)(GLenum)>("glIsEnabled");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(enable && disable && is_enabled && get_error);
  if (!enable || !disable || !is_enabled || !get_error) return;

  // DEPTH_TEST starts disabled (spec 15.1.7 defaults).
  EXPECT_TRUE(is_enabled(kDepthTest) == GL_FALSE);
  enable(kDepthTest);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(is_enabled(kDepthTest) == GL_TRUE);
  disable(kDepthTest);
  EXPECT_TRUE(is_enabled(kDepthTest) == GL_FALSE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);

  // Unknown cap: fail closed, state untouched.
  enable(0x9999u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  EXPECT_TRUE(is_enabled(kBlend) == GL_FALSE);
}

TEST(AbiStatePath, RasterSettersValidate) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto blend_func = lib.sym<void (*)(GLenum, GLenum)>("glBlendFunc");
  auto blend_equation = lib.sym<void (*)(GLenum)>("glBlendEquation");
  auto depth_func = lib.sym<void (*)(GLenum)>("glDepthFunc");
  auto depth_mask = lib.sym<void (*)(GLboolean)>("glDepthMask");
  auto cull_face = lib.sym<void (*)(GLenum)>("glCullFace");
  auto front_face = lib.sym<void (*)(GLenum)>("glFrontFace");
  auto color_mask =
      lib.sym<void (*)(GLboolean, GLboolean, GLboolean, GLboolean)>(
          "glColorMask");
  auto scissor =
      lib.sym<void (*)(GLint, GLint, GLsizei, GLsizei)>("glScissor");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(blend_func && blend_equation && depth_func && depth_mask &&
              cull_face && front_face && color_mask && scissor && get_error);
  if (!get_error) return;

  // Valid values: silent success.
  blend_func(0x0302 /*SRC_ALPHA*/, 0x0303 /*ONE_MINUS_SRC_ALPHA*/);
  blend_equation(0x8006 /*FUNC_ADD*/);
  depth_func(0x0201 /*LESS*/);
  depth_mask(1);
  cull_face(0x0405 /*BACK*/);
  front_face(0x0901 /*CCW*/);
  color_mask(1, 1, 1, 1);
  scissor(0, 0, 16, 16);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);

  // Invalid values: fail closed, one error at a time.
  blend_func(0x9999u, 0x0303u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  blend_equation(0x9999u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  depth_func(0x9999u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  cull_face(0x9999u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  front_face(0x9999u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  scissor(0, 0, -1, 16);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_VALUE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, DrawElementsFailsClosedWithoutIndexBuffer) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen_vao = lib.sym<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
  auto bind_vao = lib.sym<void (*)(GLuint)>("glBindVertexArray");
  auto draw_elements =
      lib.sym<void (*)(GLenum, GLsizei, GLenum, const void*)>(
          "glDrawElements");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen_vao && bind_vao && draw_elements && get_error);
  if (!draw_elements || !get_error) return;

  // VAO with no ELEMENT_ARRAY_BUFFER: ES 3.2 core has no client arrays, so
  // an indexed draw must fail closed (and must not touch the gap ledger as
  // a "supported" call — it records INVALID_OPERATION instead).
  GLuint vao = 0;
  gen_vao(1, &vao);
  bind_vao(vao);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  draw_elements(0x0004 /*TRIANGLES*/, 3, 0x1403 /*UNSIGNED_SHORT*/,
                (const void*)0);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  // Bad index type fails even earlier, on the enum.
  draw_elements(0x0004, 3, 0x9999u, (const void*)0);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, ObjectLifecycleRoundTrip) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen_buffers = lib.sym<void (*)(GLsizei, GLuint*)>("glGenBuffers");
  auto delete_buffers =
      lib.sym<void (*)(GLsizei, const GLuint*)>("glDeleteBuffers");
  auto bind_buffer = lib.sym<void (*)(GLenum, GLuint)>("glBindBuffer");
  auto is_buffer = lib.sym<GLboolean (*)(GLuint)>("glIsBuffer");
  auto gen_textures = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto delete_textures =
      lib.sym<void (*)(GLsizei, const GLuint*)>("glDeleteTextures");
  auto bind_texture = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto is_texture = lib.sym<GLboolean (*)(GLuint)>("glIsTexture");
  auto create_program = lib.sym<GLuint (*)()>("glCreateProgram");
  auto delete_program = lib.sym<void (*)(GLuint)>("glDeleteProgram");
  auto is_program = lib.sym<GLboolean (*)(GLuint)>("glIsProgram");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen_buffers && is_buffer && gen_textures && is_texture &&
              create_program && is_program && get_error);
  if (!get_error) return;

  // Generated names are reserved, not objects: Is* stays FALSE until first
  // bind (spec 2.6.1 object model; same rule as real drivers).
  GLuint b = 0;
  gen_buffers(1, &b);
  EXPECT_TRUE(b != 0u);
  EXPECT_TRUE(is_buffer(b) == GL_FALSE);
  bind_buffer(0x8892 /*ARRAY_BUFFER*/, b);
  EXPECT_TRUE(is_buffer(b) == GL_TRUE);
  delete_buffers(1, &b);
  EXPECT_TRUE(is_buffer(b) == GL_FALSE);

  GLuint t = 0;
  gen_textures(1, &t);
  EXPECT_TRUE(is_texture(t) == GL_FALSE);
  bind_texture(0x0DE1 /*TEXTURE_2D*/, t);
  EXPECT_TRUE(is_texture(t) == GL_TRUE);
  delete_textures(1, &t);
  EXPECT_TRUE(is_texture(t) == GL_FALSE);

  GLuint p = create_program();
  EXPECT_TRUE(p != 0u);
  EXPECT_TRUE(is_program(p) == GL_TRUE);
  delete_program(p);
  EXPECT_TRUE(is_program(p) == GL_FALSE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, QueryObjectsRoundTrip) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenQueries");
  auto del = lib.sym<void (*)(GLsizei, const GLuint*)>("glDeleteQueries");
  auto begin = lib.sym<void (*)(GLenum, GLuint)>("glBeginQuery");
  auto end = lib.sym<void (*)(GLenum)>("glEndQuery");
  auto is = lib.sym<GLboolean (*)(GLuint)>("glIsQuery");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && del && is && get_error);
  if (!gen) return;

  GLuint q = 0;
  gen(1, &q);
  EXPECT_TRUE(q != 0u);
  EXPECT_TRUE(is(q) == GL_FALSE);  // Reserved until first Begin.
  begin(0x8C2Fu /*ANY_SAMPLES_PASSED*/, q);
  EXPECT_TRUE(is(q) == GL_TRUE);
  end(0x8C2Fu);
  del(1, &q);
  EXPECT_TRUE(is(q) == GL_FALSE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, FenceSyncFlow) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto fence = lib.sym<GLsync (*)(GLenum, GLbitfield)>("glFenceSync");
  auto is = lib.sym<GLboolean (*)(GLsync)>("glIsSync");
  auto client_wait =
      lib.sym<GLenum (*)(GLsync, GLbitfield, GLuint64)>("glClientWaitSync");
  auto del = lib.sym<void (*)(GLsync)>("glDeleteSync");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(fence && is && client_wait && del && get_error);
  if (!fence) return;

  GLsync s = fence(0x9117 /*SYNC_GPU_COMMANDS_COMPLETE*/, 0);
  EXPECT_TRUE(s != (GLsync)0);  // Gap returns null sync.
  EXPECT_TRUE(is(s) == GL_TRUE);
  // CPU model: unsignaled fence + zero timeout expires, no error.
  EXPECT_EQ(client_wait(s, 0, 0), (GLenum)0x911B /*TIMEOUT_EXPIRED*/);
  del(s);
  EXPECT_TRUE(is(s) == GL_FALSE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, ImmutableStorageViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto storage = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>(
      "glTexStorage2D");
  auto image = lib.sym<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                GLint, GLenum, GLenum, const void*)>(
      "glTexImage2D");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && storage && image && get_error);
  if (!storage) return;

  GLuint t = 0;
  gen(1, &t);
  bind(0x0DE1 /*TEXTURE_2D*/, t);
  storage(0x0DE1, 1, 0x8058 /*RGBA8*/, 8, 8);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  // Immutable level rejects redefinition (gap storage is a no-op, so the
  // mutable upload wrongly succeeds there).
  image(0x0DE1, 0, 0x8058, 8, 8, 0, 0x1908 /*RGBA*/,
        0x1401 /*UNSIGNED_BYTE*/, nullptr);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, ShaderInfoLogViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto create = lib.sym<GLuint (*)(GLenum)>("glCreateShader");
  auto source = lib.sym<void (*)(GLuint, GLsizei, const GLchar* const*,
                                 const GLint*)>("glShaderSource");
  auto compile = lib.sym<void (*)(GLuint)>("glCompileShader");
  auto get_iv =
      lib.sym<void (*)(GLuint, GLenum, GLint*)>("glGetShaderiv");
  auto get_log = lib.sym<void (*)(GLuint, GLsizei, GLsizei*, GLchar*)>(
      "glGetShaderInfoLog");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(create && source && compile && get_iv && get_log && get_error);
  if (!get_log) return;

  GLuint vs = create(0x8B31 /*VERTEX_SHADER*/);
  const GLchar* bad = "not a shader";
  source(vs, 1, &bad, nullptr);
  compile(vs);
  GLint status = 0;
  get_iv(vs, 0x8B81 /*COMPILE_STATUS*/, &status);
  EXPECT_TRUE(status == 0);
  // A failed compile must explain itself (gap writes nothing).
  char log[256] = {};
  GLsizei len = 0;
  get_log(vs, sizeof(log), &len, log);
  EXPECT_TRUE(len > 0);
  EXPECT_TRUE(log[0] != '\0');
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, DebugControlValidation) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto control =
      lib.sym<void (*)(GLenum, GLenum, GLenum, GLsizei, const GLuint*,
                       GLboolean)>("glDebugMessageControl");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(control && get_error);
  if (!control) return;

  control(0x9999u /*bad source*/, 0x824Cu /*ERROR*/, 0x9146u /*HIGH*/, 0,
          nullptr, 1);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  control(0x8246u /*API*/, 0x824Cu, 0x9146u, 0, nullptr, 1);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, FramebufferStatusViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen_tex = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bind_tex = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto storage = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>(
      "glTexStorage2D");
  auto gen_fbo = lib.sym<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
  auto bind_fbo = lib.sym<void (*)(GLenum, GLuint)>("glBindFramebuffer");
  auto attach = lib.sym<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>(
      "glFramebufferTexture2D");
  auto status = lib.sym<GLenum (*)(GLenum)>("glCheckFramebufferStatus");
  auto draw_buffers = lib.sym<void (*)(GLsizei, const GLenum*)>(
      "glDrawBuffers");
  auto read_buffer = lib.sym<void (*)(GLenum)>("glReadBuffer");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen_tex && storage && gen_fbo && attach && status &&
              draw_buffers && read_buffer && get_error);
  if (!status) return;

  GLuint t = 0, f = 0;
  gen_tex(1, &t);
  bind_tex(0x0DE1 /*TEXTURE_2D*/, t);
  storage(0x0DE1, 1, 0x8058 /*RGBA8*/, 16, 16);
  gen_fbo(1, &f);
  bind_fbo(0x8CA9 /*FRAMEBUFFER*/, f);
  attach(0x8CA9, 0x8CE0 /*COLOR_ATTACHMENT0*/, 0x0DE1, t, 0);
  // A complete color-only FBO reports COMPLETE (gap returns 0).
  EXPECT_EQ(status(0x8CA9), (GLenum)0x8CD5 /*FRAMEBUFFER_COMPLETE*/);
  const GLenum buf = 0x8CE0;
  draw_buffers(1, &buf);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  read_buffer(0x8CE0);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, ProgramReflectionViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto create_shader = lib.sym<GLuint (*)(GLenum)>("glCreateShader");
  auto source = lib.sym<void (*)(GLuint, GLsizei, const GLchar* const*,
                                 const GLint*)>("glShaderSource");
  auto compile = lib.sym<void (*)(GLuint)>("glCompileShader");
  auto create_program = lib.sym<GLuint (*)()>("glCreateProgram");
  auto attach = lib.sym<void (*)(GLuint, GLuint)>("glAttachShader");
  auto link = lib.sym<void (*)(GLuint)>("glLinkProgram");
  auto get_iv = lib.sym<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
  auto get_active = lib.sym<void (*)(GLuint, GLuint, GLsizei, GLsizei*,
                                     GLint*, GLenum*, GLchar*)>(
      "glGetActiveUniform");
  auto get_res = lib.sym<GLuint (*)(GLuint, GLenum, const GLchar*)>(
      "glGetProgramResourceIndex");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(create_shader && link && get_iv && get_active && get_res &&
              get_error);
  if (!get_active) return;

  const GLchar* vs_src =
      (const GLchar*)"#version 320 es\n"
                     "layout(location = 0) in vec3 a_pos;\n"
                     "void main() { gl_Position = vec4(a_pos, 1.0); }\n";
  const GLchar* fs_src =
      (const GLchar*)"#version 320 es\n"
                     "precision mediump float;\n"
                     "uniform vec4 u_color;\n"
                     "layout(location = 0) out vec4 o_color;\n"
                     "void main() { o_color = u_color; }\n";
  GLuint vs = create_shader(0x8B31u), fs = create_shader(0x8B30u);
  source(vs, 1, &vs_src, nullptr);
  source(fs, 1, &fs_src, nullptr);
  compile(vs);
  compile(fs);
  GLuint p = create_program();
  attach(p, vs);
  attach(p, fs);
  link(p);
  GLint linked = 0;
  get_iv(p, 0x8B82 /*LINK_STATUS*/, &linked);
  EXPECT_TRUE(linked == 1);

  // The linked uniform reflects back out (gap writes nothing).
  char name[64] = {};
  GLsizei len = 0;
  GLint size = 0;
  GLenum type = 0;
  get_active(p, 0, sizeof(name), &len, &size, &type, name);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(len > 0 && name[0] != '\0');
  EXPECT_TRUE(std::strstr(name, "u_color") != nullptr);
  EXPECT_TRUE(get_res(p, 0x92E1u /*UNIFORM*/, "u_color") != 0xFFFFFFFFu);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, SyncInteger64ViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto get64 =
      lib.sym<void (*)(GLenum, long long*)>("glGetInteger64v");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get64 && get_error);
  if (!get64) return;

  long long value = -1;
  get64(0x9111 /*MAX_SERVER_WAIT_TIMEOUT*/, &value);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(value == 1000000000LL);  // Gap never writes (stays -1).
}

TEST(AbiStatePath, DebugLogRoundTripViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto insert = lib.sym<void (*)(GLenum, GLenum, GLuint, GLenum, GLsizei,
                                 const GLchar*)>("glDebugMessageInsert");
  auto get_log = lib.sym<GLuint (*)(GLuint, GLsizei, GLenum*, GLenum*,
                                    GLuint*, GLenum*, GLsizei*, GLchar*)>(
      "glGetDebugMessageLog");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(insert && get_log && get_error);
  if (!get_log) return;

  const GLchar* text = (const GLchar*)"abi-probe";
  insert(0x824Au /*APPLICATION*/, 0x8268u /*MARKER*/, 41u,
         0x826Bu /*NOTIFICATION*/, -1, text);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  // The stored message reads back out (gap log returns 0, writes nothing).
  GLenum src = 0;
  GLuint id = 0;
  char out[64] = {};
  GLuint n = get_log(1, sizeof(out), &src, nullptr, &id, nullptr, nullptr,
                     out);
  EXPECT_EQ(n, 1u);
  EXPECT_EQ(src, (GLenum)0x824Au);
  EXPECT_EQ(id, 41u);
  EXPECT_TRUE(std::strstr(out, "abi-probe") != nullptr);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, ObjectLabelRoundTripViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto label = lib.sym<void (*)(GLenum, GLuint, GLsizei, const GLchar*)>(
      "glObjectLabel");
  auto get_label = lib.sym<void (*)(GLenum, GLuint, GLsizei, GLsizei*,
                                    GLchar*)>("glGetObjectLabel");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(label && get_label && get_error);
  if (!label) return;

  const GLchar* text = (const GLchar*)"albedo";
  label(0x9152u /*TEXTURE*/, 7u, -1, text);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  char out[64] = {};
  GLsizei len = 0;
  get_label(0x9152u, 7u, sizeof(out), &len, out);
  EXPECT_TRUE(len > 0);
  EXPECT_TRUE(std::strstr(out, "albedo") != nullptr);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, StencilBlendSeparateViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto blend_sep =
      lib.sym<void (*)(GLenum, GLenum, GLenum, GLenum)>("glBlendFuncSeparate");
  auto stencil_func =
      lib.sym<void (*)(GLenum, GLint, GLuint)>("glStencilFunc");
  auto stencil_op =
      lib.sym<void (*)(GLenum, GLenum, GLenum)>("glStencilOp");
  auto color_mask_i =
      lib.sym<void (*)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean)>(
          "glColorMaski");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(blend_sep && stencil_func && stencil_op && color_mask_i &&
              get_error);
  if (!get_error) return;

  blend_sep(0x0302u, 0x0303u, 1u, 0u);
  stencil_func(0x0201u /*LESS*/, 1, 0xFFu);
  stencil_op(0x1E00u /*KEEP*/, 0x1E00u, 0x1E00u);
  color_mask_i(0u, 1, 1, 1, 1);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);

  blend_sep(0x9999u, 0x0303u, 1u, 0u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  stencil_func(0x9999u, 1, 0xFFu);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  stencil_op(0x9999u, 0x1E00u, 0x1E00u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
}

TEST(AbiStatePath, TexSubUploadViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto image = lib.sym<void (*)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                GLint, GLenum, GLenum, const void*)>(
      "glTexImage2D");
  auto sub = lib.sym<void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei,
                              GLenum, GLenum, const void*)>("glTexSubImage2D");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && image && sub && get_error);
  if (!sub) return;

  GLuint t = 0;
  gen(1, &t);
  bind(0x0DE1u, t);
  image(0x0DE1u, 0, 0x8058, 4, 4, 0, 0x1908u, 0x1401u, nullptr);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  const unsigned char patch[2 * 2 * 4] = {9};
  sub(0x0DE1u, 0, 1, 1, 2, 2, 0x1908u, 0x1401u, patch);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  // Overruns the 4x4 level (gap silently accepts).
  sub(0x0DE1u, 0, 3, 3, 2, 2, 0x1908u, 0x1401u, patch);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_VALUE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, ClearMaskViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto clear = lib.sym<void (*)(GLbitfield)>("glClear");
  auto bindF = lib.sym<void (*)(GLenum, GLuint)>("glBindFramebuffer");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(clear && get_error);
  if (!clear) return;

  clear(0xFFFFu);  // No such buffer bits.
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_VALUE);
  // Isolate from prior tests sharing the process-wide HostRuntime singleton:
  // bind the default drawable explicitly before probing default-clear rules.
  if (bindF) bindF(0x8CA9u /*FRAMEBUFFER*/, 0);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  clear(0x00004000u /*COLOR_BUFFER_BIT*/);
  // Default framebuffer has no CPU store: fail closed, never silent.
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, DispatchWithoutProgramViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto dispatch =
      lib.sym<void (*)(GLuint, GLuint, GLuint)>("glDispatchCompute");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(dispatch && get_error);
  if (!dispatch) return;

  // No compute program bound (gap silently accepts).
  dispatch(1, 1, 1);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, HintViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto hint = lib.sym<void (*)(GLenum, GLenum)>("glHint");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(hint && get_error);
  if (!hint) return;

  hint(0x9999u, 0x1100u);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  hint(0x0C50u /*PERSPECTIVE_CORRECTION_HINT*/, 0x1100u /*DONT_CARE*/);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, DebugGroupViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto push = lib.sym<void (*)(GLenum, GLuint, GLsizei, const GLchar*)>(
      "glPushDebugGroup");
  auto pop = lib.sym<void (*)()>("glPopDebugGroup");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(push && pop && get_error);
  if (!push) return;

  const GLchar* text = (const GLchar*)"frame";
  push(0x824Au /*APPLICATION*/, 3u, -1, text);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  pop();
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  pop();  // Empty stack (gap silently accepts).
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, UniformSettersViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto create_shader = lib.sym<GLuint (*)(GLenum)>("glCreateShader");
  auto source = lib.sym<void (*)(GLuint, GLsizei, const GLchar* const*,
                                 const GLint*)>("glShaderSource");
  auto compile = lib.sym<void (*)(GLuint)>("glCompileShader");
  auto create_program = lib.sym<GLuint (*)()>("glCreateProgram");
  auto attach = lib.sym<void (*)(GLuint, GLuint)>("glAttachShader");
  auto link = lib.sym<void (*)(GLuint)>("glLinkProgram");
  auto use = lib.sym<void (*)(GLuint)>("glUseProgram");
  auto get_loc =
      lib.sym<GLint (*)(GLuint, const GLchar*)>("glGetUniformLocation");
  auto uniform1f = lib.sym<void (*)(GLint, GLfloat)>("glUniform1f");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(create_shader && link && use && get_loc && uniform1f &&
              get_error);
  if (!uniform1f) return;

  const GLchar* vs_src =
      (const GLchar*)"#version 320 es\n"
                     "layout(location = 0) in vec3 a_pos;\n"
                     "void main() { gl_Position = vec4(a_pos, 1.0); }\n";
  const GLchar* fs_src =
      (const GLchar*)"#version 320 es\n"
                     "precision mediump float;\n"
                     "uniform float u_alpha;\n"
                     "layout(location = 0) out vec4 o_color;\n"
                     "void main() { o_color = vec4(1.0, 1.0, 1.0, u_alpha); }\n";
  GLuint vs = create_shader(0x8B31u), fs = create_shader(0x8B30u);
  source(vs, 1, &vs_src, nullptr);
  source(fs, 1, &fs_src, nullptr);
  compile(vs);
  compile(fs);
  GLuint p = create_program();
  attach(p, vs);
  attach(p, fs);
  link(p);
  use(p);
  GLint loc = get_loc(p, "u_alpha");
  EXPECT_TRUE(loc >= 0);
  uniform1f(loc, 0.5f);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  uniform1f(-1, 0.5f);  // -1 locations are silently ignored.
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  // No current program: setting fails closed (gap silently accepts).
  use(0);
  uniform1f(loc, 0.5f);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, SamplerAndIntegerAttribViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen_samp = lib.sym<void (*)(GLsizei, GLuint*)>("glGenSamplers");
  auto bind_samp = lib.sym<void (*)(GLuint, GLuint)>("glBindSampler");
  auto param_i =
      lib.sym<void (*)(GLuint, GLenum, GLint)>("glSamplerParameteri");
  auto gen_vao = lib.sym<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
  auto bind_vao = lib.sym<void (*)(GLuint)>("glBindVertexArray");
  auto ipointer = lib.sym<void (*)(GLuint, GLint, GLenum, GLsizei,
                                   const void*)>("glVertexAttribIPointer");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen_samp && bind_samp && param_i && gen_vao && ipointer &&
              get_error);
  if (!param_i) return;

  param_i(9999u, 0x2801u /*TEXTURE_MIN_FILTER*/, 0x2601u /*LINEAR*/);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);  // Never created.
  GLuint s = 0;
  gen_samp(1, &s);
  bind_samp(0, s);
  param_i(s, 0x2801u, 0x2601u);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);

  GLuint vao = 0;
  gen_vao(1, &vao);
  bind_vao(vao);
  ipointer(0u, 4, 0x1404u /*INT*/, 0, nullptr);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  ipointer(99u, 4, 0x1404u, 0, nullptr);  // Past 16 attribs.
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_VALUE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, FinishFlushSmoke) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto finish = lib.sym<void (*)()>("glFinish");
  auto flush = lib.sym<void (*)()>("glFlush");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(finish && flush && get_error);
  if (!finish) return;
  finish();
  flush();
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiStatePath, ClearValuesAreAccepted) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto clear_color =
      lib.sym<void (*)(GLfloat, GLfloat, GLfloat, GLfloat)>("glClearColor");
  auto clear_depth = lib.sym<void (*)(GLfloat)>("glClearDepthf");
  auto clear_stencil = lib.sym<void (*)(GLint)>("glClearStencil");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(clear_color && clear_depth && clear_stencil && get_error);
  if (!clear_color || !clear_depth || !clear_stencil || !get_error) return;

  clear_color(0.25f, 0.5f, 1.0f, 1.0f);
  clear_depth(0.5f);
  clear_stencil(7);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}
