// Exam-style uniform/reflection path (TDD red-first): the biggest remaining
// Tier-2 family, driven through the real built library via dlopen like CTS
// and MobileGL call it. Setters must store, reads must read back, reflection
// must reflect the link parse — a gap records and returns zeros, so every
// value assertion below fails red until the entry is real.

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
  const EGLint cfg_attribs[] = {0x3040, 0x40, 0x3038};
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

// Links a program with float/vec3/ivec2/mat3 uniforms plus one uniform
// block (Camera). Returns 0 when any step is missing.
GLuint LinkTestProgram(Lib& lib) {
  auto create_shader = lib.sym<GLuint (*)(GLenum)>("glCreateShader");
  auto source = lib.sym<void (*)(GLuint, GLsizei, const GLchar* const*,
                                 const GLint*)>("glShaderSource");
  auto compile = lib.sym<void (*)(GLuint)>("glCompileShader");
  auto create_program = lib.sym<GLuint (*)()>("glCreateProgram");
  auto attach = lib.sym<void (*)(GLuint, GLuint)>("glAttachShader");
  auto link = lib.sym<void (*)(GLuint)>("glLinkProgram");
  auto get_iv = lib.sym<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
  if (!create_shader || !link || !get_iv) return 0;
  const GLchar* vs_src =
      (const GLchar*)"#version 320 es\n"
                     "layout(std140) uniform Camera { mat4 vp; };\n"
                     "layout(location = 0) in vec3 a_pos;\n"
                     "uniform float u_alpha;\n"
                     "uniform vec3 u_pos;\n"
                     "uniform ivec2 u_pair;\n"
                     "uniform mat3 u_mat;\n"
                     "void main() { gl_Position = vp * vec4(a_pos, u_alpha); }\n";
  const GLchar* fs_src =
      (const GLchar*)"#version 320 es\n"
                     "precision mediump float;\n"
                     "layout(location = 0) out vec4 o_color;\n"
                     "void main() { o_color = vec4(1.0); }\n";
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
  get_iv(p, 0x8B82u /*LINK_STATUS*/, &linked);
  return linked == 1 ? p : 0;
}

}  // namespace

TEST(AbiUniformPath, SettersStoreAndReadsReadBack) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));
  GLuint p = LinkTestProgram(lib);
  EXPECT_TRUE(p != 0u);
  if (p == 0u) return;

  auto use = lib.sym<void (*)(GLuint)>("glUseProgram");
  auto get_loc =
      lib.sym<GLint (*)(GLuint, const GLchar*)>("glGetUniformLocation");
  auto u1f = lib.sym<void (*)(GLint, GLfloat)>("glUniform1f");
  auto u3f =
      lib.sym<void (*)(GLint, GLfloat, GLfloat, GLfloat)>("glUniform3f");
  auto u2i = lib.sym<void (*)(GLint, GLint, GLint)>("glUniform2i");
  auto m3f = lib.sym<void (*)(GLint, GLsizei, GLboolean, const GLfloat*)>(
      "glUniformMatrix3fv");
  auto pu1f =
      lib.sym<void (*)(GLuint, GLint, GLfloat)>("glProgramUniform1f");
  auto get_fv = lib.sym<void (*)(GLuint, GLint, GLfloat*)>("glGetUniformfv");
  auto get_iv = lib.sym<void (*)(GLuint, GLint, GLint*)>("glGetUniformiv");
  auto get_nfv = lib.sym<void (*)(GLuint, GLint, GLsizei, GLfloat*)>(
      "glGetnUniformfv");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(use && get_loc && u1f && u3f && u2i && m3f && pu1f && get_fv &&
              get_iv && get_nfv && get_error);
  if (!u1f) return;

  use(p);
  GLint loc_a = get_loc(p, "u_alpha");
  GLint loc_p = get_loc(p, "u_pos");
  GLint loc_pair = get_loc(p, "u_pair");
  GLint loc_m = get_loc(p, "u_mat");
  EXPECT_TRUE(loc_a >= 0 && loc_p >= 0 && loc_pair >= 0 && loc_m >= 0);

  // Float uniforms round-trip exactly (gap writes nothing: stale zeros).
  u1f(loc_a, 0.5f);
  GLfloat back_a = -1.0f;
  get_fv(p, loc_a, &back_a);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(back_a == 0.5f);
  u3f(loc_p, 1.0f, 2.0f, 3.0f);
  GLfloat back_p[3] = {-1, -1, -1};
  get_fv(p, loc_p, back_p);
  EXPECT_TRUE(back_p[0] == 1.0f && back_p[1] == 2.0f && back_p[2] == 3.0f);

  // Integer uniforms round-trip as integers (stored exactly for small ints).
  u2i(loc_pair, 7, 8);
  GLint back_pair[2] = {-1, -1};
  get_iv(p, loc_pair, back_pair);
  EXPECT_TRUE(back_pair[0] == 7 && back_pair[1] == 8);

  // Matrices come back column-major, all 9 floats.
  const GLfloat ident9[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  m3f(loc_m, 1, 0, ident9);
  GLfloat back_m[9];
  for (int i = 0; i < 9; ++i) back_m[i] = -1.0f;
  get_fv(p, loc_m, back_m);
  for (int i = 0; i < 9; ++i) EXPECT_TRUE(back_m[i] == ident9[i]);

  // Robust read clamps by element count, not bytes (see gl_real.cpp note).
  GLfloat back_n[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
  get_nfv(p, loc_m, 9, back_n);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  for (int i = 0; i < 9; ++i) EXPECT_TRUE(back_n[i] == ident9[i]);
  get_nfv(p, loc_m, 8, back_n);  // One element short of 9.
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);

  // ProgramUniform* hits an idle program: unbind first, set, read back.
  use(0);
  pu1f(p, loc_a, 0.75f);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  get_fv(p, loc_a, &back_a);
  EXPECT_TRUE(back_a == 0.75f);

  // Type errors fail closed (float setter on an ivec2).
  use(p);
  u1f(loc_pair, 1.0f);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  // No current program fails closed too.
  use(0);
  u1f(loc_a, 1.0f);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  // Unknown program fails closed (spec 7.12: INVALID_VALUE for a name that
  // is neither program nor shader — verified in es_spec_3.2.pdf p.152).
  get_fv(9999u, loc_a, &back_a);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_VALUE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiUniformPath, UniformIndicesViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));
  GLuint p = LinkTestProgram(lib);
  EXPECT_TRUE(p != 0u);
  if (p == 0u) return;

  auto get_indices = lib.sym<void (*)(GLuint, GLsizei, const GLchar* const*,
                                      GLuint*)>("glGetUniformIndices");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get_indices && get_error);
  if (!get_indices) return;

  const GLchar* names[2] = {(const GLchar*)"u_alpha",
                            (const GLchar*)"nope"};
  GLuint indices[2] = {0xFFFFFFFFu, 42u};
  get_indices(p, 2, names, indices);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(indices[0] != 0xFFFFFFFFu);  // Active uniform found.
  EXPECT_EQ(indices[1], 0xFFFFFFFFu);      // Inactive -> INVALID_INDEX.
}

TEST(AbiUniformPath, InterfaceAndBlockReflectionViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));
  GLuint p = LinkTestProgram(lib);
  EXPECT_TRUE(p != 0u);
  if (p == 0u) return;

  auto get_iface =
      lib.sym<void (*)(GLuint, GLenum, GLenum, GLint*)>(
          "glGetProgramInterfaceiv");
  auto get_block_name = lib.sym<void (*)(GLuint, GLuint, GLsizei, GLsizei*,
                                         GLchar*)>("glGetActiveUniformBlockName");
  auto get_block_iv = lib.sym<void (*)(GLuint, GLuint, GLenum, GLint*)>(
      "glGetActiveUniformBlockiv");
  auto get_active_iv = lib.sym<void (*)(GLuint, GLsizei, const GLuint*,
                                        GLenum, GLint*)>(
      "glGetActiveUniformsiv");
  auto get_res_name = lib.sym<void (*)(GLuint, GLenum, GLuint, GLsizei,
                                       GLsizei*, GLchar*)>(
      "glGetProgramResourceName");
  auto get_res_loc = lib.sym<GLint (*)(GLuint, GLenum, const GLchar*)>(
      "glGetProgramResourceLocation");
  auto get_loc =
      lib.sym<GLint (*)(GLuint, const GLchar*)>("glGetUniformLocation");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get_iface && get_block_name && get_block_iv && get_active_iv &&
              get_res_name && get_res_loc && get_loc && get_error);
  if (!get_iface) return;

  GLint count = -1;
  get_iface(p, 0x92E1u /*UNIFORM*/, 0x92F5u /*ACTIVE_RESOURCES*/, &count);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  // u_alpha, u_pos, u_pair, u_mat (block members are NOT default-block
  // uniforms: the link regex only matches declarations with the uniform
  // keyword, so `mat4 vp;` inside the block stays out — verified above).
  EXPECT_TRUE(count == 4);
  get_iface(p, 0x92E2u /*UNIFORM_BLOCK*/, 0x92F5u, &count);
  EXPECT_TRUE(count == 1);  // Camera.
  get_iface(p, 0x92E3u /*PROGRAM_INPUT*/, 0x92F5u, &count);
  EXPECT_TRUE(count == 1);  // a_pos.

  char block[64] = {};
  GLsizei block_len = 0;
  get_block_name(p, 0, sizeof(block), &block_len, block);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(std::strstr(block, "Camera") != nullptr);
  GLint binding = -1;
  get_block_iv(p, 0, 0x8A3Fu /*UNIFORM_BLOCK_BINDING*/, &binding);
  EXPECT_TRUE(binding == 0);
  GLint name_len = 0;
  get_block_iv(p, 0, 0x8A41u /*UNIFORM_BLOCK_NAME_LENGTH*/, &name_len);
  EXPECT_TRUE(name_len == block_len + 1);

  // Active-uniform query by resolved index (u_alpha, a float).
  auto get_indices = lib.sym<void (*)(GLuint, GLsizei, const GLchar* const*,
                                      GLuint*)>("glGetUniformIndices");
  EXPECT_TRUE(get_indices != nullptr);
  if (get_indices == nullptr) return;
  const GLchar* uname[1] = {(const GLchar*)"u_alpha"};
  GLuint uidx[1] = {0xFFFFFFFFu};
  get_indices(p, 1, uname, uidx);
  EXPECT_TRUE(uidx[0] != 0xFFFFFFFFu);
  GLint type = 0, size = 0;
  get_active_iv(p, 1, uidx, 0x8A37u /*UNIFORM_TYPE*/, &type);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(type == 0x1406 /*FLOAT*/);
  get_active_iv(p, 1, uidx, 0x8A38u /*UNIFORM_SIZE*/, &size);
  EXPECT_TRUE(size == 1);

  // Resource name/location round-trip on the UNIFORM interface.
  char res_name[64] = {};
  get_res_name(p, 0x92E1u /*UNIFORM*/, uidx[0], sizeof(res_name), nullptr,
               res_name);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(std::strstr(res_name, "u_alpha") != nullptr);
  EXPECT_EQ(get_res_loc(p, 0x92E1u, "u_alpha"), get_loc(p, "u_alpha"));
  EXPECT_EQ(get_res_loc(p, 0x92E1u, "nope"), -1);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}


TEST(AbiUniformPath, AttachedShadersViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));
  GLuint p = LinkTestProgram(lib);
  EXPECT_TRUE(p != 0u);
  if (p == 0u) return;

  auto get_attached = lib.sym<void (*)(GLuint, GLsizei, GLsizei*, GLuint*)>(
      "glGetAttachedShaders");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get_attached && get_error);
  if (!get_attached) return;

  GLuint shaders[4] = {};
  GLsizei count = -1;
  get_attached(p, 4, &count, shaders);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_EQ(count, 2);  // vs + fs (gap writes nothing: count stays -1).
  EXPECT_TRUE(shaders[0] != 0u && shaders[1] != 0u);
}

TEST(AbiUniformPath, PrecisionFormatViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto precision = lib.sym<void (*)(GLenum, GLenum, GLint*, GLint*)>(
      "glGetShaderPrecisionFormat");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(precision && get_error);
  if (!precision) return;

  // IEEE single for highp float, both stages (spec formula + GLSL ES 3.20
  // Table 4.7.1; TGL stores everything at full width — see gl_real.cpp).
  GLint range[2] = {-1, -1};
  GLint prec = -1;
  precision(0x8B31u /*VERTEX_SHADER*/, 0x8DF0u /*HIGH_FLOAT*/, range, &prec);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(range[0] == 127 && range[1] == 127 && prec == 23);
  precision(0x8B30u /*FRAGMENT_SHADER*/, 0x8DF5u /*HIGH_INT*/, range, &prec);
  EXPECT_TRUE(range[0] == 31 && range[1] == 30 && prec == 0);
  precision(0x8DD9u /*GEOMETRY (bad stage)*/, 0x8DF0u, range, &prec);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiUniformPath, MapBufferOESViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenBuffers");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindBuffer");
  auto data = lib.sym<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>(
      "glBufferData");
  auto map = lib.sym<void* (*)(GLenum, GLenum)>("glMapBufferOES");
  auto unmap = lib.sym<GLboolean (*)(GLenum)>("glUnmapBuffer");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && data && map && unmap && get_error);
  if (!map) return;

  GLuint b = 0;
  gen(1, &b);
  bind(0x8892u /*ARRAY_BUFFER*/, b);
  const unsigned char bytes[16] = {1, 2, 3, 4};
  data(0x8892u, sizeof(bytes), bytes, 0x88E4u /*STATIC_DRAW*/);
  void* ptr = map(0x8892u, 0x88BAu /*READ_WRITE*/);
  EXPECT_TRUE(ptr != nullptr);  // Gap returns null.
  if (ptr != nullptr) {
    EXPECT_TRUE(static_cast<unsigned char*>(ptr)[3] == 4u);
  }
  EXPECT_TRUE(unmap(0x8892u) == GL_TRUE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiUniformPath, VertexAttribReadsViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenVertexArrays");
  auto bind = lib.sym<void (*)(GLuint)>("glBindVertexArray");
  auto attrib4f =
      lib.sym<void (*)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat)>(
          "glVertexAttrib4f");
  auto get_fv =
      lib.sym<void (*)(GLuint, GLenum, GLfloat*)>("glGetVertexAttribfv");
  auto get_iv =
      lib.sym<void (*)(GLuint, GLenum, GLint*)>("glGetVertexAttribiv");
  auto get_iiv =
      lib.sym<void (*)(GLuint, GLenum, GLint*)>("glGetVertexAttribIiv");
  auto get_ptr = lib.sym<void (*)(GLuint, GLenum, void**)>(
      "glGetVertexAttribPointerv");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && attrib4f && get_fv && get_iv && get_iiv &&
              get_ptr && get_error);
  if (!attrib4f) return;

  GLuint vao = 0;
  gen(1, &vao);
  bind(vao);
  attrib4f(3u, 1.0f, 2.0f, 3.0f, 4.0f);
  GLfloat fv[4] = {-1, -1, -1, -1};
  get_fv(3u, 0x8626u /*CURRENT_VERTEX_ATTRIB*/, fv);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(fv[0] == 1.0f && fv[3] == 4.0f);  // Gap never writes.
  GLint iv[4] = {-1, -1, -1, -1};
  get_iv(3u, 0x8626u, iv);
  EXPECT_TRUE(iv[0] == 1 && iv[3] == 4);
  GLint iiv[4] = {-1, -1, -1, -1};
  get_iiv(3u, 0x8626u, iiv);  // Integer view defaults to (0,0,0,1).
  EXPECT_TRUE(iiv[0] == 0 && iiv[3] == 1);
  get_fv(99u, 0x8626u, fv);  // Past 16 attribs.
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_VALUE);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  (void)get_ptr;
}

TEST(AbiUniformPath, TexLevelViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenTextures");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindTexture");
  auto storage = lib.sym<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>(
      "glTexStorage2D");
  auto get_level = lib.sym<void (*)(GLenum, GLint, GLenum, GLint*)>(
      "glGetTexLevelParameteriv");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && storage && get_level && get_error);
  if (!get_level) return;

  GLuint t = 0;
  gen(1, &t);
  bind(0x0DE1u, t);
  storage(0x0DE1u, 1, 0x8058u /*RGBA8*/, 8, 8);
  GLint width = -1, format = -1;
  get_level(0x0DE1u, 0, 0x1000u /*TEXTURE_WIDTH*/, &width);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(width == 8);  // Gap never writes (stays -1).
  get_level(0x0DE1u, 0, 0x1003u /*TEXTURE_INTERNAL_FORMAT*/, &format);
  EXPECT_TRUE(format == 0x8058);
}

TEST(AbiUniformPath, SamplerIntegerParamsViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenSamplers");
  auto bind = lib.sym<void (*)(GLuint, GLuint)>("glBindSampler");
  auto param_iiv = lib.sym<void (*)(GLuint, GLenum, const GLint*)>(
      "glSamplerParameterIiv");
  auto get_iiv = lib.sym<void (*)(GLuint, GLenum, GLint*)>(
      "glGetSamplerParameterIiv");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && param_iiv && get_iiv && get_error);
  if (!param_iiv) return;

  GLuint s = 0;
  gen(1, &s);
  bind(0, s);  // Sampler names become objects on first bind (pinned).
  const GLint linear = 0x2601;
  param_iiv(s, 0x2801u /*TEXTURE_MIN_FILTER*/, &linear);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  GLint back = -1;
  get_iiv(s, 0x2801u, &back);
  EXPECT_TRUE(back == 0x2601);  // Gap never writes (stays -1).
  // Float-valued params reject integer reads (spec: type must match family).
  get_iiv(s, 0x1004u /*TEXTURE_BORDER_COLOR*/, &back);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_OPERATION);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiUniformPath, RenderbufferParamViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenRenderbuffers");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindRenderbuffer");
  auto storage = lib.sym<void (*)(GLenum, GLenum, GLsizei, GLsizei)>(
      "glRenderbufferStorage");
  auto get_param = lib.sym<void (*)(GLenum, GLenum, GLint*)>(
      "glGetRenderbufferParameteriv");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && storage && get_param && get_error);
  if (!get_param) return;

  GLuint r = 0;
  gen(1, &r);
  bind(0x8D41u /*RENDERBUFFER*/, r);
  storage(0x8D41u, 0x8058u /*RGBA8*/, 16, 16);
  GLint width = -1;
  get_param(0x8D41u, 0x8D42u /*RENDERBUFFER_WIDTH*/, &width);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(width == 16);  // Gap never writes (stays -1).
}

TEST(AbiUniformPath, FboDefaultsViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenFramebuffers");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindFramebuffer");
  auto param = lib.sym<void (*)(GLenum, GLenum, GLint)>(
      "glFramebufferParameteri");
  auto get_param = lib.sym<void (*)(GLenum, GLenum, GLint*)>(
      "glGetFramebufferParameteriv");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && param && get_param && get_error);
  if (!get_param) return;

  GLuint f = 0;
  gen(1, &f);
  bind(0x8CA9u /*DRAW_FRAMEBUFFER*/, f);
  param(0x8CA9u, 0x9310u /*FRAMEBUFFER_DEFAULT_WIDTH*/, 64);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  GLint width = -1;
  get_param(0x8CA9u, 0x9310u, &width);
  EXPECT_TRUE(width == 64);  // Gap never writes (stays -1).
}

TEST(AbiUniformPath, FragDataLocationIsMinusOne) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));
  GLuint p = LinkTestProgram(lib);
  EXPECT_TRUE(p != 0u);
  if (p == 0u) return;

  auto get_frag = lib.sym<GLint (*)(GLuint, const GLchar*)>(
      "glGetFragDataLocation");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get_frag && get_error);
  if (!get_frag) return;

  // No color-number bindings exist (BindFragDataLocationEXT is a gap), so
  // every name is unbound: -1 with no error (documented in gl_real.cpp).
  EXPECT_EQ(get_frag(p, "o_color"), -1);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiUniformPath, CreateShaderProgramViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto create_progs = lib.sym<GLuint (*)(GLenum, GLsizei, const GLchar* const*)>(
      "glCreateShaderProgramv");
  auto get_iv = lib.sym<void (*)(GLuint, GLenum, GLint*)>("glGetProgramiv");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(create_progs && get_iv && get_error);
  if (!create_progs) return;

  const GLchar* vs_src =
      (const GLchar*)"#version 320 es\n"
                     "layout(location = 0) in vec3 a_pos;\n"
                     "void main() { gl_Position = vec4(a_pos, 1.0); }\n";
  GLuint p = create_progs(0x8B31u /*VERTEX_SHADER*/, 1, &vs_src);
  EXPECT_TRUE(p != 0u);  // Gap returns 0.
  if (p == 0u) return;
  GLint linked = 0;
  get_iv(p, 0x8B82u /*LINK_STATUS*/, &linked);
  EXPECT_TRUE(linked == 1);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiUniformPath, Buffer64ViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto gen = lib.sym<void (*)(GLsizei, GLuint*)>("glGenBuffers");
  auto bind = lib.sym<void (*)(GLenum, GLuint)>("glBindBuffer");
  auto data = lib.sym<void (*)(GLenum, GLsizeiptr, const void*, GLenum)>(
      "glBufferData");
  auto get64 = lib.sym<void (*)(GLenum, GLenum, GLint64*)>(
      "glGetBufferParameteri64v");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(gen && bind && data && get64 && get_error);
  if (!get64) return;

  GLuint b = 0;
  gen(1, &b);
  bind(0x8892u, b);
  const unsigned char bytes[16] = {};
  data(0x8892u, sizeof(bytes), bytes, 0x88E4u);
  GLint64 size = -1;
  get64(0x8892u, 0x8764u /*BUFFER_SIZE*/, &size);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(size == 16);  // Gap never writes (stays -1).
}

TEST(AbiUniformPath, Integer64IndexedViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto get64i =
      lib.sym<void (*)(GLenum, GLuint, GLint64*)>("glGetInteger64i_v");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get64i && get_error);
  if (!get64i) return;

  GLint64 count = -1;
  get64i(0x91BEu /*MAX_COMPUTE_WORK_GROUP_COUNT*/, 0, &count);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(count == 65535);  // Gap never writes (stays -1).
}

TEST(AbiUniformPath, BooleanIndexedViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto get_bi =
      lib.sym<void (*)(GLenum, GLuint, GLboolean*)>("glGetBooleani_v");
  auto color_mask_i =
      lib.sym<void (*)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean)>(
          "glColorMaski");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get_bi && color_mask_i && get_error);
  if (!get_bi) return;

  GLboolean mask[4] = {0, 0, 0, 0};
  get_bi(0x0C23u /*COLOR_WRITEMASK*/, 0, mask);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(mask[0] && mask[1] && mask[2] && mask[3]);  // Default all on.
  color_mask_i(0u, 0, 1, 0, 1);
  get_bi(0x0C23u, 0, mask);
  EXPECT_TRUE(!mask[0] && mask[1] && !mask[2] && mask[3]);
  get_bi(0x9999u, 0, mask);  // Only COLOR_WRITEMASK is modeled.
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}

TEST(AbiUniformPath, BooleanFloatMirrorViaAbi) {
  Lib lib(TGLES_HOST_LIBRARY_PATH);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  EXPECT_TRUE(MakePbufferCurrent(lib));

  auto get_b = lib.sym<void (*)(GLenum, GLboolean*)>("glGetBooleanv");
  auto get_f = lib.sym<void (*)(GLenum, GLfloat*)>("glGetFloatv");
  auto enable = lib.sym<void (*)(GLenum)>("glEnable");
  auto get_error = lib.sym<GLenum (*)()>("glGetError");
  EXPECT_TRUE(get_b && get_f && enable && get_error);
  if (!get_b) return;

  GLboolean b = 42;
  get_b(0x0BE2u /*BLEND*/, &b);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
  EXPECT_TRUE(b == GL_FALSE);
  enable(0x0BE2u);
  get_b(0x0BE2u, &b);
  EXPECT_TRUE(b == GL_TRUE);
  GLfloat f = -1.0f;
  get_f(0x0D33u /*MAX_TEXTURE_SIZE*/, &f);
  EXPECT_TRUE(f == 8192.0f);  // Gap never writes (stays -1).
  get_b(0x9999u, &b);
  EXPECT_EQ(get_error(), (GLenum)GL_INVALID_ENUM);
  EXPECT_EQ(get_error(), (GLenum)GL_NO_ERROR);
}
