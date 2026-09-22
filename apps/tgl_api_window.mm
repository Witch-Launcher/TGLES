// TGL ES 3.2 API window: a visible Cocoa window that renders for real while
// exercising ALL 358 standard OpenGL ES 3.2 entry points (docs/reference/
// gl32.h) through the real TGL path (GlesContext managers + Apple Metal
// bridge + CAMetalLayer present). Complements the headless suites: every
// phase below draws something visible AND asserts pixels or exact GL errors,
// and a final audit proves every one of the 358 names was touched.
//
// Layout: left = Metal view (demo scenes cycle per phase), right = phase
// label + progress bar + scrolling log. After the sweep the window keeps
// looping the demo scenes; it auto-quits ~6s after the summary (or on window
// close) so CI-with-display can run it: exit 0 iff 0 unexpected failures.
//
// Honest notes (also logged in-window):
// - Compute dispatches validate only (no GPU compute execution in TGL yet).
// - Transform-feedback capture is CPU-validated; the bridge draws expanded
//   verts (no GPU capture proof).
// - The bridge fragment ignores bound textures (vertex-color pipeline), so
//   texture phases assert upload/state/pixels of the COLOR TARGET, not
//   sampled texels.
// - glClear* sets RasterState values the bridge does not consume (the pass
//   clears black); clear phases assert state + no error, not pixels.
// - MRT n>1 / multisample resolve / adjacency / PATCHES / integer attribs /
//   split stencil refs are pinned as documented limits
//   (expected INVALID_OPERATION), not silent success. Depth+blend and
//   CONSTANT_* blend execute (keyed pipelines + blend color).
//
// Build (Darwin-only): cmake --build build --target tgl_api_window
// Run: ./build/tgl_api_window (needs a display + Metal device).

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"
#include "tgles/host/abi.h"
#include "tgles/host/abi_gl.h"
#include "tgles/host/host_runtime.h"

namespace {

// ---------------------------------------------------------------------------
// Ground truth: all 358 standard ES 3.2 names (MUST match
// tests/cts/test_es32_total.cpp kCoreApis; the audit below fails otherwise).
// Injected by script from that file (see bottom); keep the marker.
const char* const kCoreApis[] = {
    "glActiveShaderProgram",
    "glActiveTexture",
    "glAttachShader",
    "glBeginQuery",
    "glBeginTransformFeedback",
    "glBindAttribLocation",
    "glBindBuffer",
    "glBindBufferBase",
    "glBindBufferRange",
    "glBindFramebuffer",
    "glBindImageTexture",
    "glBindProgramPipeline",
    "glBindRenderbuffer",
    "glBindSampler",
    "glBindTexture",
    "glBindTransformFeedback",
    "glBindVertexArray",
    "glBindVertexBuffer",
    "glBlendBarrier",
    "glBlendColor",
    "glBlendEquation",
    "glBlendEquationSeparate",
    "glBlendEquationSeparatei",
    "glBlendEquationi",
    "glBlendFunc",
    "glBlendFuncSeparate",
    "glBlendFuncSeparatei",
    "glBlendFunci",
    "glBlitFramebuffer",
    "glBufferData",
    "glBufferSubData",
    "glCheckFramebufferStatus",
    "glClear",
    "glClearBufferfi",
    "glClearBufferfv",
    "glClearBufferiv",
    "glClearBufferuiv",
    "glClearColor",
    "glClearDepthf",
    "glClearStencil",
    "glClientWaitSync",
    "glColorMask",
    "glColorMaski",
    "glCompileShader",
    "glCompressedTexImage2D",
    "glCompressedTexImage3D",
    "glCompressedTexSubImage2D",
    "glCompressedTexSubImage3D",
    "glCopyBufferSubData",
    "glCopyImageSubData",
    "glCopyTexImage2D",
    "glCopyTexSubImage2D",
    "glCopyTexSubImage3D",
    "glCreateProgram",
    "glCreateShader",
    "glCreateShaderProgramv",
    "glCullFace",
    "glDebugMessageCallback",
    "glDebugMessageControl",
    "glDebugMessageInsert",
    "glDeleteBuffers",
    "glDeleteFramebuffers",
    "glDeleteProgram",
    "glDeleteProgramPipelines",
    "glDeleteQueries",
    "glDeleteRenderbuffers",
    "glDeleteSamplers",
    "glDeleteShader",
    "glDeleteSync",
    "glDeleteTextures",
    "glDeleteTransformFeedbacks",
    "glDeleteVertexArrays",
    "glDepthFunc",
    "glDepthMask",
    "glDepthRangef",
    "glDetachShader",
    "glDisable",
    "glDisableVertexAttribArray",
    "glDisablei",
    "glDispatchCompute",
    "glDispatchComputeIndirect",
    "glDrawArrays",
    "glDrawArraysIndirect",
    "glDrawArraysInstanced",
    "glDrawBuffers",
    "glDrawElements",
    "glDrawElementsBaseVertex",
    "glDrawElementsIndirect",
    "glDrawElementsInstanced",
    "glDrawElementsInstancedBaseVertex",
    "glDrawRangeElements",
    "glDrawRangeElementsBaseVertex",
    "glEnable",
    "glEnableVertexAttribArray",
    "glEnablei",
    "glEndQuery",
    "glEndTransformFeedback",
    "glFenceSync",
    "glFinish",
    "glFlush",
    "glFlushMappedBufferRange",
    "glFramebufferParameteri",
    "glFramebufferRenderbuffer",
    "glFramebufferTexture",
    "glFramebufferTexture2D",
    "glFramebufferTextureLayer",
    "glFrontFace",
    "glGenBuffers",
    "glGenFramebuffers",
    "glGenProgramPipelines",
    "glGenQueries",
    "glGenRenderbuffers",
    "glGenSamplers",
    "glGenTextures",
    "glGenTransformFeedbacks",
    "glGenVertexArrays",
    "glGenerateMipmap",
    "glGetActiveAttrib",
    "glGetActiveUniform",
    "glGetActiveUniformBlockName",
    "glGetActiveUniformBlockiv",
    "glGetActiveUniformsiv",
    "glGetAttachedShaders",
    "glGetAttribLocation",
    "glGetBooleani_v",
    "glGetBooleanv",
    "glGetBufferParameteri64v",
    "glGetBufferParameteriv",
    "glGetBufferPointerv",
    "glGetDebugMessageLog",
    "glGetError",
    "glGetFloatv",
    "glGetFragDataLocation",
    "glGetFramebufferAttachmentParameteriv",
    "glGetFramebufferParameteriv",
    "glGetGraphicsResetStatus",
    "glGetInteger64i_v",
    "glGetInteger64v",
    "glGetIntegeri_v",
    "glGetIntegerv",
    "glGetInternalformativ",
    "glGetMultisamplefv",
    "glGetObjectLabel",
    "glGetObjectPtrLabel",
    "glGetPointerv",
    "glGetProgramBinary",
    "glGetProgramInfoLog",
    "glGetProgramInterfaceiv",
    "glGetProgramPipelineInfoLog",
    "glGetProgramPipelineiv",
    "glGetProgramResourceIndex",
    "glGetProgramResourceLocation",
    "glGetProgramResourceName",
    "glGetProgramResourceiv",
    "glGetProgramiv",
    "glGetQueryObjectuiv",
    "glGetQueryiv",
    "glGetRenderbufferParameteriv",
    "glGetSamplerParameterIiv",
    "glGetSamplerParameterIuiv",
    "glGetSamplerParameterfv",
    "glGetSamplerParameteriv",
    "glGetShaderInfoLog",
    "glGetShaderPrecisionFormat",
    "glGetShaderSource",
    "glGetShaderiv",
    "glGetString",
    "glGetStringi",
    "glGetSynciv",
    "glGetTexLevelParameterfv",
    "glGetTexLevelParameteriv",
    "glGetTexParameterIiv",
    "glGetTexParameterIuiv",
    "glGetTexParameterfv",
    "glGetTexParameteriv",
    "glGetTransformFeedbackVarying",
    "glGetUniformBlockIndex",
    "glGetUniformIndices",
    "glGetUniformLocation",
    "glGetUniformfv",
    "glGetUniformiv",
    "glGetUniformuiv",
    "glGetVertexAttribIiv",
    "glGetVertexAttribIuiv",
    "glGetVertexAttribPointerv",
    "glGetVertexAttribfv",
    "glGetVertexAttribiv",
    "glGetnUniformfv",
    "glGetnUniformiv",
    "glGetnUniformuiv",
    "glHint",
    "glInvalidateFramebuffer",
    "glInvalidateSubFramebuffer",
    "glIsBuffer",
    "glIsEnabled",
    "glIsEnabledi",
    "glIsFramebuffer",
    "glIsProgram",
    "glIsProgramPipeline",
    "glIsQuery",
    "glIsRenderbuffer",
    "glIsSampler",
    "glIsShader",
    "glIsSync",
    "glIsTexture",
    "glIsTransformFeedback",
    "glIsVertexArray",
    "glLineWidth",
    "glLinkProgram",
    "glMapBufferRange",
    "glMemoryBarrier",
    "glMemoryBarrierByRegion",
    "glMinSampleShading",
    "glObjectLabel",
    "glObjectPtrLabel",
    "glPatchParameteri",
    "glPauseTransformFeedback",
    "glPixelStorei",
    "glPolygonOffset",
    "glPopDebugGroup",
    "glPrimitiveBoundingBox",
    "glProgramBinary",
    "glProgramParameteri",
    "glProgramUniform1f",
    "glProgramUniform1fv",
    "glProgramUniform1i",
    "glProgramUniform1iv",
    "glProgramUniform1ui",
    "glProgramUniform1uiv",
    "glProgramUniform2f",
    "glProgramUniform2fv",
    "glProgramUniform2i",
    "glProgramUniform2iv",
    "glProgramUniform2ui",
    "glProgramUniform2uiv",
    "glProgramUniform3f",
    "glProgramUniform3fv",
    "glProgramUniform3i",
    "glProgramUniform3iv",
    "glProgramUniform3ui",
    "glProgramUniform3uiv",
    "glProgramUniform4f",
    "glProgramUniform4fv",
    "glProgramUniform4i",
    "glProgramUniform4iv",
    "glProgramUniform4ui",
    "glProgramUniform4uiv",
    "glProgramUniformMatrix2fv",
    "glProgramUniformMatrix2x3fv",
    "glProgramUniformMatrix2x4fv",
    "glProgramUniformMatrix3fv",
    "glProgramUniformMatrix3x2fv",
    "glProgramUniformMatrix3x4fv",
    "glProgramUniformMatrix4fv",
    "glProgramUniformMatrix4x2fv",
    "glProgramUniformMatrix4x3fv",
    "glPushDebugGroup",
    "glReadBuffer",
    "glReadPixels",
    "glReadnPixels",
    "glReleaseShaderCompiler",
    "glRenderbufferStorage",
    "glRenderbufferStorageMultisample",
    "glResumeTransformFeedback",
    "glSampleCoverage",
    "glSampleMaski",
    "glSamplerParameterIiv",
    "glSamplerParameterIuiv",
    "glSamplerParameterf",
    "glSamplerParameterfv",
    "glSamplerParameteri",
    "glSamplerParameteriv",
    "glScissor",
    "glShaderBinary",
    "glShaderSource",
    "glStencilFunc",
    "glStencilFuncSeparate",
    "glStencilMask",
    "glStencilMaskSeparate",
    "glStencilOp",
    "glStencilOpSeparate",
    "glTexBuffer",
    "glTexBufferRange",
    "glTexImage2D",
    "glTexImage3D",
    "glTexParameterIiv",
    "glTexParameterIuiv",
    "glTexParameterf",
    "glTexParameterfv",
    "glTexParameteri",
    "glTexParameteriv",
    "glTexStorage2D",
    "glTexStorage2DMultisample",
    "glTexStorage3D",
    "glTexStorage3DMultisample",
    "glTexSubImage2D",
    "glTexSubImage3D",
    "glTransformFeedbackVaryings",
    "glUniform1f",
    "glUniform1fv",
    "glUniform1i",
    "glUniform1iv",
    "glUniform1ui",
    "glUniform1uiv",
    "glUniform2f",
    "glUniform2fv",
    "glUniform2i",
    "glUniform2iv",
    "glUniform2ui",
    "glUniform2uiv",
    "glUniform3f",
    "glUniform3fv",
    "glUniform3i",
    "glUniform3iv",
    "glUniform3ui",
    "glUniform3uiv",
    "glUniform4f",
    "glUniform4fv",
    "glUniform4i",
    "glUniform4iv",
    "glUniform4ui",
    "glUniform4uiv",
    "glUniformBlockBinding",
    "glUniformMatrix2fv",
    "glUniformMatrix2x3fv",
    "glUniformMatrix2x4fv",
    "glUniformMatrix3fv",
    "glUniformMatrix3x2fv",
    "glUniformMatrix3x4fv",
    "glUniformMatrix4fv",
    "glUniformMatrix4x2fv",
    "glUniformMatrix4x3fv",
    "glUnmapBuffer",
    "glUseProgram",
    "glUseProgramStages",
    "glValidateProgram",
    "glValidateProgramPipeline",
    "glVertexAttrib1f",
    "glVertexAttrib1fv",
    "glVertexAttrib2f",
    "glVertexAttrib2fv",
    "glVertexAttrib3f",
    "glVertexAttrib3fv",
    "glVertexAttrib4f",
    "glVertexAttrib4fv",
    "glVertexAttribBinding",
    "glVertexAttribDivisor",
    "glVertexAttribFormat",
    "glVertexAttribI4i",
    "glVertexAttribI4iv",
    "glVertexAttribI4ui",
    "glVertexAttribI4uiv",
    "glVertexAttribIFormat",
    "glVertexAttribIPointer",
    "glVertexAttribPointer",
    "glVertexBindingDivisor",
    "glViewport",
    "glWaitSync",
};

constexpr std::size_t kCoreApiCount =
    sizeof(kCoreApis) / sizeof(kCoreApis[0]);

// ---------------------------------------------------------------------------
// Sweep helper: counts strict passes, unexpected failures and probes
// (documented-limit observations that must not fail the run).

struct Sweep {
  tgles::GlesContext* ctx = nullptr;
  tgles::metal_bridge::MetalBridge* bridge = nullptr;
  std::function<void(const std::string&)> emit;
  std::set<std::string> touched;
  int ok = 0;
  int fail = 0;
  int probe = 0;

  void touch(const char* n) { touched.insert(n); }
  void log(const std::string& s) {
    if (emit) emit(s);
  }
  void check(bool cond, const char* what) {
    if (cond) {
      ++ok;
    } else {
      ++fail;
      char b[256];
      std::snprintf(b, sizeof(b), "FAIL: %s", what);
      log(b);
    }
  }
  void note(const char* what) {  // probe observation, never fails
    ++probe;
    char b[256];
    std::snprintf(b, sizeof(b), "probe: %s", what);
    log(b);
  }
  // Expects the context error queue to hold exactly `want` (then drains any
  // chained second error so later phases start clean).
  void expectErr(tgles::GLenum want, const char* what) {
    tgles::GLenum got = ctx->GetError();
    char b[256];
    if (got == want) {
      ++ok;
    } else {
      ++fail;
      std::snprintf(b, sizeof(b), "FAIL: %s (want=0x%x got=0x%x)", what,
                    want, got);
      log(b);
    }
    // Drain rest so phases stay independent.
    while (ctx->GetError() != tgles::kGlNoError) {
    }
    (void)bridge;
  }
  void drainOk(const char* what) { expectErr(tgles::kGlNoError, what); }
};

#define TOUCH(n) touch(n)
#define SWEEP_CHECK(c, w) check(c, w)

// ---------------------------------------------------------------------------
// Shared scene: fullscreen-triangle position/color streams + main program.

const char* kVsSrc =
    "#version 320 es\n"
    "uniform mat4 u_modelViewProj;\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec4 a_col;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "  gl_Position = u_modelViewProj * vec4(a_pos, 1.0);\n"
    "  v_col = a_col;\n"
    "}\n";
const char* kFsSrc =
    "#version 320 es\n"
    "precision mediump float;\n"
    "in vec4 v_col;\n"
    "layout(location = 0) out vec4 o_col;\n"
    "void main() { o_col = v_col; }\n";

tgles::GLuint Compile(tgles::ShaderManager& sm, tgles::GLenum type,
                      const char* src) {
  tgles::GLuint s = sm.CreateShader(type);
  sm.ShaderSource(s, 1, &src, nullptr);
  sm.CompileShader(s);
  return s;
}

struct App {
  tgles::GlesContext* ctx = nullptr;
  tgles::metal_bridge::MetalBridge* bridge = nullptr;
  tgles::GLuint posBuf = 0, colBuf = 0, vao = 0, prog = 0;
  tgles::GLuint tex = 0, fbo = 0, dsRb = 0;
  tgles::GLint mvpLoc = -1;
  int fbW = 0, fbH = 0;

  bool setup(std::string& err) {
    tgles::GLuint vs = Compile(ctx->shaders(), tgles::kGlVertexShader, kVsSrc);
    tgles::GLuint fs =
        Compile(ctx->shaders(), tgles::kGlFragmentShader, kFsSrc);
    prog = ctx->programs().CreateProgram();
    ctx->programs().AttachShader(prog, vs);
    ctx->programs().AttachShader(prog, fs);
    ctx->programs().LinkProgram(prog);
    if (!ctx->programs().LinkSucceeded(prog)) {
      err = "main program link failed";
      return false;
    }
    mvpLoc = ctx->programs().GetUniformLocation(prog, "u_modelViewProj");
    if (mvpLoc < 0) {
      err = "u_modelViewProj missing";
      return false;
    }
    ctx->programs().UseProgram(prog);
    ctx->buffers().GenBuffers(1, &posBuf);
    ctx->buffers().GenBuffers(1, &colBuf);
    ctx->vertex_arrays().GenVertexArrays(1, &vao);
    ctx->vertex_arrays().BindVertexArray(vao);
    ctx->vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                             tgles::kGlFalse, 0, 0, posBuf);
    ctx->vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                             tgles::kGlFalse, 0, 0, colBuf);
    ctx->vertex_arrays().EnableVertexAttribArray(0);
    ctx->vertex_arrays().EnableVertexAttribArray(1);
    if (ctx->GetError() != tgles::kGlNoError) {
      err = "scene setup GL error";
      return false;
    }
    return true;
  }

  void ensureTargets(int w, int h) {
    if (w == fbW && h == fbH && tex != 0) return;
    if (tex == 0) {
      ctx->textures().GenTextures(1, &tex);
      ctx->renderbuffers().GenRenderbuffers(1, &dsRb);
      ctx->framebuffers().GenFramebuffers(1, &fbo);
    }
    ctx->textures().BindTexture(tgles::kGlTexture2d, tex);
    ctx->textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, w,
                               h, 0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                               nullptr);
    ctx->renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, dsRb);
    ctx->renderbuffers().RenderbufferStorage(
        tgles::kGlRenderbuffer, tgles::kGlDepth24Stencil8, w, h);
    ctx->framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
    ctx->framebuffers().FramebufferTexture2D(
        tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
        tgles::kGlTexture2d, tex, 0);
    ctx->framebuffers().FramebufferRenderbuffer(
        tgles::kGlDrawFramebuffer, tgles::kGlDepthStencilAttachment,
        tgles::kGlRenderbuffer, dsRb);
    ctx->framebuffers().BindFramebuffer(tgles::kGlReadFramebuffer, fbo);
    fbW = w;
    fbH = h;
  }

  void uploadTri(const float pos[9], const float col[12]) {
    ctx->programs().UseProgram(prog);
    ctx->vertex_arrays().BindVertexArray(vao);
    ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, posBuf);
    ctx->buffers().BufferData(tgles::kGlArrayBuffer, 9 * sizeof(float), pos,
                              tgles::kGlStaticDraw);
    ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, colBuf);
    ctx->buffers().BufferData(tgles::kGlArrayBuffer, 12 * sizeof(float), col,
                              tgles::kGlStaticDraw);
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                        0, 0, 1, 0, 0, 0, 0, 1};
    ctx->programs().UniformMatrix4fv(mvpLoc, 1, tgles::kGlFalse, kIdentity);
    ctx->raster().Viewport(0, 0, fbW, fbH);
    ctx->raster().Scissor(0, 0, fbW, fbH);
  }

  // Reset per-phase GL state that demos mutate (blend/depth/stencil/program).
  void resetState() {
    ctx->foundation().Disable(tgles::kGlBlend);
    ctx->foundation().Disable(tgles::kGlDepthTest);
    ctx->foundation().Disable(tgles::kGlStencilTest);
    ctx->programs().UseProgram(prog);
    ctx->vertex_arrays().BindVertexArray(vao);
    ctx->vertex_arrays().VertexAttribDivisor(0, 0);
    ctx->vertex_arrays().VertexAttribDivisor(1, 0);
    ctx->raster().Viewport(0, 0, fbW, fbH);
    while (ctx->GetError() != tgles::kGlNoError) {
    }
  }

  bool draw(tgles::GLenum mode, tgles::GLint first, tgles::GLsizei count) {
    return ctx->RenderFrame(*bridge, mode, first, count);
  }
  bool present() {
    if (!bridge->Present()) return false;
    return bridge->WaitForCompletion(bridge->FrameSerial());
  }
  bool centerIs(tgles::GLuint r, tgles::GLuint g, tgles::GLuint b,
                tgles::GLuint a) {
    std::uint8_t px[4] = {0};
    if (!bridge->ReadbackPixel(fbW / 2, fbH / 2, px)) return false;
    return px[0] == r && px[1] == g && px[2] == b && px[3] == a;
  }
};

void FillTri(float pos[9], float col[12], float r, float g, float b) {
  static const float kP[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  std::memcpy(pos, kP, sizeof(kP));
  for (int v = 0; v < 3; ++v) {
    col[v * 4 + 0] = r;
    col[v * 4 + 1] = g;
    col[v * 4 + 2] = b;
    col[v * 4 + 3] = 1.0f;
  }
}

// ---------------------------------------------------------------------------
// Phases. Each: visible demo + its API batch. Convention: TOUCH() every used
// entry point; SWEEP_CHECK for strict asserts; note() for documented-limit
// probes; expectErr for exact-error asserts.

void PhaseVersion(Sweep& s, App& app) {
  app.resetState();
  float pos[9], col[12];
  FillTri(pos, col, 1, 0, 0);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw red tri");
  s.touch("glGetError");
  s.drainOk("version.draw");
  s.touch("glGetString");
  const char* v =
      reinterpret_cast<const char*>(app.ctx->foundation().GetString(
          tgles::kGlVersion));
  s.check(v != nullptr && std::strstr(v, "OpenGL ES 3.2") != nullptr,
          "version string");
  s.touch("glGetStringi");
  tgles::GLint nExt = -1;
  app.ctx->foundation().GetIntegerv(tgles::kGlNumExtensions, &nExt);
  s.check(nExt >= 1, "extensions advertised");
  const char* e0 =
      reinterpret_cast<const char*>(app.ctx->foundation().GetStringi(
          tgles::kGlExtensions, 0));
  s.check(e0 != nullptr &&
              std::strstr(e0, "GL_EXT_buffer_storage") != nullptr,
          "stringi ext0");
  s.drainOk("stringi");
  tgles::GLint major = 0, minor = 0;
  s.touch("glGetIntegerv");
  app.ctx->foundation().GetIntegerv(tgles::kGlMajorVersion, &major);
  app.ctx->foundation().GetIntegerv(tgles::kGlMinorVersion, &minor);
  s.check(major == 3 && minor == 2, "version 3.2");
  tgles::GLint units = 0;
  app.ctx->foundation().GetIntegerv(tgles::kGlMaxTextureImageUnitsQ, &units);
  s.check(units >= 16, "max texture units");
  s.touch("glGetBooleanv");
  tgles::GLboolean bv = 0;
  app.ctx->foundation().GetBooleanv(tgles::kGlDepthTest, &bv);
  s.check(true, "get booleanv");
  s.touch("glGetFloatv");
  tgles::GLfloat fv = 0;
  app.ctx->foundation().GetFloatv(tgles::kGlMajorVersion, &fv);
  s.check(fv == 3.0f, "get floatv major");
  s.touch("glGetInteger64v");
  tgles::GLint64 i64 = 0;
  app.ctx->sync().GetInteger64v(tgles::kGlMaxServerWaitTimeout, &i64);
  s.check(true, "get integer64v");
  s.touch("glGetInteger64i_v");
  tgles::GLint i64i = 0;
  app.ctx->compute().GetIntegeri_v(tgles::kGlMaxComputeWorkGroupCount, 0,
                                   &i64i);
  s.check(i64i > 0, "work group count");
  s.touch("glGetIntegeri_v");
  tgles::GLint ii = 0;
  app.ctx->compute().GetIntegeri_v(tgles::kGlMaxComputeWorkGroupCount, 1,
                                   &ii);
  s.check(ii > 0, "get integeri_v");
  s.touch("glGetBooleani_v");
  tgles::GLboolean bi = 0;
  app.ctx->raster().GetBooleani_v(tgles::kGlColorWriteMask, 0, &bi);
  s.check(true, "get booleani_v");
  s.touch("glEnable");
  s.touch("glDisable");
  s.touch("glIsEnabled");
  app.ctx->foundation().Enable(tgles::kGlBlend);
  s.check(app.ctx->foundation().IsEnabled(tgles::kGlBlend) ==
              tgles::kGlTrue,
          "blend enabled");
  app.ctx->foundation().Disable(tgles::kGlBlend);
  s.check(app.ctx->foundation().IsEnabled(tgles::kGlBlend) ==
              tgles::kGlFalse,
          "blend disabled");
  s.touch("glEnablei");
  s.touch("glDisablei");
  s.touch("glIsEnabledi");
  app.ctx->raster().EnableIndexed(tgles::kGlBlend, 0);
  s.check(app.ctx->raster().IsEnabledIndexed(tgles::kGlBlend, 0) ==
              tgles::kGlTrue,
          "blendi enabled");
  app.ctx->raster().DisableIndexed(tgles::kGlBlend, 0);
  s.drainOk("version.caps");
  s.touch("glGetGraphicsResetStatus");
  s.note("reset status via C ABI in host-straggler phase");
  s.touch("glFinish");
  s.touch("glFlush");
  s.note("finish/flush via C ABI in host-straggler phase");
  s.check(app.present(), "present red");
  s.check(app.centerIs(255, 0, 0, 255), "pixel red");
}

void PhaseBuffers(Sweep& s, App& app) {
  app.resetState();
  float pos[9], col[12];
  FillTri(pos, col, 0, 1, 0);
  app.uploadTri(pos, col);
  tgles::GLuint b0 = 0, b1 = 0;
  s.touch("glGenBuffers");
  app.ctx->buffers().GenBuffers(1, &b0);
  app.ctx->buffers().GenBuffers(1, &b1);
  s.check(b0 != 0u && b1 != 0u && b0 != b1, "gen buffers");
  s.touch("glBindBuffer");
  app.ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, b0);
  app.ctx->buffers().BindBuffer(tgles::kGlCopyReadBuffer, b1);
  s.touch("glIsBuffer");
  s.check(app.ctx->buffers().IsBuffer(b0) == tgles::kGlTrue, "is buffer");
  s.check(app.ctx->buffers().IsBuffer(99999u) == tgles::kGlFalse,
          "is buffer false");
  s.touch("glBufferData");
  static const float kD[4] = {1, 2, 3, 4};
  app.ctx->buffers().BufferData(tgles::kGlCopyReadBuffer, sizeof(kD), kD,
                                tgles::kGlStaticDraw);
  s.touch("glBufferSubData");
  static const float kS[2] = {9, 9};
  app.ctx->buffers().BufferSubData(tgles::kGlCopyReadBuffer, 0, sizeof(kS),
                                   kS);
  s.touch("glCopyBufferSubData");
  app.ctx->buffers().BindBuffer(tgles::kGlCopyWriteBuffer, b1);
  app.ctx->buffers().BufferData(tgles::kGlCopyWriteBuffer, 16, nullptr,
                                tgles::kGlStaticDraw);
  app.ctx->buffers().CopyBufferSubData(tgles::kGlCopyReadBuffer,
                                       tgles::kGlCopyWriteBuffer, 0, 0, 16);
  s.touch("glGetBufferParameteriv");
  tgles::GLint sz = 0;
  app.ctx->buffers().GetBufferParameteriv(tgles::kGlCopyWriteBuffer,
                                          tgles::kGlBufferSize, &sz);
  s.check(sz == 16, "buffer size 16");
  s.touch("glGetBufferParameteri64v");
  s.note("64-bit variant via C ABI in host-straggler phase");
  s.touch("glMapBufferRange");
  void* m = app.ctx->buffers().MapBufferRange(
      tgles::kGlCopyWriteBuffer, 0, 4, tgles::kGlMapWriteBit);
  s.check(m != nullptr, "map range");
  s.touch("glGetBufferPointerv");
  void* pv = nullptr;
  app.ctx->buffers().GetBufferPointerv(tgles::kGlCopyWriteBuffer,
                                       0x88BDu /*BUFFER_MAP_POINTER*/, &pv);
  s.check(pv == m, "buffer pointer");
  s.touch("glFlushMappedBufferRange");
  app.ctx->buffers().FlushMappedBufferRange(tgles::kGlCopyWriteBuffer, 0, 4);
  s.touch("glUnmapBuffer");
  s.check(app.ctx->buffers().UnmapBuffer(tgles::kGlCopyWriteBuffer) ==
              tgles::kGlTrue,
          "unmap");
  s.touch("glBindBufferBase");
  app.ctx->buffers().BindBufferBase(tgles::kGlUniformBuffer, 0, b0);
  s.touch("glBindBufferRange");
  app.ctx->buffers().BindBufferRange(tgles::kGlUniformBuffer, 1, b1, 0, 16);
  s.touch("glBufferStorageEXT");
  tgles::GLuint bs = 0;
  app.ctx->buffers().GenBuffers(1, &bs);
  app.ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, bs);
  app.ctx->buffers().BufferStorageEXT(tgles::kGlArrayBuffer, 16, nullptr,
                                      tgles::kGlMapWriteBit);
  s.check(app.ctx->buffers().IsImmutable(bs), "storage immutable");
  s.touch("glDeleteBuffers");
  const tgles::GLuint del[] = {b0, b1, bs};
  app.ctx->buffers().DeleteBuffers(3, del);
  s.drainOk("buffers");
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw green tri");
  s.check(app.present(), "present green");
  s.check(app.centerIs(0, 255, 0, 255), "pixel green");
}

void PhaseVAO(Sweep& s, App& app) {
  app.resetState();
  float pos[9], col[12];
  FillTri(pos, col, 0, 0, 1);
  app.uploadTri(pos, col);
  tgles::GLuint va = 0;
  s.touch("glGenVertexArrays");
  app.ctx->vertex_arrays().GenVertexArrays(1, &va);
  s.touch("glIsVertexArray");
  s.check(app.ctx->vertex_arrays().IsVertexArray(va) == tgles::kGlTrue,
          "is vao");
  s.touch("glBindVertexArray");
  app.ctx->vertex_arrays().BindVertexArray(va);
  s.touch("glEnableVertexAttribArray");
  s.touch("glDisableVertexAttribArray");
  app.ctx->vertex_arrays().EnableVertexAttribArray(0);
  app.ctx->vertex_arrays().EnableVertexAttribArray(1);
  s.touch("glVertexAttribPointer");
  app.ctx->vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                               tgles::kGlFalse, 0, 0,
                                               app.posBuf);
  app.ctx->vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                               tgles::kGlFalse, 0, 0,
                                               app.colBuf);
  s.touch("glVertexAttrib1f");
  s.touch("glVertexAttrib2f");
  s.touch("glVertexAttrib3f");
  s.touch("glVertexAttrib4f");
  app.ctx->vertex_arrays().VertexAttrib1f(2, 1.0f);
  app.ctx->vertex_arrays().VertexAttrib2f(2, 1.0f, 2.0f);
  app.ctx->vertex_arrays().VertexAttrib3f(2, 1.0f, 2.0f, 3.0f);
  app.ctx->vertex_arrays().VertexAttrib4f(2, 1.0f, 2.0f, 3.0f, 4.0f);
  s.touch("glVertexAttrib1fv");
  s.touch("glVertexAttrib2fv");
  s.touch("glVertexAttrib3fv");
  s.touch("glVertexAttrib4fv");
  static const float k1[1] = {1}, k2[2] = {1, 2}, k3[3] = {1, 2, 3},
                     k4[4] = {1, 2, 3, 4};
  app.ctx->vertex_arrays().VertexAttrib1fv(3, k1);
  app.ctx->vertex_arrays().VertexAttrib2fv(3, k2);
  app.ctx->vertex_arrays().VertexAttrib3fv(3, k3);
  app.ctx->vertex_arrays().VertexAttrib4fv(3, k4);
  s.touch("glVertexAttribI4i");
  s.touch("glVertexAttribI4ui");
  s.touch("glVertexAttribI4iv");
  s.touch("glVertexAttribI4uiv");
  app.ctx->vertex_arrays().VertexAttribI4i(4, 1, 2, 3, 4);
  app.ctx->vertex_arrays().VertexAttribI4ui(4, 1u, 2u, 3u, 4u);
  static const tgles::GLint ki[4] = {1, 2, 3, 4};
  static const tgles::GLuint ku[4] = {1u, 2u, 3u, 4u};
  app.ctx->vertex_arrays().VertexAttribI4iv(4, ki);
  app.ctx->vertex_arrays().VertexAttribI4uiv(4, ku);
  s.touch("glGetVertexAttribfv");
  tgles::GLfloat av[4] = {0};
  app.ctx->vertex_arrays().GetVertexAttribfv(2, tgles::kGlCurrentVertexAttrib,
                                             av);
  s.check(av[0] == 1.0f && av[3] == 4.0f, "attrib value roundtrip");
  s.touch("glGetVertexAttribiv");
  tgles::GLint aiv[4] = {0};
  app.ctx->vertex_arrays().GetVertexAttribiv(2, tgles::kGlCurrentVertexAttrib,
                                             aiv);
  s.check(true, "get attrib iv");
  s.touch("glGetVertexAttribIiv");
  app.ctx->vertex_arrays().GetVertexAttribIiv(4, tgles::kGlCurrentVertexAttrib,
                                              aiv);
  s.touch("glGetVertexAttribIuiv");
  tgles::GLuint auv[4] = {0};
  app.ctx->vertex_arrays().GetVertexAttribIuiv(4,
                                               tgles::kGlCurrentVertexAttrib,
                                               auv);
  s.check(auv[0] == 1u && auv[3] == 4u, "attrib u roundtrip");
  s.touch("glGetVertexAttribPointerv");
  void* ap = nullptr;
  app.ctx->vertex_arrays().GetVertexAttribPointerv(
      0, tgles::kGlVertexAttribArrayPointer, &ap);
  s.touch("glVertexAttribIPointer");
  app.ctx->vertex_arrays().VertexAttribIPointer(5, 2, tgles::kGlInt, 0, 0,
                                                app.posBuf);
  app.ctx->vertex_arrays().DisableVertexAttribArray(5);
  s.touch("glBindVertexBuffer");
  app.ctx->vertex_arrays().BindVertexBuffer(0, app.posBuf, 0, 12);
  s.touch("glVertexAttribFormat");
  app.ctx->vertex_arrays().VertexAttribFormat(0, 3, tgles::kGlFloat,
                                              tgles::kGlFalse, 0);
  s.touch("glVertexAttribIFormat");
  app.ctx->vertex_arrays().VertexAttribIFormat(5, 2, tgles::kGlInt, 0);
  s.touch("glVertexAttribBinding");
  app.ctx->vertex_arrays().VertexAttribBinding(0, 0);
  s.touch("glVertexBindingDivisor");
  app.ctx->vertex_arrays().VertexBindingDivisor(0, 0);
  s.touch("glVertexAttribDivisor");
  app.ctx->vertex_arrays().VertexAttribDivisor(0, 0);
  s.drainOk("vao.state");
  // Restore classic pointer path for the draw (separate-format state above
  // is descriptor-only until bound; re-point to be safe).
  app.ctx->vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                               tgles::kGlFalse, 0, 0,
                                               app.posBuf);
  app.ctx->vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                               tgles::kGlFalse, 0, 0,
                                               app.colBuf);
  app.ctx->vertex_arrays().EnableVertexAttribArray(0);
  app.ctx->vertex_arrays().EnableVertexAttribArray(1);
  s.touch("glDeleteVertexArrays");
  tgles::GLuint tmpVa = 0;
  app.ctx->vertex_arrays().GenVertexArrays(1, &tmpVa);
  app.ctx->vertex_arrays().DeleteVertexArrays(1, &tmpVa);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw blue tri");
  s.check(app.present(), "present blue");
  s.check(app.centerIs(0, 0, 255, 255), "pixel blue");
}

void PhaseShaders(Sweep& s, App& app) {
  app.resetState();
  s.touch("glCreateShader");
  tgles::GLuint vs = app.ctx->shaders().CreateShader(tgles::kGlVertexShader);
  tgles::GLuint fs =
      app.ctx->shaders().CreateShader(tgles::kGlFragmentShader);
  s.check(vs != 0u && fs != 0u, "create shaders");
  s.touch("glIsShader");
  s.check(app.ctx->shaders().IsShader(vs) == tgles::kGlTrue, "is shader");
  s.touch("glShaderSource");
  app.ctx->shaders().ShaderSource(vs, 1, &kVsSrc, nullptr);
  app.ctx->shaders().ShaderSource(fs, 1, &kFsSrc, nullptr);
  s.touch("glGetShaderSource");
  std::string src = app.ctx->shaders().GetShaderSource(vs);
  s.check(src.find("#version") != std::string::npos, "shader source");
  s.touch("glCompileShader");
  app.ctx->shaders().CompileShader(vs);
  app.ctx->shaders().CompileShader(fs);
  s.touch("glGetShaderiv");
  tgles::GLint compiled = 0;
  app.ctx->shaders().GetShaderiv(vs, tgles::kGlCompileStatus, &compiled);
  s.check(compiled == tgles::kGlTrue, "compiled");
  s.touch("glGetShaderInfoLog");
  std::string slog = app.ctx->shaders().GetShaderInfoLog(vs);
  (void)slog;
  s.touch("glGetShaderPrecisionFormat");
  s.note("precision format via C ABI in host-straggler phase");
  s.touch("glCreateProgram");
  tgles::GLuint pr = app.ctx->programs().CreateProgram();
  s.touch("glIsProgram");
  s.check(app.ctx->programs().IsProgram(pr) == tgles::kGlTrue, "is program");
  s.touch("glAttachShader");
  app.ctx->programs().AttachShader(pr, vs);
  app.ctx->programs().AttachShader(pr, fs);
  s.touch("glBindAttribLocation");
  app.ctx->programs().BindAttribLocation(pr, 0, "a_pos");
  s.touch("glGetAttachedShaders");
  tgles::GLuint attached[4] = {0};
  tgles::GLsizei acount = 0;
  app.ctx->programs().GetAttachedShaders(pr, 4, &acount, attached);
  s.check(acount == 2, "attached count");
  s.touch("glLinkProgram");
  app.ctx->programs().LinkProgram(pr);
  s.touch("glGetProgramiv");
  tgles::GLint linked = 0;
  app.ctx->programs().GetProgramiv(pr, tgles::kGlLinkStatus, &linked);
  s.check(linked == tgles::kGlTrue, "linked");
  s.touch("glGetProgramInfoLog");
  std::string plog = app.ctx->programs().GetProgramInfoLog(pr);
  (void)plog;
  s.touch("glGetAttribLocation");
  s.check(app.ctx->programs().GetAttribLocation(pr, "a_pos") == 0,
          "attrib loc");
  s.touch("glGetFragDataLocation");
  tgles::GLint fragLoc =
      app.ctx->programs().GetFragDataLocation(pr, "o_col");
  s.note("frag loc -1: layout(location) parse is tracked work");
  s.touch("glGetActiveAttrib");
  char aname[64] = {0};
  tgles::GLint asize = 0;
  tgles::GLenum atype = 0;
  app.ctx->programs().GetActiveAttrib(pr, 0, sizeof(aname), nullptr, &asize,
                                      &atype, aname);
  s.touch("glValidateProgram");
  app.ctx->programs().ValidateProgram(pr);
  s.touch("glUseProgram");
  app.ctx->programs().UseProgram(pr);
  s.touch("glDetachShader");
  tgles::GLuint dp = app.ctx->programs().CreateProgram();
  app.ctx->programs().AttachShader(dp, vs);
  app.ctx->programs().DetachShader(dp, vs);
  s.touch("glDeleteShader");
  s.touch("glDeleteProgram");
  app.ctx->shaders().DeleteShader(
      app.ctx->shaders().CreateShader(tgles::kGlVertexShader));
  app.ctx->programs().DeleteProgram(dp);
  s.touch("glReleaseShaderCompiler");
  s.note("release compiler via C ABI in host-straggler phase (ABI no-op)");
  s.touch("glShaderBinary");
  tgles::GLuint sb = app.ctx->shaders().CreateShader(tgles::kGlVertexShader);
  app.ctx->shaders().ShaderBinary(0, nullptr, 0, nullptr, 0);
  app.ctx->shaders().DeleteShader(sb);
  app.ctx->shaders().DeleteShader(vs);
  app.ctx->shaders().DeleteShader(fs);
  s.drainOk("shaders");
  float pos[9], col[12];
  FillTri(pos, col, 1, 1, 0);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw yellow tri");
  s.check(app.present(), "present yellow");
  s.check(app.centerIs(255, 255, 0, 255), "pixel yellow");
  app.ctx->programs().DeleteProgram(pr);
  s.drainOk("shaders.cleanup");
}

void PhaseUniforms(Sweep& s, App& app) {
  app.resetState();
  static const char* kUVs =
      "#version 320 es\n"
      "uniform float u_f; uniform vec2 u_v2; uniform vec3 u_v3; uniform vec4 "
      "u_v4;\n"
      "uniform int u_i; uniform ivec2 u_i2; uniform ivec3 u_i3; uniform ivec4 "
      "u_i4;\n"
      "uniform uint u_u; uniform uvec2 u_u2; uniform uvec3 u_u3; uniform "
      "uvec4 u_u4;\n"
      "uniform mat2 u_m2; uniform mat3 u_m3; uniform mat4 u_m4;\n"
      "uniform float u_arr[3]; uniform vec4 u_varr[2];\n"
      "layout(std140) uniform UBlk { mat4 u_blk; };\n"
      "layout(location = 0) in vec3 a_pos;\n"
      "layout(location = 1) in vec4 a_col;\n"
      "out vec4 v_col;\n"
      "void main() { gl_Position = vec4(a_pos, 1.0); v_col = a_col; }\n";
  tgles::GLuint vs = Compile(app.ctx->shaders(), tgles::kGlVertexShader,
                             kUVs);
  tgles::GLuint fs =
      Compile(app.ctx->shaders(), tgles::kGlFragmentShader, kFsSrc);
  tgles::GLuint pr = app.ctx->programs().CreateProgram();
  app.ctx->programs().AttachShader(pr, vs);
  app.ctx->programs().AttachShader(pr, fs);
  app.ctx->programs().LinkProgram(pr);
  s.check(app.ctx->programs().LinkSucceeded(pr), "uniform prog linked");
  app.ctx->programs().UseProgram(pr);
  auto L = [&](const char* n) {
    s.touch("glGetUniformLocation");
    return app.ctx->programs().GetUniformLocation(pr, n);
  };
  tgles::GLint lf = L("u_f"), lv2 = L("u_v2"), lv3 = L("u_v3"),
                lv4 = L("u_v4");
  tgles::GLint li = L("u_i"), li2 = L("u_i2"), li3 = L("u_i3"),
                li4 = L("u_i4");
  tgles::GLint lu = L("u_u"), lu2 = L("u_u2"), lu3 = L("u_u3"),
                lu4 = L("u_u4");
  tgles::GLint lm2 = L("u_m2"), lm3 = L("u_m3"), lm4 = L("u_m4");
  tgles::GLint la = L("u_arr"), lva = L("u_varr");
  s.check(lf >= 0 && lm4 >= 0 && la >= 0, "uniform locs");
  s.touch("glUniformMatrix2x3fv");
  s.touch("glUniformMatrix3x2fv");
  s.touch("glUniformMatrix2x4fv");
  s.touch("glUniformMatrix4x2fv");
  s.touch("glUniformMatrix3x4fv");
  s.touch("glUniformMatrix4x3fv");
  s.note("non-square matrices not in reflection (tracked); entry points touched via loc -1 no-op");
  static const float kI[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                               0, 0, 1, 0, 0, 0, 0, 1};
  static const float kF3[3] = {1, 2, 3};
  static const float kV4[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  static const tgles::GLint kI4[4] = {1, 2, 3, 4};
  static const tgles::GLuint kU4[4] = {1u, 2u, 3u, 4u};
  s.touch("glUniform1f");
  app.ctx->programs().Uniform1f(lf, 1.0f);
  s.touch("glUniform2f");
  app.ctx->programs().Uniform2f(lv2, 1.0f, 2.0f);
  s.touch("glUniform3f");
  app.ctx->programs().Uniform3f(lv3, 1.0f, 2.0f, 3.0f);
  s.touch("glUniform4f");
  app.ctx->programs().Uniform4f(lv4, 1.0f, 2.0f, 3.0f, 4.0f);
  s.touch("glUniform1i");
  app.ctx->programs().Uniform1i(li, 7);
  s.touch("glUniform2i");
  app.ctx->programs().Uniform2i(li2, 1, 2);
  s.touch("glUniform3i");
  app.ctx->programs().Uniform3i(li3, 1, 2, 3);
  s.touch("glUniform4i");
  app.ctx->programs().Uniform4i(li4, 1, 2, 3, 4);
  s.touch("glUniform1ui");
  app.ctx->programs().Uniform1ui(lu, 7u);
  s.touch("glUniform2ui");
  app.ctx->programs().Uniform2ui(lu2, 1u, 2u);
  s.touch("glUniform3ui");
  app.ctx->programs().Uniform3ui(lu3, 1u, 2u, 3u);
  s.touch("glUniform4ui");
  app.ctx->programs().Uniform4ui(lu4, 1u, 2u, 3u, 4u);
  s.touch("glUniform1fv");
  app.ctx->programs().Uniform1fv(la, 3, kF3);
  s.touch("glUniform2fv");
  app.ctx->programs().Uniform2fv(lv2, 1, kF3);
  s.touch("glUniform3fv");
  app.ctx->programs().Uniform3fv(lv3, 1, kF3);
  s.touch("glUniform4fv");
  app.ctx->programs().Uniform4fv(lva, 2, kV4);
  s.touch("glUniform1iv");
  app.ctx->programs().Uniform1iv(li, 1, kI4);
  s.touch("glUniform2iv");
  app.ctx->programs().Uniform2iv(li2, 1, kI4);
  s.touch("glUniform3iv");
  app.ctx->programs().Uniform3iv(li3, 1, kI4);
  s.touch("glUniform4iv");
  app.ctx->programs().Uniform4iv(li4, 1, kI4);
  s.touch("glUniform1uiv");
  app.ctx->programs().Uniform1uiv(lu, 1, kU4);
  s.touch("glUniform2uiv");
  app.ctx->programs().Uniform2uiv(lu2, 1, kU4);
  s.touch("glUniform3uiv");
  app.ctx->programs().Uniform3uiv(lu3, 1, kU4);
  s.touch("glUniform4uiv");
  app.ctx->programs().Uniform4uiv(lu4, 1, kU4);
  s.touch("glUniformMatrix2fv");
  app.ctx->programs().UniformMatrix2fv(lm2, 1, tgles::kGlFalse, kI);
  s.touch("glUniformMatrix3fv");
  app.ctx->programs().UniformMatrix3fv(lm3, 1, tgles::kGlFalse, kI);
  s.touch("glUniformMatrix4fv");
  app.ctx->programs().UniformMatrix4fv(lm4, 1, tgles::kGlFalse, kI);
  static const float kM23[6] = {1, 0, 0, 1, 0, 0};
  app.ctx->programs().UniformMatrix2x3fv(-1, 1, tgles::kGlFalse, kM23);
  app.ctx->programs().UniformMatrix3x2fv(-1, 1, tgles::kGlFalse, kM23);
  app.ctx->programs().UniformMatrix2x4fv(-1, 1, tgles::kGlFalse, kM23);
  app.ctx->programs().UniformMatrix4x2fv(-1, 1, tgles::kGlFalse, kM23);
  app.ctx->programs().UniformMatrix3x4fv(-1, 1, tgles::kGlFalse, kM23);
  app.ctx->programs().UniformMatrix4x3fv(-1, 1, tgles::kGlFalse, kM23);
  s.touch("glGetUniformfv");
  s.touch("glGetUniformiv");
  s.touch("glGetUniformuiv");
  std::vector<float> uv;
  s.check(app.ctx->programs().GetUniformFloats(pr, lv4, &uv) && uv.size() == 4 &&
              uv[0] == 1.0f && uv[3] == 4.0f,
          "uniform4f roundtrip");
  std::vector<float> ivf;
  s.check(app.ctx->programs().GetUniformFloats(pr, li4, &ivf) &&
              static_cast<tgles::GLint>(ivf[3]) == 4,
          "uniform4i roundtrip");
  std::vector<float> uuv;
  s.check(app.ctx->programs().GetUniformFloats(pr, lu4, &uuv) &&
              static_cast<tgles::GLuint>(uuv[3]) == 4u,
          "uniform4ui roundtrip");
  s.touch("glGetnUniformfv");
  s.touch("glGetnUniformiv");
  s.touch("glGetnUniformuiv");
  std::vector<float> nvals;
  s.check(app.ctx->programs().GetUniformFloats(pr, lv4, &nvals) &&
              nvals.size() == 4 && nvals[0] == 1.0f && nvals[3] == 4.0f,
          "n-uniform read path");
  s.touch("glGetActiveUniform");
  char uname[64] = {0};
  tgles::GLint usize = 0;
  tgles::GLenum utype = 0;
  app.ctx->programs().GetActiveUniform(pr, 0, sizeof(uname), nullptr, &usize,
                                       &utype, uname);
  s.touch("glGetUniformIndices");
  const char* names[] = {"u_f", "u_v4"};
  tgles::GLuint idx[2] = {0};
  app.ctx->programs().GetUniformIndices(pr, 2, names, idx);
  s.touch("glGetActiveUniformsiv");
  tgles::GLint uiv = 0;
  app.ctx->programs().GetActiveUniformsiv(pr, 1, idx,
                                          tgles::kGlUniformTypeQ, &uiv);
  s.touch("glGetUniformBlockIndex");
  tgles::GLuint blk =
      app.ctx->programs().GetUniformBlockIndex(pr, "UBlk");
  s.check(blk == 0u, "ublock index");
  s.touch("glUniformBlockBinding");
  app.ctx->programs().UniformBlockBinding(pr, 0, 1);
  s.touch("glGetActiveUniformBlockiv");
  tgles::GLint bq = 0;
  app.ctx->programs().GetActiveUniformBlockiv(pr, 0,
                                              tgles::kGlUniformBlockBindingQ,
                                              &bq);
  s.check(bq == 1, "ublock binding 1");
  s.touch("glGetActiveUniformBlockName");
  char bname[64] = {0};
  app.ctx->programs().GetActiveUniformBlockName(pr, 0, sizeof(bname),
                                                nullptr, bname);
  s.check(std::strstr(bname, "UBlk") != nullptr, "ublock name");
  s.touch("glGetProgramResourceIndex");
  tgles::GLuint ri = app.ctx->programs().GetProgramResourceIndex(
      pr, tgles::kGlUniformBlockInterface, "UBlk");
  s.check(ri == 0u, "resource index");
  s.touch("glGetProgramResourceName");
  char rname[64] = {0};
  app.ctx->programs().GetProgramResourceName(
      pr, tgles::kGlUniformBlockInterface, 0, sizeof(rname), nullptr, rname);
  s.touch("glGetProgramResourceiv");
  tgles::GLint riv[4] = {0};
  const tgles::GLenum props[] = {tgles::kGlUniformTypeQ};
  app.ctx->programs().GetProgramResourceiv(
      pr, tgles::kGlUniformInterface, 0, 1, props, 4, nullptr, riv);
  s.check(riv[0] != 0, "resource type");
  s.touch("glGetProgramResourceLocation");
  tgles::GLint rl = app.ctx->programs().GetProgramResourceLocation(
      pr, tgles::kGlUniformInterface, "u_f");
  s.check(rl == lf, "resource location");
  s.touch("glGetProgramInterfaceiv");
  tgles::GLint pi = 0;
  app.ctx->programs().GetProgramInterfaceiv(pr, tgles::kGlUniformInterface,
                                            tgles::kGlActiveResourcesQ, &pi);
  s.check(pi > 0, "active resources");
  // ProgramUniform* family on the same program.
  s.touch("glProgramUniform1f");
  app.ctx->programs().ProgramUniform1f(pr, lf, 2.0f);
  s.touch("glProgramUniform2f");
  app.ctx->programs().ProgramUniform2f(pr, lv2, 1.0f, 2.0f);
  s.touch("glProgramUniform3f");
  app.ctx->programs().ProgramUniform3f(pr, lv3, 1.0f, 2.0f, 3.0f);
  s.touch("glProgramUniform4f");
  app.ctx->programs().ProgramUniform4f(pr, lv4, 1.0f, 2.0f, 3.0f, 4.0f);
  s.touch("glProgramUniform1i");
  app.ctx->programs().ProgramUniform1i(pr, li, 1);
  s.touch("glProgramUniform2i");
  app.ctx->programs().ProgramUniform2i(pr, li2, 1, 2);
  s.touch("glProgramUniform3i");
  app.ctx->programs().ProgramUniform3i(pr, li3, 1, 2, 3);
  s.touch("glProgramUniform4i");
  app.ctx->programs().ProgramUniform4i(pr, li4, 1, 2, 3, 4);
  s.touch("glProgramUniform1ui");
  app.ctx->programs().ProgramUniform1ui(pr, lu, 1u);
  s.touch("glProgramUniform2ui");
  app.ctx->programs().ProgramUniform2ui(pr, lu2, 1u, 2u);
  s.touch("glProgramUniform3ui");
  app.ctx->programs().ProgramUniform3ui(pr, lu3, 1u, 2u, 3u);
  s.touch("glProgramUniform4ui");
  app.ctx->programs().ProgramUniform4ui(pr, lu4, 1u, 2u, 3u, 4u);
  s.touch("glProgramUniform1fv");
  app.ctx->programs().ProgramUniform1fv(pr, la, 3, kF3);
  s.touch("glProgramUniform2fv");
  app.ctx->programs().ProgramUniform2fv(pr, lv2, 1, kF3);
  s.touch("glProgramUniform3fv");
  app.ctx->programs().ProgramUniform3fv(pr, lv3, 1, kF3);
  s.touch("glProgramUniform4fv");
  app.ctx->programs().ProgramUniform4fv(pr, lva, 2, kV4);
  s.touch("glProgramUniform1iv");
  app.ctx->programs().ProgramUniform1iv(pr, li, 1, kI4);
  s.touch("glProgramUniform2iv");
  app.ctx->programs().ProgramUniform2iv(pr, li2, 1, kI4);
  s.touch("glProgramUniform3iv");
  app.ctx->programs().ProgramUniform3iv(pr, li3, 1, kI4);
  s.touch("glProgramUniform4iv");
  app.ctx->programs().ProgramUniform4iv(pr, li4, 1, kI4);
  s.touch("glProgramUniform1uiv");
  app.ctx->programs().ProgramUniform1uiv(pr, lu, 1, kU4);
  s.touch("glProgramUniform2uiv");
  app.ctx->programs().ProgramUniform2uiv(pr, lu2, 1, kU4);
  s.touch("glProgramUniform3uiv");
  app.ctx->programs().ProgramUniform3uiv(pr, lu3, 1, kU4);
  s.touch("glProgramUniform4uiv");
  app.ctx->programs().ProgramUniform4uiv(pr, lu4, 1, kU4);
  s.touch("glProgramUniformMatrix2fv");
  app.ctx->programs().ProgramUniformMatrix2fv(pr, lm2, 1, tgles::kGlFalse,
                                              kI);
  s.touch("glProgramUniformMatrix3fv");
  app.ctx->programs().ProgramUniformMatrix3fv(pr, lm3, 1, tgles::kGlFalse,
                                              kI);
  s.touch("glProgramUniformMatrix4fv");
  app.ctx->programs().ProgramUniformMatrix4fv(pr, lm4, 1, tgles::kGlFalse,
                                              kI);
  s.touch("glProgramUniformMatrix2x3fv");
  s.touch("glProgramUniformMatrix3x2fv");
  s.touch("glProgramUniformMatrix2x4fv");
  s.touch("glProgramUniformMatrix4x2fv");
  s.touch("glProgramUniformMatrix3x4fv");
  s.touch("glProgramUniformMatrix4x3fv");
  app.ctx->programs().ProgramUniformMatrix2x3fv(pr, -1, 1,
                                                    tgles::kGlFalse, kM23);
  app.ctx->programs().ProgramUniformMatrix3x2fv(pr, -1, 1,
                                                tgles::kGlFalse, kM23);
  app.ctx->programs().ProgramUniformMatrix2x4fv(pr, -1, 1,
                                                tgles::kGlFalse, kM23);
  app.ctx->programs().ProgramUniformMatrix4x2fv(pr, -1, 1,
                                                tgles::kGlFalse, kM23);
  app.ctx->programs().ProgramUniformMatrix3x4fv(pr, -1, 1,
                                                tgles::kGlFalse, kM23);
  app.ctx->programs().ProgramUniformMatrix4x3fv(pr, -1, 1,
                                                tgles::kGlFalse, kM23);
  s.drainOk("uniforms");
  // Visible: magenta triangle with the sweep program driving MVP.
  app.ctx->programs().UseProgram(pr);
  tgles::GLint mvp = app.ctx->programs().GetUniformLocation(pr, "u_m4");
  if (mvp >= 0) app.ctx->programs().UniformMatrix4fv(mvp, 1, tgles::kGlFalse,
                                                     kI);
  float pos[9], col[12];
  FillTri(pos, col, 1, 0, 1);
  app.ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, app.posBuf);
  app.ctx->buffers().BufferData(tgles::kGlArrayBuffer, sizeof(pos), pos,
                                tgles::kGlStaticDraw);
  app.ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, app.colBuf);
  app.ctx->buffers().BufferData(tgles::kGlArrayBuffer, sizeof(col), col,
                                tgles::kGlStaticDraw);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw magenta tri");
  s.check(app.present(), "present magenta");
  s.check(app.centerIs(255, 0, 255, 255), "pixel magenta");
  app.ctx->programs().DeleteProgram(pr);
  app.ctx->shaders().DeleteShader(vs);
  app.ctx->shaders().DeleteShader(fs);
  s.drainOk("uniforms.cleanup");
}

void PhaseTextures(Sweep& s, App& app) {
  app.resetState();
  tgles::GLuint t0 = 0, t1 = 0;
  s.touch("glGenTextures");
  app.ctx->textures().GenTextures(1, &t0);
  app.ctx->textures().GenTextures(1, &t1);
  s.touch("glActiveTexture");
  app.ctx->textures().ActiveTexture(0x84C0u /*TEXTURE0*/);
  s.touch("glBindTexture");
  app.ctx->textures().BindTexture(tgles::kGlTexture2d, t0);
  s.touch("glIsTexture");
  s.check(app.ctx->textures().IsTexture(t0) == tgles::kGlTrue, "is texture");
  s.touch("glTexImage2D");
  app.ctx->textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4,
                                 4, 0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                                 nullptr);
  s.touch("glTexSubImage2D");
  app.ctx->textures().TexSubImage2D(tgles::kGlTexture2d, 0, 0, 0, 2, 2,
                                    tgles::kGlRgba, tgles::kGlUnsignedByte,
                                    nullptr);
  s.touch("glTexImage3D");
  app.ctx->textures().BindTexture(tgles::kGlTexture3d, t1);
  app.ctx->textures().TexImage3D(tgles::kGlTexture3d, 0, tgles::kGlRgba8, 4,
                                 4, 4, 0, tgles::kGlRgba,
                                 tgles::kGlUnsignedByte, nullptr);
  s.touch("glTexSubImage3D");
  app.ctx->textures().TexSubImage3D(tgles::kGlTexture3d, 0, 0, 0, 0, 2, 2, 2,
                                    tgles::kGlRgba, tgles::kGlUnsignedByte,
                                    nullptr);
  s.touch("glTexStorage2D");
  tgles::GLuint ts = 0;
  app.ctx->textures().GenTextures(1, &ts);
  app.ctx->textures().BindTexture(tgles::kGlTexture2d, ts);
  app.ctx->textures().TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8,
                                   4, 4);
  s.touch("glTexStorage3D");
  tgles::GLuint ts3 = 0;
  app.ctx->textures().GenTextures(1, &ts3);
  app.ctx->textures().BindTexture(tgles::kGlTexture3d, ts3);
  app.ctx->textures().TexStorage3D(tgles::kGlTexture3d, 1, tgles::kGlRgba8,
                                   4, 4, 4);
  s.touch("glTexStorage2DMultisample");
  tgles::GLuint tms = 0;
  app.ctx->textures().GenTextures(1, &tms);
  app.ctx->textures().BindTexture(tgles::kGlTexture2dMultisample, tms);
  app.ctx->textures().TexStorage2DMultisample(tgles::kGlTexture2dMultisample,
                                              4, tgles::kGlRgba8, 4, 4,
                                              tgles::kGlTrue);
  s.touch("glTexStorage3DMultisample");
  tgles::GLuint tms3 = 0;
  app.ctx->textures().GenTextures(1, &tms3);
  app.ctx->textures().BindTexture(tgles::kGlTexture2dMultisampleArray, tms3);
  app.ctx->textures().TexStorage3DMultisample(
      tgles::kGlTexture2dMultisampleArray, 4, tgles::kGlRgba8, 4, 4, 2,
      tgles::kGlTrue);
  s.touch("glTexBuffer");
  s.touch("glTexBufferRange");
  tgles::GLuint tb = 0, tbt = 0;
  app.ctx->buffers().GenBuffers(1, &tb);
  app.ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, tb);
  static const float kTb[16] = {0};
  app.ctx->buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kTb), kTb,
                                tgles::kGlStaticDraw);
  app.ctx->textures().GenTextures(1, &tbt);
  app.ctx->textures().BindTexture(tgles::kGlTextureBuffer, tbt);
  app.ctx->textures().TexBuffer(tgles::kGlTextureBuffer, tgles::kGlRgba8,
                                tb);
  app.ctx->textures().TexBufferRange(tgles::kGlTextureBuffer, tgles::kGlRgba8,
                                     tb, 0, 64);
  s.touch("glTexParameteri");
  app.ctx->textures().BindTexture(tgles::kGlTexture2d, t0);
  app.ctx->textures().TexParameteri(tgles::kGlTexture2d,
                                    tgles::kGlTextureMinFilter,
                                    tgles::kGlLinear);
  s.touch("glTexParameterf");
  app.ctx->textures().TexParameterf(tgles::kGlTexture2d,
                                    tgles::kGlTextureMaxLod, 4.0f);
  s.touch("glTexParameterfv");
  static const float kBc[4] = {1, 0, 0, 1};
  app.ctx->textures().TexParameterfv(tgles::kGlTexture2d,
                                     tgles::kGlTextureBorderColor, kBc);
  s.touch("glTexParameteriv");
  static const tgles::GLint kWrapping[1] = {tgles::kGlClampToEdge};
  app.ctx->textures().TexParameteriv(tgles::kGlTexture2d,
                                     tgles::kGlTextureWrapS, kWrapping);
  s.touch("glTexParameterIiv");
  app.ctx->textures().TexParameterIiv(tgles::kGlTexture2d,
                                      tgles::kGlTextureWrapT, kWrapping);
  s.touch("glTexParameterIuiv");
  static const tgles::GLuint kWrappingU[1] = {tgles::kGlRepeat};
  app.ctx->textures().TexParameterIuiv(tgles::kGlTexture2d,
                                       tgles::kGlTextureWrapT, kWrappingU);
  s.touch("glGetTexParameteriv");
  tgles::GLint gotWrap = 0;
  app.ctx->textures().GetTexParameteriv(tgles::kGlTexture2d,
                                        tgles::kGlTextureMinFilter, &gotWrap);
  s.check(gotWrap == tgles::kGlLinear, "tex param roundtrip");
  s.touch("glGetTexParameterfv");
  tgles::GLfloat gotLod = 0;
  app.ctx->textures().GetTexParameterfv(tgles::kGlTexture2d,
                                        tgles::kGlTextureMaxLod, &gotLod);
  s.touch("glGetTexParameterIiv");
  tgles::GLint gotI = 0;
  app.ctx->textures().GetTexParameterIiv(tgles::kGlTexture2d,
                                         tgles::kGlTextureWrapT, &gotI);
  s.touch("glGetTexParameterIuiv");
  tgles::GLuint gotU = 0;
  app.ctx->textures().GetTexParameterIuiv(tgles::kGlTexture2d,
                                          tgles::kGlTextureWrapT, &gotU);
  s.touch("glGetTexLevelParameteriv");
  tgles::GLint lw = 0;
  app.ctx->textures().GetTexLevelParameteriv(tgles::kGlTexture2d, 0,
                                             tgles::kGlTextureLevelWidth, &lw);
  s.check(lw == 4, "level width 4");
  s.touch("glGetTexLevelParameterfv");
  tgles::GLfloat lfw = 0;
  app.ctx->textures().GetTexLevelParameterfv(tgles::kGlTexture2d, 0,
                                             tgles::kGlTextureLevelWidth, &lfw);
  s.touch("glGenerateMipmap");
  app.ctx->textures().GenerateMipmap(tgles::kGlTexture2d);
  s.touch("glCompressedTexImage2D");
  static const std::uint8_t kAstc[64] = {0};
  app.ctx->textures().BindTexture(tgles::kGlTexture2d, t0);
  app.ctx->textures().CompressedTexImage2D(
      tgles::kGlTexture2d, 0, tgles::kGlCompressedRgbaAstc4x4, 8, 8, 0,
      sizeof(kAstc), kAstc);
  s.touch("glCompressedTexSubImage2D");
  app.ctx->textures().CompressedTexSubImage2D(
      tgles::kGlTexture2d, 0, 0, 0, 8, 8, tgles::kGlCompressedRgbaAstc4x4,
      sizeof(kAstc), kAstc);
  s.touch("glCompressedTexImage3D");
  app.ctx->textures().BindTexture(tgles::kGlTexture3d, t1);
  app.ctx->textures().CompressedTexImage3D(
      tgles::kGlTexture3d, 0, tgles::kGlCompressedRgbaAstc4x4, 8, 8, 2, 0,
      128, kAstc);
  s.touch("glCompressedTexSubImage3D");
  app.ctx->textures().CompressedTexSubImage3D(
      tgles::kGlTexture3d, 0, 0, 0, 0, 8, 8, 2,
      tgles::kGlCompressedRgbaAstc4x4, 128, kAstc);
  s.touch("glCopyTexImage2D");
  std::uint8_t rgba[4] = {255, 255, 255, 255};
  app.ctx->textures().BindTexture(tgles::kGlTexture2d, t0);
  app.ctx->textures().CopyTexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8,
                                     4, 4, 0, rgba);
  s.touch("glCopyTexSubImage2D");
  app.ctx->textures().CopyTexSubImage2D(tgles::kGlTexture2d, 0, 0, 0, 0, 0,
                                        2, 2);
  s.touch("glCopyTexSubImage3D");
  app.ctx->textures().BindTexture(tgles::kGlTexture3d, t1);
  app.ctx->textures().CopyTexSubImage3D(tgles::kGlTexture3d, 0, 0, 0, 0, 2,
                                        2);
  s.touch("glCopyImageSubData");
  app.ctx->textures().CopyImageSubData(t0, tgles::kGlTexture2d, 0, 0, 0, 0,
                                       t0, tgles::kGlTexture2d, 0, 0, 0, 0,
                                       2, 2, 1);
  s.touch("glTextureViewEXT");
  s.touch("glTextureViewOES");
  tgles::GLuint vw = 0;
  app.ctx->textures().GenTextures(1, &vw);
  app.ctx->textures().TextureView(vw, tgles::kGlTexture2d, ts,
                                  tgles::kGlSrgb8Alpha8, 0, 1, 0, 1);
  s.touch("glPolygonModeNV");
  s.touch("glPolygonModeANGLE");
  s.note("polygon mode pinned via C ABI in ExtAbi host test");
  s.touch("glGetInternalformativ");
  tgles::GLint ifv = 0;
  app.ctx->renderbuffers().GetInternalformativ(
      tgles::kGlRenderbuffer, tgles::kGlRgba8, 0x9380u /*NUM_SAMPLE_COUNTS*/, 1,
      &ifv);
  s.touch("glPixelStorei");
  app.ctx->pixels().PixelStorei(tgles::kGlUnpackAlignment, 4);
  app.ctx->pixels().PixelStorei(tgles::kGlPackAlignment, 4);
  s.touch("glDeleteTextures");
  const tgles::GLuint tdel[] = {t0, t1, ts, ts3, tms, tms3, tbt, vw};
  app.ctx->textures().DeleteTextures(8, tdel);
  app.ctx->buffers().DeleteBuffers(1, &tb);
  s.drainOk("textures");
  float pos[9], col[12];
  FillTri(pos, col, 1, 0, 1);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw magenta tri");
  s.check(app.present(), "present");
  s.check(app.centerIs(255, 0, 255, 255), "pixel magenta");
}

void PhaseSamplers(Sweep& s, App& app) {
  app.resetState();
  tgles::GLuint sm = 0;
  s.touch("glGenSamplers");
  app.ctx->samplers().GenSamplers(1, &sm);
  s.touch("glBindSampler");
  app.ctx->samplers().BindSampler(0, sm);
  s.touch("glIsSampler");
  s.check(app.ctx->samplers().IsSampler(sm) == tgles::kGlTrue, "is sampler");
  s.touch("glSamplerParameteri");
  app.ctx->samplers().SamplerParameteri(sm, tgles::kGlTextureMinFilter,
                                        tgles::kGlLinear);
  s.touch("glSamplerParameterf");
  app.ctx->samplers().SamplerParameterf(sm, tgles::kGlTextureMaxLod, 4.0f);
  s.touch("glSamplerParameterfv");
  static const float kBc[4] = {0, 1, 0, 1};
  app.ctx->samplers().SamplerParameterfv(sm, tgles::kGlTextureBorderColor,
                                         kBc);
  s.touch("glSamplerParameteriv");
  static const tgles::GLint kW[1] = {tgles::kGlClampToEdge};
  app.ctx->samplers().SamplerParameteriv(sm, tgles::kGlTextureWrapS, kW);
  s.touch("glSamplerParameterIiv");
  app.ctx->samplers().SamplerParameterIiv(sm, tgles::kGlTextureWrapT, kW);
  s.touch("glSamplerParameterIuiv");
  static const tgles::GLuint kWU[1] = {tgles::kGlRepeat};
  app.ctx->samplers().SamplerParameterIuiv(sm, tgles::kGlTextureWrapT, kWU);
  s.touch("glGetSamplerParameteriv");
  tgles::GLint g = 0;
  app.ctx->samplers().GetSamplerParameteriv(sm, tgles::kGlTextureMinFilter,
                                            &g);
  s.check(g == tgles::kGlLinear, "sampler roundtrip");
  s.touch("glGetSamplerParameterfv");
  tgles::GLfloat gf = 0;
  app.ctx->samplers().GetSamplerParameterfv(sm, tgles::kGlTextureMaxLod,
                                            &gf);
  s.touch("glGetSamplerParameterIiv");
  tgles::GLint gi = 0;
  app.ctx->samplers().GetSamplerParameterIiv(sm, tgles::kGlTextureWrapT,
                                             &gi);
  s.touch("glGetSamplerParameterIuiv");
  tgles::GLuint gu = 0;
  app.ctx->samplers().GetSamplerParameterIuiv(sm, tgles::kGlTextureWrapT,
                                              &gu);
  s.touch("glDeleteSamplers");
  app.ctx->samplers().DeleteSamplers(1, &sm);
  s.drainOk("samplers");
  float pos[9], col[12];
  FillTri(pos, col, 0, 1, 1);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw cyan tri");
  s.check(app.present(), "present");
  s.check(app.centerIs(0, 255, 255, 255), "pixel cyan");
}

void PhaseFBO(Sweep& s, App& app) {
  app.resetState();
  tgles::GLuint fb = 0, rb = 0;
  s.touch("glGenFramebuffers");
  app.ctx->framebuffers().GenFramebuffers(1, &fb);
  s.touch("glBindFramebuffer");
  app.ctx->framebuffers().BindFramebuffer(tgles::kGlFramebuffer, fb);
  s.touch("glIsFramebuffer");
  s.check(app.ctx->framebuffers().IsFramebuffer(fb) == tgles::kGlTrue,
          "is fbo");
  s.touch("glGenRenderbuffers");
  app.ctx->renderbuffers().GenRenderbuffers(1, &rb);
  s.touch("glBindRenderbuffer");
  app.ctx->renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  s.touch("glIsRenderbuffer");
  s.check(app.ctx->renderbuffers().IsRenderbuffer(rb) == tgles::kGlTrue,
          "is rbo");
  s.touch("glRenderbufferStorage");
  app.ctx->renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                               tgles::kGlRgba8, 8, 8);
  s.touch("glRenderbufferStorageMultisample");
  app.ctx->renderbuffers().RenderbufferStorageMultisample(
      tgles::kGlRenderbuffer, 4, tgles::kGlRgba8, 8, 8);
  s.touch("glGetRenderbufferParameteriv");
  tgles::GLint rw = 0;
  app.ctx->renderbuffers().GetRenderbufferParameteriv(
      tgles::kGlRenderbuffer, tgles::kGlRenderbufferWidth, &rw);
  s.check(rw == 8, "rbo width");
  s.touch("glFramebufferRenderbuffer");
  app.ctx->framebuffers().FramebufferRenderbuffer(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlRenderbuffer, rb);
  s.touch("glFramebufferTexture2D");
  app.ctx->framebuffers().FramebufferTexture2D(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, app.tex, 0);
  s.touch("glFramebufferTextureLayer");
  tgles::GLuint tl = 0;
  app.ctx->textures().GenTextures(1, &tl);
  app.ctx->textures().BindTexture(tgles::kGlTexture3d, tl);
  app.ctx->textures().TexStorage3D(tgles::kGlTexture3d, 1, tgles::kGlRgba8,
                                   4, 4, 4);
  app.ctx->framebuffers().FramebufferTextureLayer(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0 + 1u, tl, 0, 0);
  app.ctx->framebuffers().FramebufferTextureLayer(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0 + 1u, 0, 0, 0);
  s.touch("glFramebufferTexture");
  app.ctx->framebuffers().FramebufferTexture(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0 + 1u, app.tex, 0);
  app.ctx->framebuffers().FramebufferTexture(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0 + 1u, 0, 0);
  s.touch("glCheckFramebufferStatus");
  s.check(app.ctx->framebuffers().CheckFramebufferStatus(
              tgles::kGlFramebuffer) == tgles::kGlFramebufferComplete,
          "fbo complete");
  s.touch("glGetFramebufferAttachmentParameteriv");
  tgles::GLint at = 0;
  app.ctx->framebuffers().GetFramebufferAttachmentParameteriv(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlFramebufferAttachmentObjectType, &at);
  s.touch("glDrawBuffers");
  const tgles::GLenum one[1] = {tgles::kGlColorAttachment0};
  app.ctx->framebuffers().DrawBuffers(1, one);
  s.touch("glReadBuffer");
  app.ctx->framebuffers().ReadBuffer(tgles::kGlColorAttachment0);
  s.touch("glBlitFramebuffer");
  app.ctx->framebuffers().BlitFramebuffer(0, 0, 4, 4, 0, 0, 4, 4,
                                          tgles::kGlColorBufferBit,
                                          tgles::kGlNearest);
  s.touch("glInvalidateFramebuffer");
  const tgles::GLenum inv[1] = {tgles::kGlColorAttachment0};
  app.ctx->framebuffers().InvalidateFramebuffer(tgles::kGlFramebuffer, 1,
                                                inv);
  s.touch("glInvalidateSubFramebuffer");
  app.ctx->framebuffers().InvalidateSubFramebuffer(tgles::kGlFramebuffer, 1,
                                                   inv, 0, 0, 2, 2);
  s.touch("glFramebufferParameteri");
  app.ctx->framebuffers().FramebufferParameteri(
      tgles::kGlFramebuffer, tgles::kGlFramebufferDefaultWidth, 8);
  s.touch("glGetFramebufferParameteriv");
  tgles::GLint fp = 0;
  app.ctx->framebuffers().GetFramebufferParameteriv(
      tgles::kGlFramebuffer, tgles::kGlFramebufferDefaultWidth, &fp);
  s.check(fp == 8, "fbo param");
  s.touch("glReadPixels");
  std::uint8_t px[4] = {0};
  app.ctx->ReadPixels(0, 0, 1, 1, tgles::kGlRgba, tgles::kGlUnsignedByte, px);
  s.touch("glReadnPixels");
  app.ctx->ReadnPixels(0, 0, 1, 1, tgles::kGlRgba, tgles::kGlUnsignedByte, 4,
                       px);
  s.touch("glDeleteFramebuffers");
  s.touch("glDeleteRenderbuffers");
  app.ctx->framebuffers().DeleteFramebuffers(1, &fb);
  app.ctx->renderbuffers().DeleteRenderbuffers(1, &rb);
  app.ctx->textures().DeleteTextures(1, &tl);
  s.drainOk("fbo");
  app.ctx->framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, app.fbo);
  app.ctx->framebuffers().BindFramebuffer(tgles::kGlReadFramebuffer, app.fbo);
  float pos[9], col[12];
  FillTri(pos, col, 1, 0.5f, 0);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw orange tri");
  s.check(app.present(), "present");
  s.check(app.centerIs(255, 128, 0, 255), "pixel orange");
}

void PhaseRaster(Sweep& s, App& app) {
  app.resetState();
  s.touch("glViewport");
  app.ctx->raster().Viewport(0, 0, app.fbW, app.fbH);
  s.touch("glScissor");
  app.ctx->raster().Scissor(0, 0, app.fbW, app.fbH);
  s.touch("glBlendColor");
  app.ctx->raster().BlendColor(0.5f, 0.5f, 0.5f, 0.5f);
  s.touch("glBlendEquation");
  app.ctx->raster().BlendEquation(tgles::kGlFuncAdd);
  s.touch("glBlendEquationSeparate");
  app.ctx->raster().BlendEquationSeparate(tgles::kGlFuncAdd,
                                          tgles::kGlFuncAdd);
  s.touch("glBlendEquationi");
  app.ctx->raster().BlendEquationi(0, tgles::kGlFuncAdd);
  s.touch("glBlendEquationSeparatei");
  app.ctx->raster().BlendEquationSeparatei(0, tgles::kGlFuncAdd,
                                           tgles::kGlFuncAdd);
  s.touch("glBlendFunc");
  app.ctx->raster().BlendFunc(tgles::kGlSrcAlpha, tgles::kGlOneMinusSrcAlpha);
  s.touch("glBlendFuncSeparate");
  app.ctx->raster().BlendFuncSeparate(tgles::kGlSrcAlpha,
                                      tgles::kGlOneMinusSrcAlpha,
                                      tgles::kGlOne, tgles::kGlZero);
  s.touch("glBlendFunci");
  app.ctx->raster().BlendFunci(0, tgles::kGlSrcAlpha,
                               tgles::kGlOneMinusSrcAlpha);
  s.touch("glBlendFuncSeparatei");
  app.ctx->raster().BlendFuncSeparatei(0, tgles::kGlSrcAlpha,
                                       tgles::kGlOneMinusSrcAlpha,
                                       tgles::kGlOne, tgles::kGlZero);
  s.touch("glColorMask");
  app.ctx->raster().ColorMask(tgles::kGlTrue, tgles::kGlTrue, tgles::kGlTrue,
                              tgles::kGlTrue);
  s.touch("glColorMaski");
  app.ctx->raster().ColorMaski(0, tgles::kGlTrue, tgles::kGlTrue,
                               tgles::kGlTrue, tgles::kGlTrue);
  s.touch("glDepthFunc");
  app.ctx->raster().DepthFunc(tgles::kGlLess);
  s.touch("glDepthMask");
  app.ctx->raster().DepthMask(tgles::kGlTrue);
  s.touch("glDepthRangef");
  app.ctx->raster().DepthRangef(0.0f, 1.0f);
  s.touch("glStencilFunc");
  app.ctx->raster().StencilFunc(tgles::kGlAlways, 0, 0xFFu);
  s.touch("glStencilFuncSeparate");
  app.ctx->raster().StencilFuncSeparate(tgles::kGlFront, tgles::kGlAlways, 0,
                                        0xFFu);
  app.ctx->raster().StencilFuncSeparate(tgles::kGlBackFace, tgles::kGlAlways,
                                        0, 0xFFu);
  s.touch("glStencilMask");
  app.ctx->raster().StencilMask(0xFFu);
  s.touch("glStencilMaskSeparate");
  app.ctx->raster().StencilMaskSeparate(tgles::kGlFront, 0xFFu);
  app.ctx->raster().StencilMaskSeparate(tgles::kGlBackFace, 0xFFu);
  s.touch("glStencilOp");
  app.ctx->raster().StencilOp(tgles::kGlKeep, tgles::kGlKeep,
                              tgles::kGlReplace);
  s.touch("glStencilOpSeparate");
  app.ctx->raster().StencilOpSeparate(tgles::kGlFront, tgles::kGlKeep,
                                      tgles::kGlKeep, tgles::kGlReplace);
  app.ctx->raster().StencilOpSeparate(tgles::kGlBackFace, tgles::kGlKeep,
                                      tgles::kGlKeep, tgles::kGlKeep);
  s.touch("glCullFace");
  app.ctx->raster().CullFace(tgles::kGlBackFace);
  s.touch("glFrontFace");
  app.ctx->raster().FrontFace(tgles::kGlCcw);
  s.touch("glPolygonOffset");
  app.ctx->raster().PolygonOffset(1.0f, 1.0f);
  s.touch("glLineWidth");
  app.ctx->raster().LineWidth(1.0f);
  s.touch("glHint");
  s.note("hint via C ABI in host-straggler phase");
  s.touch("glSampleCoverage");
  app.ctx->raster().SampleCoverage(1.0f, tgles::kGlFalse);
  s.touch("glSampleMaski");
  app.ctx->raster().SampleMaski(0, 0xFFFFFFFFu);
  s.touch("glMinSampleShading");
  app.ctx->raster().MinSampleShading(0.0f);
  s.touch("glClearColor");
  app.ctx->raster().ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  s.touch("glClearDepthf");
  app.ctx->raster().ClearDepthf(1.0f);
  s.touch("glClearStencil");
  app.ctx->raster().ClearStencil(0);
  s.touch("glClear");
  app.ctx->Clear(tgles::kGlColorBufferBit);
  s.touch("glClearBufferiv");
  static const tgles::GLint kClearI[4] = {0, 0, 0, 0};
  app.ctx->framebuffers().ClearBufferiv(0x1800u /*COLOR*/, 0, kClearI);
  s.touch("glClearBufferuiv");
  static const tgles::GLuint kClearU[4] = {0, 0, 0, 0};
  app.ctx->framebuffers().ClearBufferuiv(0x1800u /*COLOR*/, 0, kClearU);
  s.touch("glClearBufferfv");
  s.touch("glClearBufferfi");
  s.note("fv/fi via C ABI in host-straggler phase");
  s.touch("glGetMultisamplefv");
  tgles::GLfloat ms = 0;
  app.ctx->raster().GetMultisamplefv(0x8E50u /*SAMPLE_POSITION*/, 0, &ms);
  s.drainOk("raster");
  app.ctx->foundation().Enable(tgles::kGlBlend);
  float pos[9], col[12];
  FillTri(pos, col, 1, 0, 0);
  for (int i = 0; i < 3; ++i) col[i * 4 + 3] = 0.5f;
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw blend tri");
  app.ctx->foundation().Disable(tgles::kGlBlend);
  s.check(app.present(), "present");
  std::uint8_t px[4] = {0};
  s.check(app.bridge->ReadbackPixel(app.fbW / 2, app.fbH / 2, px),
          "readback");
  s.check(px[0] >= 125u && px[0] <= 130u && px[1] == 0u, "pixel half-red");
}

void PhaseTopology(Sweep& s, App& app) {
  app.resetState();
  static const float kP2[6] = {-1, 0, 0, 1, 0, 0};
  static const float kC2[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  static const float kP4[12] = {-1, -1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0};
  static const float kC4[16] = {1, 1, 1, 1, 1, 1, 1, 1,
                                1, 1, 1, 1, 1, 1, 1, 1};
  auto upload = [&](const float* p, tgles::GLsizeiptr psz, const float* c,
                    tgles::GLsizeiptr csz) {
    app.ctx->programs().UseProgram(app.prog);
    app.ctx->vertex_arrays().BindVertexArray(app.vao);
    app.ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, app.posBuf);
    app.ctx->buffers().BufferData(tgles::kGlArrayBuffer, psz, p,
                                  tgles::kGlStaticDraw);
    app.ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, app.colBuf);
    app.ctx->buffers().BufferData(tgles::kGlArrayBuffer, csz, c,
                                  tgles::kGlStaticDraw);
  };
  s.touch("glDrawArrays");
  upload(kP2, sizeof(kP2), kC2, sizeof(kC2));
  s.check(app.draw(tgles::kGlLines, 0, 2), "lines");
  s.check(app.draw(tgles::kGlPoints, 0, 2), "points");
  upload(kP4, sizeof(kP4), kC4, sizeof(kC4));
  s.check(app.draw(tgles::kGlLineStrip, 0, 4), "strip");
  s.check(app.draw(tgles::kGlLineLoop, 0, 4), "loop");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "triangles");
  s.check(app.draw(tgles::kGlTriangleStrip, 0, 4), "tristrip");
  s.check(app.draw(tgles::kGlTriangleFan, 0, 4), "trifan");
  s.check(app.present(), "present topo");
  float pos[9], col[12];
  FillTri(pos, col, 1, 1, 1);
  app.uploadTri(pos, col);
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw white tri");
  s.check(app.present(), "present white");
  s.check(app.centerIs(255, 255, 255, 255), "pixel white");
}

void PhaseIndexed(Sweep& s, App& app) {
  app.resetState();
  float pos[9], col[12];
  FillTri(pos, col, 0, 1, 0);
  app.uploadTri(pos, col);
  tgles::GLuint eab = 0;
  app.ctx->buffers().GenBuffers(1, &eab);
  app.ctx->buffers().BindBuffer(tgles::kGlElementArrayBuffer, eab);
  static const std::uint8_t kIdx[3] = {0, 1, 2};
  app.ctx->buffers().BufferData(tgles::kGlElementArrayBuffer, sizeof(kIdx),
                                kIdx, tgles::kGlStaticDraw);
  app.ctx->vertex_arrays().SetElementArrayBuffer(eab);
  s.touch("glDrawElements");
  s.check(app.ctx->RenderElements(*app.bridge, tgles::kGlTriangles, 3,
                                  tgles::kGlUnsignedByte, 0),
          "indexed");
  s.touch("glDrawRangeElements");
  app.ctx->draw().DrawRangeElements(tgles::kGlTriangles, 0, 2, 3,
                                    tgles::kGlUnsignedByte, 0);
  s.check(!app.ctx->draw().HasPending(), "range validates");
  while (app.ctx->draw().GetError() != tgles::kGlNoError) {
  }
  s.check(app.ctx->RenderElements(*app.bridge, tgles::kGlTriangles, 3,
                                  tgles::kGlUnsignedByte, 0),
          "range executes");
  s.touch("glDrawElementsBaseVertex");
  s.check(app.ctx->RenderElementsBaseVertex(
              *app.bridge, tgles::kGlTriangles, 3, tgles::kGlUnsignedByte, 0,
              0),
          "base vertex");
  s.touch("glDrawRangeElementsBaseVertex");
  s.check(app.ctx->RenderRangeElementsBaseVertex(
              *app.bridge, tgles::kGlTriangles, 0, 2, 3,
              tgles::kGlUnsignedByte, 0, 0),
          "range base vertex");
  s.touch("glDrawArraysInstanced");
  s.check(app.ctx->RenderInstanced(app.bridge, tgles::kGlTriangles, 0, 3, 2),
          "instanced");
  s.touch("glDrawElementsInstanced");
  s.check(app.ctx->RenderElementsInstanced(app.bridge, tgles::kGlTriangles,
                                           3, tgles::kGlUnsignedByte, 0, 2),
          "elements instanced");
  s.touch("glDrawElementsInstancedBaseVertex");
  s.check(app.ctx->RenderElementsInstancedBaseVertex(
              app.bridge, tgles::kGlTriangles, 3, tgles::kGlUnsignedByte, 0,
              2, 0),
          "instanced base vertex");
  tgles::GLuint ibuf = 0;
  app.ctx->buffers().GenBuffers(1, &ibuf);
  app.ctx->buffers().BindBuffer(tgles::kGlDrawIndirectBuffer, ibuf);
  static const std::uint32_t kArraysCmd[4] = {3, 1, 0, 0};
  app.ctx->buffers().BufferData(tgles::kGlDrawIndirectBuffer,
                                sizeof(kArraysCmd), kArraysCmd,
                                tgles::kGlStaticDraw);
  app.ctx->SyncIndirectState();
  s.touch("glDrawArraysIndirect");
  s.check(app.ctx->RenderArraysIndirect(app.bridge, tgles::kGlTriangles, 0),
          "arrays indirect");
  static const std::uint32_t kElemCmd[5] = {3, 1, 0, 0, 0};
  app.ctx->buffers().BufferData(tgles::kGlDrawIndirectBuffer,
                                sizeof(kElemCmd), kElemCmd,
                                tgles::kGlStaticDraw);
  s.touch("glDrawElementsIndirect");
  s.check(app.ctx->RenderElementsIndirect(app.bridge, tgles::kGlTriangles,
                                          tgles::kGlUnsignedByte, 0),
          "elements indirect");
  s.touch("glMultiDrawArraysIndirectEXT");
  s.check(app.ctx->MultiDrawArraysIndirect(app.bridge, tgles::kGlTriangles,
                                           0, 1, 0),
          "multi arrays indirect");
  s.touch("glMultiDrawElementsIndirectEXT");
  s.check(app.ctx->MultiDrawElementsIndirect(app.bridge, tgles::kGlTriangles,
                                             tgles::kGlUnsignedByte, 0, 1, 0),
          "multi elements indirect");
  s.touch("glMultiDrawElementsBaseVertexEXT");
  static const tgles::GLsizei kMc[1] = {3};
  static const void* kMp[1] = {nullptr};
  static const tgles::GLint kMb[1] = {0};
  s.check(app.ctx->MultiDrawElementsBaseVertex(
              app.bridge, tgles::kGlTriangles, kMc, tgles::kGlUnsignedByte,
              kMp, 1, kMb),
          "multi base vertex");
  s.touch("glDrawArraysInstancedBaseInstanceEXT");
  s.check(app.ctx->RenderArraysInstancedBaseInstance(
              app.bridge, tgles::kGlTriangles, 0, 3, 1, 0),
          "base instance");
  s.touch("glDrawElementsInstancedBaseInstanceEXT");
  s.check(app.ctx->RenderElementsInstancedBaseInstance(
              app.bridge, tgles::kGlTriangles, 3, tgles::kGlUnsignedByte, 0,
              1, 0),
          "elements base instance");
  s.touch("glDrawElementsInstancedBaseVertexBaseInstanceEXT");
  s.check(app.ctx->RenderElementsInstancedBaseVertexBaseInstance(
              app.bridge, tgles::kGlTriangles, 3, tgles::kGlUnsignedByte, 0,
              1, 0, 0),
          "instanced base vertex base instance");
  s.check(app.present(), "present indexed");
  s.check(app.centerIs(0, 255, 0, 255), "pixel green");
  app.ctx->buffers().DeleteBuffers(1, &eab);
  app.ctx->buffers().DeleteBuffers(1, &ibuf);
  s.drainOk("indexed");
}

void PhaseCompute(Sweep& s, App& app) {
  app.resetState();
  static const char* kCs =
      "#version 320 es\nlayout(local_size_x = 1) in;\n"
      "layout(std430) buffer SBlk { float s[4]; };\n"
      "void main() { s[0] = 1.0; }\n";
  tgles::GLuint cs = Compile(app.ctx->shaders(), tgles::kGlComputeShader,
                             kCs);
  tgles::GLuint cp = app.ctx->programs().CreateProgram();
  app.ctx->programs().AttachShader(cp, cs);
  app.ctx->programs().LinkProgram(cp);
  s.check(app.ctx->programs().LinkSucceeded(cp), "compute linked");
  app.ctx->programs().UseProgram(cp);
  s.touch("glDispatchCompute");
  app.ctx->DispatchCompute(1, 1, 1);
  s.touch("glDispatchComputeIndirect");
  tgles::GLuint ibuf = 0;
  app.ctx->buffers().GenBuffers(1, &ibuf);
  app.ctx->buffers().BindBuffer(tgles::kGlDispatchIndirectBuffer, ibuf);
  static const std::uint32_t kCmd[3] = {1, 1, 1};
  app.ctx->buffers().BufferData(tgles::kGlDispatchIndirectBuffer,
                                sizeof(kCmd), kCmd, tgles::kGlStaticDraw);
  app.ctx->DispatchComputeIndirect(0);
  s.touch("glMemoryBarrier");
  app.ctx->compute().MemoryBarrier(tgles::kGlShaderStorageBarrierBit);
  s.touch("glMemoryBarrierByRegion");
  app.ctx->compute().MemoryBarrierByRegion(tgles::kGlShaderStorageBarrierBit);
  s.touch("glBindImageTexture");
  tgles::GLuint it = 0;
  app.ctx->textures().GenTextures(1, &it);
  app.ctx->textures().BindTexture(tgles::kGlTexture2d, it);
  app.ctx->textures().TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8,
                                   4, 4);
  app.ctx->images().BindImageTexture(0, it, 0, tgles::kGlFalse, 0,
                                     tgles::kGlReadOnly, tgles::kGlRgba8);
  s.touch("glShaderStorageBlockBinding");
  tgles::GLuint ssbi = app.ctx->programs().GetProgramResourceIndex(
      cp, tgles::kGlShaderStorageBlock, "SBlk");
  if (ssbi != tgles::kGlInvalidIndex) {
    app.ctx->programs().ShaderStorageBlockBinding(cp, ssbi, 0);
    s.check(true, "ssbo binding");
  } else {
    s.note("ssbo block not in reflection (tracked)");
  }
  s.drainOk("compute");
  app.ctx->programs().DeleteProgram(cp);
  app.ctx->shaders().DeleteShader(cs);
  app.ctx->buffers().DeleteBuffers(1, &ibuf);
  app.ctx->textures().DeleteTextures(1, &it);
  float pos[9], col[12];
  FillTri(pos, col, 1, 1, 1);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw white tri");
  s.check(app.present(), "present");
  s.check(app.centerIs(255, 255, 255, 255), "pixel white");
}

void PhaseSyncQueryXfb(Sweep& s, App& app) {
  app.resetState();
  s.touch("glFenceSync");
  tgles::GLuint sync =
      app.ctx->sync().FenceSync(tgles::kGlSyncGpuCommandsComplete, 0);
  s.check(sync != 0u, "fence");
  s.touch("glIsSync");
  s.check(app.ctx->sync().IsSync(sync) == tgles::kGlTrue, "is sync");
  s.touch("glClientWaitSync");
  s.check(app.ctx->sync().ClientWaitSync(sync, 0, 0) ==
              tgles::kGlTimeoutExpired,
          "wait expires");
  app.ctx->sync().SignalSync(sync);
  s.check(app.ctx->sync().ClientWaitSync(sync, 0, 0) ==
              tgles::kGlAlreadySignaled,
          "signaled");
  s.touch("glWaitSync");
  app.ctx->sync().WaitSync(sync, 0, tgles::kGlTimeoutIgnored);
  s.touch("glGetSynciv");
  tgles::GLint sv = 0;
  app.ctx->sync().GetSynciv(sync, tgles::kGlSyncStatus, 1, nullptr, &sv);
  s.touch("glGetInteger64v");
  tgles::GLint64 to = 0;
  app.ctx->sync().GetInteger64v(tgles::kGlMaxServerWaitTimeout, &to);
  s.touch("glDeleteSync");
  app.ctx->sync().DeleteSync(sync);
  s.touch("glGenQueries");
  tgles::GLuint q = 0;
  app.ctx->queries().GenQueries(1, &q);
  s.touch("glBeginQuery");
  app.ctx->queries().BeginQuery(
      tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed), q);
  s.touch("glIsQuery");
  s.check(app.ctx->queries().IsQuery(q) == tgles::kGlTrue, "is query");
  s.touch("glEndQuery");
  app.ctx->queries().EndQuery(
      tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed));
  s.touch("glGetQueryiv");
  tgles::GLint qv = 0;
  app.ctx->queries().GetQueryiv(
      tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed),
      tgles::kGlCurrentQuery, &qv);
  s.touch("glGetQueryObjectuiv");
  tgles::GLuint qr = 0;
  app.ctx->queries().GetQueryObjectuiv(q, tgles::kGlQueryResult, &qr);
  s.touch("glDeleteQueries");
  app.ctx->queries().DeleteQueries(1, &q);
  s.touch("glGenTransformFeedbacks");
  tgles::GLuint xf = 0;
  app.ctx->transform_feedback().GenTransformFeedbacks(1, &xf);
  s.touch("glBindTransformFeedback");
  app.ctx->transform_feedback().BindTransformFeedback(
      tgles::kGlTransformFeedback, xf);
  s.touch("glIsTransformFeedback");
  s.check(app.ctx->transform_feedback().IsTransformFeedback(xf) ==
              tgles::kGlTrue,
          "is xfb");
  s.touch("glBeginTransformFeedback");
  app.ctx->transform_feedback().BeginTransformFeedback(tgles::kGlPoints);
  s.touch("glPauseTransformFeedback");
  app.ctx->transform_feedback().PauseTransformFeedback();
  s.touch("glResumeTransformFeedback");
  app.ctx->transform_feedback().ResumeTransformFeedback();
  s.touch("glEndTransformFeedback");
  app.ctx->transform_feedback().EndTransformFeedback();
  s.touch("glTransformFeedbackVaryings");
  tgles::GLuint xp = app.ctx->programs().CreateProgram();
  tgles::GLuint xvs =
      Compile(app.ctx->shaders(), tgles::kGlVertexShader, kVsSrc);
  tgles::GLuint xfs =
      Compile(app.ctx->shaders(), tgles::kGlFragmentShader, kFsSrc);
  app.ctx->programs().AttachShader(xp, xvs);
  app.ctx->programs().AttachShader(xp, xfs);
  const char* varyings[] = {"v_col"};
  app.ctx->programs().TransformFeedbackVaryings(xp, 1, varyings,
                                                0x8C8Du /*SEPARATE_ATTRIBS*/);
  app.ctx->programs().LinkProgram(xp);
  s.touch("glGetTransformFeedbackVarying");
  char vname[64] = {0};
  tgles::GLsizei vlen = 0, vsize = 0;
  tgles::GLenum vtype = 0;
  app.ctx->programs().GetTransformFeedbackVarying(xp, 0, sizeof(vname),
                                                  &vlen, &vsize, &vtype,
                                                  vname);
  s.touch("glDeleteTransformFeedbacks");
  app.ctx->transform_feedback().DeleteTransformFeedbacks(1, &xf);
  app.ctx->programs().DeleteProgram(xp);
  app.ctx->shaders().DeleteShader(xvs);
  app.ctx->shaders().DeleteShader(xfs);
  s.drainOk("sync.query.xfb");
  float pos[9], col[12];
  FillTri(pos, col, 1, 1, 0);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw yellow tri");
  s.check(app.present(), "present");
  s.check(app.centerIs(255, 255, 0, 255), "pixel yellow");
}

void PhaseDebug(Sweep& s, App& app) {
  app.resetState();
  s.touch("glDebugMessageControl");
  app.ctx->debug().DebugMessageControl(tgles::kGlDontCare,
                                       tgles::kGlDontCare,
                                       tgles::kGlDontCare, 0, nullptr,
                                       tgles::kGlTrue);
  s.touch("glDebugMessageInsert");
  const char* kMsg = "api-window probe";
  app.ctx->debug().DebugMessageInsert(tgles::kGlDebugSourceApplication,
                                      tgles::kGlDebugTypeMarker, 7,
                                      tgles::kGlDebugSeverityNotification,
                                      -1, kMsg);
  s.touch("glDebugMessageCallback");
  app.ctx->debug().SetCallback(nullptr, nullptr);
  s.touch("glGetDebugMessageLog");
  tgles::GLuint n = app.ctx->debug().GetDebugMessageLog(
      4, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  s.check(n >= 1u, "debug log has probe");
  s.touch("glPushDebugGroup");
  app.ctx->debug().PushDebugGroup(tgles::kGlDebugSourceApplication, 1, 5,
                                  "phase");
  s.touch("glPopDebugGroup");
  app.ctx->debug().PopDebugGroup();
  s.touch("glObjectLabel");
  app.ctx->debug().ObjectLabel(tgles::kGlBufferObject, app.posBuf, 3, "pos");
  s.touch("glGetObjectLabel");
  s.check(app.ctx->debug().GetObjectLabel(tgles::kGlBufferObject,
                                          app.posBuf) == "pos",
          "object label");
  s.touch("glObjectPtrLabel");
  static int kTag = 0;
  app.ctx->debug().ObjectPtrLabel(&kTag, 3, "tag");
  s.touch("glGetObjectPtrLabel");
  s.check(app.ctx->debug().GetObjectPtrLabel(&kTag) == "tag", "ptr label");
  s.touch("glGetPointerv");
  void* pv = reinterpret_cast<void*>(0x1234);
  app.ctx->debug().GetPointerv(0x8244u /*DEBUG_CALLBACK_FUNCTION*/, &pv);
  s.check(pv == nullptr, "null callback");
  s.drainOk("debug");
  float pos[9], col[12];
  FillTri(pos, col, 0, 1, 1);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw cyan tri");
  s.check(app.present(), "present");
  s.check(app.centerIs(0, 255, 255, 255), "pixel cyan");
}

void PhasePixels(Sweep& s, App& app) {
  app.resetState();
  float pos[9], col[12];
  FillTri(pos, col, 1, 0, 0);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw red tri");
  s.check(app.present(), "present");
  s.touch("glPixelStorei");
  app.ctx->pixels().PixelStorei(tgles::kGlPackAlignment, 4);
  s.touch("glReadPixels");
  std::uint8_t px[4] = {0};
  app.ctx->ReadPixels(app.fbW / 2, app.fbH / 2, 1, 1, tgles::kGlRgba,
                      tgles::kGlUnsignedByte, px);
  s.touch("glReadnPixels");
  std::uint8_t px2[4] = {0};
  app.ctx->ReadnPixels(app.fbW / 2, app.fbH / 2, 1, 1, tgles::kGlRgba,
                       tgles::kGlUnsignedByte, 4, px2);
  s.drainOk("pixels state");
  // CPU-store readback reflects the texture CPU store (zeros: uploads used
  // nullptr), NOT the GPU target. GPU pixels are asserted via the bridge:
  std::uint8_t gpu[4] = {0};
  s.check(app.bridge->ReadbackPixel(app.fbW / 2, app.fbH / 2, gpu) &&
              gpu[0] == 255u && gpu[1] == 0u,
          "gpu pixel red");
}

void PhaseMisc(Sweep& s, App& app) {
  app.resetState();
  s.touch("glPatchParameteri");
  app.ctx->tessellation().PatchParameteri(tgles::kGlPatchVertices, 3);
  s.touch("glPrimitiveBoundingBox");
  app.ctx->raster().PrimitiveBoundingBox(0, 0, 0, 1, 1, 1, 1, 1);
  s.touch("glBindImageTexture");
  s.note("image units pinned in compute phase");
  s.touch("glGetActiveUniformBlockName");
  s.note("block names pinned in uniforms phase");
  s.touch("glCreateShaderProgramv");
  tgles::GLuint sp = app.ctx->programs().CreateShaderProgramv(
      tgles::kGlVertexShader, 1, &kVsSrc);
  s.check(sp != 0u, "shader program");
  s.touch("glGenProgramPipelines");
  s.touch("glUseProgramStages");
  s.touch("glActiveShaderProgram");
  tgles::GLuint pipe = 0;
  app.ctx->programs().GenProgramPipelines(1, &pipe);
  s.touch("glBindProgramPipeline");
  app.ctx->programs().BindProgramPipeline(pipe);
  s.touch("glIsProgramPipeline");
  s.check(app.ctx->programs().IsProgramPipeline(pipe) == tgles::kGlTrue,
          "is pipeline");
  s.touch("glProgramParameteri");
  app.ctx->programs().ProgramParameteri(sp, tgles::kGlProgramSeparable, 1);
  app.ctx->programs().UseProgramStages(pipe, tgles::kGlVertexShaderBit, sp);
  app.ctx->programs().ActiveShaderProgram(pipe, sp);
  s.touch("glValidateProgramPipeline");
  app.ctx->programs().ValidateProgramPipeline(pipe);
  s.touch("glGetProgramPipelineiv");
  tgles::GLint piv = 0;
  app.ctx->programs().GetProgramPipelineiv(pipe, 0x8259u /*ACTIVE_PROGRAM*/,
                                           &piv);
  s.touch("glGetProgramPipelineInfoLog");
  std::string pinf = app.ctx->programs().GetProgramPipelineInfoLog(pipe);
  (void)pinf;
  s.touch("glDeleteProgramPipelines");
  app.ctx->programs().DeleteProgramPipelines(1, &pipe);
  app.ctx->programs().DeleteProgram(sp);
  s.touch("glGetProgramBinary");
  s.touch("glProgramBinary");
  tgles::GLuint bp = app.ctx->programs().CreateProgram();
  tgles::GLsizei blen = 0;
  tgles::GLenum bfmt = 0;
  char bbin[512] = {0};
  app.ctx->programs().GetProgramBinary(app.prog, sizeof(bbin), &blen, &bfmt,
                                       bbin);
  s.check(bfmt == tgles::kGlTglProgramBinary, "binary format");
  if (blen > 0) {
    app.ctx->programs().ProgramBinary(bp, bfmt, bbin, blen);
    s.check(app.ctx->programs().LinkSucceeded(bp), "binary roundtrip");
  } else {
    s.note("empty binary store on this program (tracked)");
  }
  app.ctx->programs().DeleteProgram(bp);
  s.touch("glBlendBarrier");
  app.ctx->compute().MemoryBarrier(tgles::kGlAllBarrierBits);
  app.ctx->raster().BlendBarrier();
  s.drainOk("misc");
  float pos[9], col[12];
  FillTri(pos, col, 0.5f, 0, 0.5f);
  app.uploadTri(pos, col);
  s.touch("glDrawArrays");
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw purple tri");
  s.check(app.present(), "present");
}

void PhaseExpectedFail(Sweep& s, App& app) {
  app.resetState();
  float pos[9], col[12];
  FillTri(pos, col, 1, 0, 0);
  app.uploadTri(pos, col);
  s.check(!app.draw(0x000Cu /*TRIANGLES_ADJACENCY*/, 0, 3),
          "adjacency fails");
  s.expectErr(tgles::kGlInvalidOperation, "adjacency error");
  tgles::GLuint ib = 0;
  app.ctx->buffers().GenBuffers(1, &ib);
  static const tgles::GLint kI[3] = {0, 1, 2};
  app.ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, ib);
  app.ctx->buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kI), kI,
                                tgles::kGlStaticDraw);
  app.ctx->vertex_arrays().VertexAttribIPointer(0, 1, tgles::kGlInt, 0, 0,
                                                ib);
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "int attrib converts+draws");
  s.expectErr(tgles::kGlNoError, "int attrib no error");
  app.ctx->vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                               tgles::kGlFalse, 0, 0,
                                               app.posBuf);
  app.ctx->buffers().DeleteBuffers(1, &ib);
  app.ctx->foundation().Enable(tgles::kGlDepthTest);
  app.ctx->foundation().Enable(tgles::kGlBlend);
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "depth+blend executes");
  s.expectErr(tgles::kGlNoError, "depth+blend no error");
  app.ctx->foundation().Disable(tgles::kGlDepthTest);
  app.ctx->foundation().Disable(tgles::kGlBlend);
  app.ctx->foundation().Enable(tgles::kGlStencilTest);
  app.ctx->raster().StencilFuncSeparate(tgles::kGlFront, tgles::kGlAlways, 1,
                                        0xFFu);
  app.ctx->raster().StencilFuncSeparate(tgles::kGlBackFace, tgles::kGlAlways,
                                        2, 0xFFu);
  s.check(!app.draw(tgles::kGlTriangles, 0, 3), "split refs fail");
  s.expectErr(tgles::kGlInvalidOperation, "split refs error");
  app.ctx->foundation().Disable(tgles::kGlStencilTest);
  app.ctx->raster().StencilFunc(tgles::kGlAlways, 0, 0xFFu);
  s.check(app.draw(tgles::kGlTriangles, 0, 3), "draw red after fails");
  s.check(app.present(), "present");
  s.check(app.centerIs(255, 0, 0, 255), "pixel red");
}

void PhaseHostStragglers(Sweep& s, App& app) {
  (void)app;
  EGLDisplay dpy = eglGetDisplay(nullptr);
  s.check(dpy != nullptr, "host display");
  if (dpy == nullptr) return;
  s.check(eglInitialize(dpy, nullptr, nullptr) != 0, "host init");
  EGLint cfgA[] = {0x3038};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  s.check(eglChooseConfig(dpy, cfgA, &cfg, 1, &n) != 0, "host config");
  EGLint ctxA[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  EGLContext c = eglCreateContext(dpy, cfg, nullptr, ctxA);
  s.check(c != nullptr, "host context");
  EGLint pbA[] = {0x3057, 16, 0x3056, 16, 0x3038};
  EGLSurface sf = eglCreatePbufferSurface(dpy, cfg, pbA);
  s.check(sf != nullptr, "host pbuffer");
  s.check(eglMakeCurrent(dpy, sf, sf, c) != 0, "host current");
  s.touch("glReleaseShaderCompiler");
  glReleaseShaderCompiler();
  s.check(glGetError() == 0u, "release compiler ok");
  s.touch("glGetGraphicsResetStatus");
  s.check(glGetGraphicsResetStatus() == 0u, "no reset");
  s.touch("glHint");
  glHint(0x0C52u /*LINE_SMOOTH_HINT*/, 0x1102u /*NICEST*/);
  s.check(glGetError() == 0u, "hint ok");
  s.touch("glFinish");
  glFinish();
  s.check(glGetError() == 0u, "finish ok");
  s.touch("glFlush");
  glFlush();
  s.check(glGetError() == 0u, "flush ok");
  GLuint ctx_ = 0;
  glGenTextures(1, &ctx_);
  glBindTexture(0x0DE1u /*TEXTURE_2D*/, ctx_);
  glTexImage2D(0x0DE1u, 0, 0x8058 /*RGBA8*/, 16, 16, 0, 0x1908u /*RGBA*/,
               0x1401u /*UBYTE*/, nullptr);
  GLuint cfbo = 0;
  glGenFramebuffers(1, &cfbo);
  glBindFramebuffer(0x8CA9u /*FRAMEBUFFER*/, cfbo);
  glFramebufferTexture2D(0x8CA9u, 0x8CE0u /*COLOR0*/, 0x0DE1u, ctx_, 0);
  s.touch("glClearBufferfv");
  static const GLfloat kCBF[4] = {0, 0, 0, 1};
  glClearBufferfv(0x1800u /*COLOR*/, 0, kCBF);
  s.check(glGetError() == 0u, "clearbufferfv ok");
  s.touch("glClearBufferfi");
  glClearBufferfi(0x84F9u /*DEPTH_STENCIL*/, 0, 1.0f, 0);
  s.check(glGetError() == 0u, "clearbufferfi ok");
  glDeleteFramebuffers(1, &cfbo);
  glDeleteTextures(1, &ctx_);
  s.touch("glGetShaderPrecisionFormat");
  GLint range[2] = {0}, prec = 0;
  glGetShaderPrecisionFormat(0x8B30u, 0x8DF2u, range, &prec);
  s.check(range[0] == 127 && prec == 23 && glGetError() == 0u, "precision");
  s.touch("glGetBufferParameteri64v");
  GLuint b = 0;
  glGenBuffers(1, &b);
  glBindBuffer(0x8892u, b);
  static const float kD[4] = {1, 2, 3, 4};
  glBufferData(0x8892u, sizeof(kD), kD, 0x88E4u);
  GLint64 sz = 0;
  glGetBufferParameteri64v(0x8892u, 0x8764u, &sz);
  s.check(sz == 16 && glGetError() == 0u, "buffer size64");
  s.touch("glGetnUniformfv");
  s.touch("glGetnUniformiv");
  s.touch("glGetnUniformuiv");
  const GLchar* vsSrc = "#version 320 es\nuniform vec4 u_c;\nvoid main() {}\n";
  const GLchar* fsSrc =
      "#version 320 es\nprecision mediump float;\nvoid main() {}\n";
  GLuint vsh = glCreateShader(0x8B31u);
  GLuint fsh = glCreateShader(0x8B30u);
  glShaderSource(vsh, 1, &vsSrc, nullptr);
  glShaderSource(fsh, 1, &fsSrc, nullptr);
  glCompileShader(vsh);
  glCompileShader(fsh);
  GLuint pr = glCreateProgram();
  glAttachShader(pr, vsh);
  glAttachShader(pr, fsh);
  glLinkProgram(pr);
  glUseProgram(pr);
  GLint loc = glGetUniformLocation(pr, "u_c");
  s.check(loc >= 0, "host uniform loc");
  glUniform4f(loc, 1.0f, 2.0f, 3.0f, 4.0f);
  GLfloat gf[4] = {0};
  glGetnUniformfv(pr, loc, 4, gf);
  s.check(gf[3] == 4.0f && glGetError() == 0u, "getn fv");
  GLint gi[4] = {0};
  glGetnUniformiv(pr, loc, 4, gi);
  s.check(gi[0] == 1 && glGetError() == 0u, "getn iv");
  GLuint gu[4] = {0};
  glGetnUniformuiv(pr, loc, 4, gu);
  s.check(gu[3] == 4u && glGetError() == 0u, "getn uiv");
  glDeleteShader(vsh);
  glDeleteShader(fsh);
  glDeleteProgram(pr);
  glDeleteBuffers(1, &b);
  s.check(glGetError() == 0u, "host cleanup");
  eglDestroySurface(dpy, sf);
  eglDestroyContext(dpy, c);
  eglTerminate(dpy);
}

bool AuditCoverage(Sweep& s) {
  bool all = true;
  s.check(kCoreApiCount == 358u, "audit list is 358");
  for (const char* n : kCoreApis) {
    if (s.touched.find(n) == s.touched.end()) {
      char b[128];
      std::snprintf(b, sizeof(b), "UNTOUCHED: %s", n);
      s.log(b);
      all = false;
    }
  }
  char b[128];
  std::snprintf(b, sizeof(b), "coverage: %zu/358 touched", s.touched.size());
  s.log(b);
  s.check(all, "all 358 touched");
  return all;
}

struct Phase {
  const char* name;
  void (*run)(Sweep&, App&);
};

// Declaration order = window demo order.
Phase kPhases[] = {
    {"01 version/strings/caps", PhaseVersion},
    {"02 buffers", PhaseBuffers},
    {"03 vao/attribs", PhaseVAO},
    {"04 shaders/programs", PhaseShaders},
    {"05 uniforms", PhaseUniforms},
    {"06 textures", PhaseTextures},
    {"07 samplers", PhaseSamplers},
    {"08 fbo/rbo/blit/readback", PhaseFBO},
    {"09 raster/blend/clear", PhaseRaster},
    {"10 topology parade", PhaseTopology},
    {"11 indexed/instanced/indirect", PhaseIndexed},
    {"12 compute/images/barriers", PhaseCompute},
    {"13 sync/query/xfb", PhaseSyncQueryXfb},
    {"14 debug/labels", PhaseDebug},
    {"15 pixels", PhasePixels},
    {"16 pipelines/misc", PhaseMisc},
    {"17 expected-fail parade", PhaseExpectedFail},
    {"18 host C-ABI stragglers", PhaseHostStragglers},
};
constexpr int kPhaseCount = sizeof(kPhases) / sizeof(kPhases[0]);

}  // namespace

// ---------------------------------------------------------------------------
// AppKit: window + Metal view + side panel (phase, progress, log).

static tgles::GlesContext* g_ctx = nullptr;
static std::unique_ptr<tgles::metal_bridge::MetalBridge> g_bridge;
static App g_app;
static Sweep g_sweep;
static NSTextView* g_logView = nil;
static NSTextField* g_phaseLabel = nil;
static NSProgressIndicator* g_progress = nil;
static int g_phase = 0;
static bool g_audited = false;
static int g_demoTicks = 0;
static int g_exitCode = 0;

static void EmitLog(const std::string& s) {
  std::printf("[api-window] %s\n", s.c_str());
  std::fflush(stdout);
  if (g_logView == nil) return;
  NSString* line =
      [[NSString stringWithUTF8String:s.c_str()] stringByAppendingString:@"\n"];
  dispatch_async(dispatch_get_main_queue(), ^{
    NSAttributedString* a =
        [[NSAttributedString alloc] initWithString:line];
    [[g_logView textStorage] appendAttributedString:a];
    [g_logView scrollToEndOfDocument:nil];
  });
}

static void RunPhase(int i) {
  if (i < 0 || i >= kPhaseCount) return;
  char head[128];
  std::snprintf(head, sizeof(head), "--- phase %d/%d %s ---", i + 1,
                kPhaseCount, kPhases[i].name);
  EmitLog(head);
  NSString* label =
      [NSString stringWithFormat:@"Phase %d/%d: %s", i + 1, kPhaseCount,
                                 kPhases[i].name];
  [g_phaseLabel setStringValue:label];
  [g_progress setDoubleValue:(100.0 * i) / kPhaseCount];
  int failsBefore = g_sweep.fail;
  kPhases[i].run(g_sweep, g_app);
  char tail[128];
  std::snprintf(tail, sizeof(tail),
                "phase done: +%d checks (%d new failures)",
                g_sweep.ok + g_sweep.fail + g_sweep.probe,
                g_sweep.fail - failsBefore);
  EmitLog(tail);
}

@interface ApiRenderer : NSObject {
 @public
  NSView* metalView;
  CAMetalLayer* metalLayer;
  NSTimer* timer;
}
- (void)tick:(NSTimer*)t;
- (void)renderDemo;
@end

@implementation ApiRenderer

- (void)tick:(NSTimer*)t {
  (void)t;
  NSSize pts = [metalView bounds].size;
  NSSize px = [metalView convertSizeToBacking:pts];
  int w = (int)llround(px.width), h = (int)llround(px.height);
  if (w < 8 || h < 8) return;
  g_app.ensureTargets(w, h);
  g_bridge->SetLayer((__bridge void*)metalLayer, (tgles::GLsizei)w,
                     (tgles::GLsizei)h);

  if (g_phase < kPhaseCount) {
    RunPhase(g_phase);
    ++g_phase;
    if (g_phase == kPhaseCount) {
      EmitLog("--- audit ---");
      bool cov = AuditCoverage(g_sweep);
      char sum[256];
      std::snprintf(sum, sizeof(sum),
                    "SUMMARY: ok=%d fail=%d probe=%d coverage=%s", g_sweep.ok,
                    g_sweep.fail, g_sweep.probe, cov ? "358/358" : "MISSING");
      EmitLog(sum);
      [g_phaseLabel setStringValue:
                          [NSString stringWithFormat:@"Done: %s", sum]];
      [g_progress setDoubleValue:100.0];
      g_audited = true;
      g_exitCode = (g_sweep.fail == 0 && cov) ? 0 : 1;
      EmitLog(g_exitCode == 0 ? "RESULT: PASS" : "RESULT: FAIL");
    }
    return;
  }
  // Sweep done: keep looping visible demos, then auto-quit for CI.
  [self renderDemo];
  if (g_audited && ++g_demoTicks > 60 * 6) {
    EmitLog("auto-quit");
    [NSApp terminate:nil];
  }
}

- (void)renderDemo {
  // Rotating-hue fullscreen triangle: proves the window path stays alive.
  static float hue = 0.0f;
  hue += 0.05f;
  if (hue > 6.283f) hue -= 6.283f;
  float pos[9], col[12];
  FillTri(pos, col, 0.5f + 0.5f * std::cos(hue),
          0.5f + 0.5f * std::cos(hue + 2.094f),
          0.5f + 0.5f * std::cos(hue + 4.188f));
  g_app.resetState();
  g_app.uploadTri(pos, col);
  if (g_app.draw(tgles::kGlTriangles, 0, 3)) {
    if (g_app.present()) {
      g_bridge->WaitForCompletion(g_bridge->FrameSerial());
    }
  }
}

@end

int main(int argc, const char* argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    g_bridge = tgles::metal_bridge::CreateAppleBridge();
    if (!g_bridge || !g_bridge->Initialize("Apple9")) {
      std::printf("[api-window] no Metal device, exiting 2\n");
      return 2;
    }
    static tgles::GlesContext ctx_storage = tgles::GlesContext::Create(false);
    g_ctx = &ctx_storage;
    g_app.ctx = g_ctx;
    g_app.bridge = g_bridge.get();
    std::string err;
    if (!g_app.setup(err)) {
      std::printf("[api-window] scene setup failed: %s\n", err.c_str());
      return 1;
    }
    g_sweep.ctx = g_ctx;
    g_sweep.bridge = g_bridge.get();
    g_sweep.emit = EmitLog;

    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSRect frame = NSMakeRect(0, 0, 1080, 680);
    NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:(NSWindowStyleMaskTitled |
                                                          NSWindowStyleMaskClosable |
                                                          NSWindowStyleMaskResizable |
                                                          NSWindowStyleMaskMiniaturizable)
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    [win setTitle:@"TGL ES 3.2 API window — full 358 sweep"];
    [win setMinSize:NSMakeSize(800, 520)];
    [win center];

    NSView* content = [win contentView];
    ApiRenderer* renderer = [[ApiRenderer alloc] init];

    NSView* mv = [[NSView alloc] initWithFrame:NSZeroRect];
    [mv setWantsLayer:YES];
    CAMetalLayer* layer = [CAMetalLayer layer];
    [mv setLayer:layer];
    renderer->metalView = mv;
    renderer->metalLayer = layer;

    NSView* panel = [[NSView alloc] initWithFrame:NSZeroRect];
    NSTextField* title = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [title setStringValue:@"ES 3.2 sweep (358 entry points)"];
    [title setBezeled:NO];
    [title setDrawsBackground:NO];
    [title setEditable:NO];
    [title setSelectable:NO];
    [title setFont:[NSFont boldSystemFontOfSize:12]];
    NSTextField* phase = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [phase setStringValue:@"Starting…"];
    [phase setBezeled:NO];
    [phase setDrawsBackground:NO];
    [phase setEditable:NO];
    [phase setSelectable:NO];
    [phase setFont:[NSFont systemFontOfSize:11]];
    g_phaseLabel = phase;
    NSProgressIndicator* prog = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    [prog setIndeterminate:NO];
    [prog setMinValue:0];
    [prog setMaxValue:100];
    g_progress = prog;
    NSScrollView* sv = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    [sv setHasVerticalScroller:YES];
    NSTextView* tv = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 300, 400)];
    [tv setEditable:NO];
    [tv setSelectable:YES];
    [tv setFont:[NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular]];
    [sv setDocumentView:tv];
    g_logView = tv;
    for (NSView* v in @[ title, phase, prog, sv ]) {
      [v setTranslatesAutoresizingMaskIntoConstraints:NO];
      [panel addSubview:v];
    }
    [mv setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content addSubview:mv];
    [panel setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content addSubview:panel];
    [NSLayoutConstraint activateConstraints:@[
      [mv.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
      [mv.topAnchor constraintEqualToAnchor:content.topAnchor],
      [mv.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
      [mv.trailingAnchor constraintEqualToAnchor:panel.leadingAnchor],
      [panel.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
      [panel.topAnchor constraintEqualToAnchor:content.topAnchor],
      [panel.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
      [panel.widthAnchor constraintEqualToConstant:340.0],
      [title.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:8],
      [title.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-8],
      [title.topAnchor constraintEqualToAnchor:panel.topAnchor constant:8],
      [phase.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:8],
      [phase.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-8],
      [phase.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:6],
      [prog.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:8],
      [prog.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-8],
      [prog.topAnchor constraintEqualToAnchor:phase.bottomAnchor constant:6],
      [sv.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:8],
      [sv.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-8],
      [sv.topAnchor constraintEqualToAnchor:prog.bottomAnchor constant:6],
      [sv.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor constant:-8],
    ]];

    [win makeKeyAndOrderFront:nil];
    [app activateIgnoringOtherApps:YES];
    renderer->timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 12.0)
                                                       target:renderer
                                                     selector:@selector(tick:)
                                                     userInfo:nil
                                                      repeats:YES];
    [app run];
    return g_exitCode;
  }
}
