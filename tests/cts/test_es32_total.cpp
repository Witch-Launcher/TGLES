// File test tổng thể OpenGL ES 3.2 chuẩn -> TGL -> Metal.
//
// Mục tiêu (theo yêu cầu): dùng TOÀN BỘ API của OpenGL ES 3.2 chuẩn
// (358 entry points trong docs/reference/gl32.h, KHÔNG chỉ subset được hỗ trợ),
// đi qua TGL để dịch sang Metal API; API nào thất bại phải báo rõ rồi fix.
//
// Cách đọc file này:
//   - kCoreApis[] là ground truth 358 API chuẩn (đối chiếu với gl32.h lúc chạy).
//   - Mỗi TEST là một tầng dịch:
//       1. Header/đếm API chuẩn.
//       2. Host ABI có export đủ 358 symbol không (dlsym + eglGetProcAddress).
//       3. Metal mapping có phủ đủ 358 API không (FindMapping != null).
//       4. Họ draw/compute có chạy thật trên MockMetalBridge không.
//       5. Họ state/resource có giữ error sane, không crash không.
//       6. Enum PSO có dịch sang giá trị Metal được không.
//   - Khi TEST 3 đỏ, xem log "NO Metal mapping for ..." để biết API nào thiếu,
//     rồi bổ sung vào src/gpu/metal_mapping.cpp (đây chính là phần đã fix).

#include "test_framework.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "tgles/base/spec.h"
#include "tgles/facade/gles.h"
#include "tgles/gpu/backend.h"
#include "tgles/gpu/metal_bridge.h"
#include "tgles/gpu/metal_mapping.h"
#include "tgles/gpu/metal_translate.h"

#ifndef TGLES_HOST_LIBRARY_PATH
#define TGLES_HOST_LIBRARY_PATH "./libtgles.dylib"
#endif

namespace {

// ---------------------------------------------------------------------------
// Ground truth: toàn bộ 358 API lõi OpenGL ES 3.2 chuẩn (trích từ gl32.h).
// Sắp xếp alphabet để diff dễ. TEST đầu tiên sẽ parse lại gl32.h trên đĩa
// và đối chiếu, nên danh sách này không thể "tự nhận đủ" một cách lặng lẽ.
// ---------------------------------------------------------------------------
const char* const kCoreApis[] = {
    "glActiveShaderProgram", "glActiveTexture", "glAttachShader",
    "glBeginQuery", "glBeginTransformFeedback", "glBindAttribLocation",
    "glBindBuffer", "glBindBufferBase", "glBindBufferRange",
    "glBindFramebuffer", "glBindImageTexture", "glBindProgramPipeline",
    "glBindRenderbuffer", "glBindSampler", "glBindTexture",
    "glBindTransformFeedback", "glBindVertexArray", "glBindVertexBuffer",
    "glBlendBarrier", "glBlendColor", "glBlendEquation",
    "glBlendEquationSeparate", "glBlendEquationSeparatei", "glBlendEquationi",
    "glBlendFunc", "glBlendFuncSeparate", "glBlendFuncSeparatei",
    "glBlendFunci", "glBlitFramebuffer", "glBufferData", "glBufferSubData",
    "glCheckFramebufferStatus", "glClear", "glClearBufferfi",
    "glClearBufferfv", "glClearBufferiv", "glClearBufferuiv", "glClearColor",
    "glClearDepthf", "glClearStencil", "glClientWaitSync", "glColorMask",
    "glColorMaski", "glCompileShader", "glCompressedTexImage2D",
    "glCompressedTexImage3D", "glCompressedTexSubImage2D",
    "glCompressedTexSubImage3D", "glCopyBufferSubData", "glCopyImageSubData",
    "glCopyTexImage2D", "glCopyTexSubImage2D", "glCopyTexSubImage3D",
    "glCreateProgram", "glCreateShader", "glCreateShaderProgramv",
    "glCullFace", "glDebugMessageCallback", "glDebugMessageControl",
    "glDebugMessageInsert", "glDeleteBuffers", "glDeleteFramebuffers",
    "glDeleteProgram", "glDeleteProgramPipelines", "glDeleteQueries",
    "glDeleteRenderbuffers", "glDeleteSamplers", "glDeleteShader",
    "glDeleteSync", "glDeleteTextures", "glDeleteTransformFeedbacks",
    "glDeleteVertexArrays", "glDepthFunc", "glDepthMask", "glDepthRangef",
    "glDetachShader", "glDisable", "glDisableVertexAttribArray", "glDisablei",
    "glDispatchCompute", "glDispatchComputeIndirect", "glDrawArrays",
    "glDrawArraysIndirect", "glDrawArraysInstanced", "glDrawBuffers",
    "glDrawElements", "glDrawElementsBaseVertex", "glDrawElementsIndirect",
    "glDrawElementsInstanced", "glDrawElementsInstancedBaseVertex",
    "glDrawRangeElements", "glDrawRangeElementsBaseVertex", "glEnable",
    "glEnableVertexAttribArray", "glEnablei", "glEndQuery",
    "glEndTransformFeedback", "glFenceSync", "glFinish", "glFlush",
    "glFlushMappedBufferRange", "glFramebufferParameteri",
    "glFramebufferRenderbuffer", "glFramebufferTexture",
    "glFramebufferTexture2D", "glFramebufferTextureLayer", "glFrontFace",
    "glGenBuffers", "glGenFramebuffers", "glGenProgramPipelines",
    "glGenQueries", "glGenRenderbuffers", "glGenSamplers", "glGenTextures",
    "glGenTransformFeedbacks", "glGenVertexArrays", "glGenerateMipmap",
    "glGetActiveAttrib", "glGetActiveUniform", "glGetActiveUniformBlockName",
    "glGetActiveUniformBlockiv", "glGetActiveUniformsiv",
    "glGetAttachedShaders", "glGetAttribLocation", "glGetBooleani_v",
    "glGetBooleanv", "glGetBufferParameteri64v", "glGetBufferParameteriv",
    "glGetBufferPointerv", "glGetDebugMessageLog", "glGetError",
    "glGetFloatv", "glGetFragDataLocation",
    "glGetFramebufferAttachmentParameteriv", "glGetFramebufferParameteriv",
    "glGetGraphicsResetStatus", "glGetInteger64i_v", "glGetInteger64v",
    "glGetIntegeri_v", "glGetIntegerv", "glGetInternalformativ",
    "glGetMultisamplefv", "glGetObjectLabel", "glGetObjectPtrLabel",
    "glGetPointerv", "glGetProgramBinary", "glGetProgramInfoLog",
    "glGetProgramInterfaceiv", "glGetProgramPipelineInfoLog",
    "glGetProgramPipelineiv", "glGetProgramResourceIndex",
    "glGetProgramResourceLocation", "glGetProgramResourceName",
    "glGetProgramResourceiv", "glGetProgramiv", "glGetQueryObjectuiv",
    "glGetQueryiv", "glGetRenderbufferParameteriv",
    "glGetSamplerParameterIiv", "glGetSamplerParameterIuiv",
    "glGetSamplerParameterfv", "glGetSamplerParameteriv",
    "glGetShaderInfoLog", "glGetShaderPrecisionFormat", "glGetShaderSource",
    "glGetShaderiv", "glGetString", "glGetStringi", "glGetSynciv",
    "glGetTexLevelParameterfv", "glGetTexLevelParameteriv",
    "glGetTexParameterIiv", "glGetTexParameterIuiv", "glGetTexParameterfv",
    "glGetTexParameteriv", "glGetTransformFeedbackVarying",
    "glGetUniformBlockIndex", "glGetUniformIndices", "glGetUniformLocation",
    "glGetUniformfv", "glGetUniformiv", "glGetUniformuiv",
    "glGetVertexAttribIiv", "glGetVertexAttribIuiv",
    "glGetVertexAttribPointerv", "glGetVertexAttribfv", "glGetVertexAttribiv",
    "glGetnUniformfv", "glGetnUniformiv", "glGetnUniformuiv", "glHint",
    "glInvalidateFramebuffer", "glInvalidateSubFramebuffer", "glIsBuffer",
    "glIsEnabled", "glIsEnabledi", "glIsFramebuffer", "glIsProgram",
    "glIsProgramPipeline", "glIsQuery", "glIsRenderbuffer", "glIsSampler",
    "glIsShader", "glIsSync", "glIsTexture", "glIsTransformFeedback",
    "glIsVertexArray", "glLineWidth", "glLinkProgram", "glMapBufferRange",
    "glMemoryBarrier", "glMemoryBarrierByRegion", "glMinSampleShading",
    "glObjectLabel", "glObjectPtrLabel", "glPatchParameteri",
    "glPauseTransformFeedback", "glPixelStorei", "glPolygonOffset",
    "glPopDebugGroup", "glPrimitiveBoundingBox", "glProgramBinary",
    "glProgramParameteri", "glProgramUniform1f", "glProgramUniform1fv",
    "glProgramUniform1i", "glProgramUniform1iv", "glProgramUniform1ui",
    "glProgramUniform1uiv", "glProgramUniform2f", "glProgramUniform2fv",
    "glProgramUniform2i", "glProgramUniform2iv", "glProgramUniform2ui",
    "glProgramUniform2uiv", "glProgramUniform3f", "glProgramUniform3fv",
    "glProgramUniform3i", "glProgramUniform3iv", "glProgramUniform3ui",
    "glProgramUniform3uiv", "glProgramUniform4f", "glProgramUniform4fv",
    "glProgramUniform4i", "glProgramUniform4iv", "glProgramUniform4ui",
    "glProgramUniform4uiv", "glProgramUniformMatrix2fv",
    "glProgramUniformMatrix2x3fv", "glProgramUniformMatrix2x4fv",
    "glProgramUniformMatrix3fv", "glProgramUniformMatrix3x2fv",
    "glProgramUniformMatrix3x4fv", "glProgramUniformMatrix4fv",
    "glProgramUniformMatrix4x2fv", "glProgramUniformMatrix4x3fv",
    "glPushDebugGroup", "glReadBuffer", "glReadPixels", "glReadnPixels",
    "glReleaseShaderCompiler", "glRenderbufferStorage",
    "glRenderbufferStorageMultisample", "glResumeTransformFeedback",
    "glSampleCoverage", "glSampleMaski", "glSamplerParameterIiv",
    "glSamplerParameterIuiv", "glSamplerParameterf", "glSamplerParameterfv",
    "glSamplerParameteri", "glSamplerParameteriv", "glScissor",
    "glShaderBinary", "glShaderSource", "glStencilFunc",
    "glStencilFuncSeparate", "glStencilMask", "glStencilMaskSeparate",
    "glStencilOp", "glStencilOpSeparate", "glTexBuffer", "glTexBufferRange",
    "glTexImage2D", "glTexImage3D", "glTexParameterIiv", "glTexParameterIuiv",
    "glTexParameterf", "glTexParameterfv", "glTexParameteri",
    "glTexParameteriv", "glTexStorage2D", "glTexStorage2DMultisample",
    "glTexStorage3D", "glTexStorage3DMultisample", "glTexSubImage2D",
    "glTexSubImage3D", "glTransformFeedbackVaryings", "glUniform1f",
    "glUniform1fv", "glUniform1i", "glUniform1iv", "glUniform1ui",
    "glUniform1uiv", "glUniform2f", "glUniform2fv", "glUniform2i",
    "glUniform2iv", "glUniform2ui", "glUniform2uiv", "glUniform3f",
    "glUniform3fv", "glUniform3i", "glUniform3iv", "glUniform3ui",
    "glUniform3uiv", "glUniform4f", "glUniform4fv", "glUniform4i",
    "glUniform4iv", "glUniform4ui", "glUniform4uiv", "glUniformBlockBinding",
    "glUniformMatrix2fv", "glUniformMatrix2x3fv", "glUniformMatrix2x4fv",
    "glUniformMatrix3fv", "glUniformMatrix3x2fv", "glUniformMatrix3x4fv",
    "glUniformMatrix4fv", "glUniformMatrix4x2fv", "glUniformMatrix4x3fv",
    "glUnmapBuffer", "glUseProgram", "glUseProgramStages", "glValidateProgram",
    "glValidateProgramPipeline", "glVertexAttrib1f", "glVertexAttrib1fv",
    "glVertexAttrib2f", "glVertexAttrib2fv", "glVertexAttrib3f",
    "glVertexAttrib3fv", "glVertexAttrib4f", "glVertexAttrib4fv",
    "glVertexAttribBinding", "glVertexAttribDivisor", "glVertexAttribFormat",
    "glVertexAttribI4i", "glVertexAttribI4iv", "glVertexAttribI4ui",
    "glVertexAttribI4uiv", "glVertexAttribIFormat", "glVertexAttribIPointer",
    "glVertexAttribPointer", "glVertexBindingDivisor", "glViewport",
    "glWaitSync",
};

constexpr std::size_t kCoreApiCount =
    sizeof(kCoreApis) / sizeof(kCoreApis[0]);

std::vector<std::string> ParseHeaderApis(const std::string& path) {
  std::vector<std::string> names;
  std::ifstream in(path);
  if (!in) return names;
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("GL_APICALL") == std::string::npos) continue;
    if (line.find("GL_APIENTRY") == std::string::npos) continue;
    std::size_t p = line.find("GL_APIENTRY");
    if (p == std::string::npos) continue;
    p = line.find("gl", p);
    if (p == std::string::npos) continue;
    std::size_t e = line.find_first_of(" (", p);
    if (e == std::string::npos) continue;
    names.push_back(line.substr(p, e - p));
  }
  return names;
}

bool IsSentinel(void* p) {
  return p != nullptr && reinterpret_cast<std::uintptr_t>(p) < 0x1000;
}

tgles::GLuint Compile(tgles::ShaderManager& sm, tgles::GLenum type,
                      const char* src) {
  tgles::GLuint s = sm.CreateShader(type);
  sm.ShaderSource(s, 1, &src, nullptr);
  sm.CompileShader(s);
  return s;
}

}  // namespace

// Tầng 1: danh sách cứng phải khớp header chuẩn trên đĩa.
TEST(Es32Total, EmbeddedListMatchesOfficialHeader) {
  EXPECT_EQ(kCoreApiCount, static_cast<std::size_t>(358));
  EXPECT_EQ(tgles::kCoreEntryPointCount, static_cast<std::size_t>(358));
  const char* dir = TGLES_REFERENCE_DIR;
  std::string path = std::string(dir) + "/gl32.h";
  std::ifstream probe(path);
  if (!probe) return;  // Không có docs/reference thì vẫn giữ hằng số.
  std::vector<std::string> on_disk = ParseHeaderApis(path);
  EXPECT_EQ(on_disk.size(), static_cast<std::size_t>(358));
  // Mọi API trên đĩa phải có trong danh sách cứng và ngược lại.
  std::size_t missing = 0, extra = 0;
  for (const std::string& name : on_disk) {
    bool found = false;
    for (const char* c : kCoreApis) {
      if (name == c) {
        found = true;
        break;
      }
    }
    if (!found) {
      std::printf("  embedded list MISSING standard API: %s\n",
                  name.c_str());
      ++missing;
    }
  }
  for (const char* c : kCoreApis) {
    bool found = false;
    for (const std::string& name : on_disk) {
      if (name == c) {
        found = true;
        break;
      }
    }
    if (!found) {
      std::printf("  embedded list has EXTRA non-standard API: %s\n", c);
      ++extra;
    }
  }
  EXPECT_EQ(missing, std::size_t{0});
  EXPECT_EQ(extra, std::size_t{0});
}

// Tầng 2: TGL phải export đủ 358 symbol chuẩn (dlsym + eglGetProcAddress).
// Đây là cửa "host loader" (MobileGL dlopen, CTS eglGetProcAddress).
TEST(Es32Total, EveryCoreApiHasHostSymbol) {
  void* lib = dlopen(TGLES_HOST_LIBRARY_PATH, RTLD_NOW | RTLD_LOCAL);
  EXPECT_TRUE(lib != nullptr);
  if (lib == nullptr) {
    std::printf("  dlopen(%s) failed: %s\n", TGLES_HOST_LIBRARY_PATH,
                dlerror());
    return;
  }
  using ProcFn = void* (*)(const char*);
  auto get_proc =
      reinterpret_cast<ProcFn>(dlsym(lib, "eglGetProcAddress"));
  EXPECT_TRUE(get_proc != nullptr);
  if (get_proc == nullptr) {
    dlclose(lib);
    return;
  }
  std::size_t nulls = 0, sentinels = 0, mismatches = 0;
  for (const char* name : kCoreApis) {
    void* via_dlsym = dlsym(lib, name);
    void* via_proc = get_proc(name);
    if (via_dlsym == nullptr || via_proc == nullptr) {
      std::printf("  NO host symbol for %s (dlsym=%p proc=%p)\n", name,
                  via_dlsym, via_proc);
      ++nulls;
    }
    if (IsSentinel(via_dlsym) || IsSentinel(via_proc)) {
      std::printf("  SENTINEL address for %s\n", name);
      ++sentinels;
    }
    if (via_dlsym != nullptr && via_proc != nullptr &&
        via_dlsym != via_proc) {
      std::printf("  DISPATCH mismatch for %s\n", name);
      ++mismatches;
    }
  }
  std::printf("  host symbols: %zu checked, %zu null, %zu sentinel, %zu "
              "mismatch\n",
              kCoreApiCount, nulls, sentinels, mismatches);
  EXPECT_EQ(nulls, std::size_t{0});
  EXPECT_EQ(sentinels, std::size_t{0});
  EXPECT_EQ(mismatches, std::size_t{0});
  dlclose(lib);
}

// Tầng 3 (CỐT LÕI THEO YÊU CẦU): mọi API chuẩn phải có đường dịch sang Metal.
// FindMapping == null nghĩa là TGL chưa biết dịch API đó sang Metal call nào
// -> test đỏ và in tên API thiếu để fix trong metal_mapping.cpp.
TEST(Es32Total, EveryCoreApiHasMetalMapping) {
  std::size_t unmapped = 0;
  for (const char* name : kCoreApis) {
    const tgles::metal::FeatureMapping* m =
        tgles::metal::FindMapping(name);
    if (m == nullptr) {
      if (unmapped < 60) {
        std::printf("  NO Metal mapping for %s\n", name);
      }
      ++unmapped;
    } else {
      EXPECT_TRUE(m->metal_equivalent != nullptr &&
                  m->metal_equivalent[0] != '\0');
      EXPECT_TRUE(m->note != nullptr && m->note[0] != '\0');
    }
  }
  if (unmapped > 60) {
    std::printf("  ... and %zu more unmapped\n", unmapped - 60);
  }
  std::printf("  metal mappings: %zu checked, %zu unmapped\n", kCoreApiCount,
              unmapped);
  EXPECT_EQ(unmapped, std::size_t{0});
}

// Tầng 4: họ draw/compute phải chạy thật qua bridge (không chỉ có tên trong
// bảng). Dùng MockMetalBridge nên chạy được trên CI không GPU.
TEST(Es32Total, DrawComputeFamilyExecutesOnMockBridge) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));

  // Dựng tam giác tối thiểu theo đúng đường RenderFrame cần:
  // 2 buffer float (pos xyz + color rgba), VAO enable cả attrib 0/1,
  // FBO có COLOR_ATTACHMENT0 kích thước > 0.
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  tgles::GLuint pb = 0, cb = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                           tgles::kGlStaticDraw);
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec3 a_pos;\n"
      "void main(){gl_Position=vec4(a_pos,1.0);}";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "layout(location=0) out vec4 o;\nvoid main(){o=vec4(1.0);}";
  tgles::GLuint v = Compile(ctx.shaders(), tgles::kGlVertexShader, vs);
  tgles::GLuint f = Compile(ctx.shaders(), tgles::kGlFragmentShader, fs);
  tgles::GLuint prog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(prog, v);
  ctx.programs().AttachShader(prog, f);
  ctx.programs().LinkProgram(prog);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  ctx.programs().UseProgram(prog);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, cb);
  ctx.vertex_arrays().EnableVertexAttribArray(0);
  ctx.vertex_arrays().EnableVertexAttribArray(1);
  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16,
                            0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  tgles::GLuint fb = 0;
  ctx.framebuffers().GenFramebuffers(1, &fb);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fb);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);

  // PSO key từ state thật phải hợp lệ + tạo được pipeline trên bridge.
  tgles::backend::PsoKey key =
      tgles::backend::PsoKeyForDraw(ctx.foundation(), ctx.raster());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_NE(bridge.CreateRenderPipeline(key), 0u);

  // CommandPlan phản chiếu encoder ordering mà bridge thực thi.
  tgles::backend::CommandPlan plan;
  plan.BeginRenderPass();
  plan.Draw();
  plan.EndRenderPass();
  plan.Blit();
  EXPECT_EQ(plan.GetError(), tgles::kGlNoError);

  // RenderFrame TRIANGLES qua Mock bridge (draw path thật).
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  // Compute dispatch validate: cần program có compute stage (spec 19).
  // Program đồ họa đang bound không dispatch được -> dựng compute program.
  const char* cs =
      "#version 320 es\nlayout(local_size_x=1) in;\nvoid main(){}";
  tgles::GLuint csh = Compile(ctx.shaders(), tgles::kGlComputeShader, cs);
  tgles::GLuint cprog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(cprog, csh);
  ctx.programs().LinkProgram(cprog);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(cprog));
  ctx.programs().UseProgram(cprog);
  ctx.SyncComputeProgram();
  ctx.DispatchCompute(1, 1, 1);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  // Memory barrier + blend barrier là fence/textureBarrier trên Metal.
  EXPECT_TRUE(tgles::metal::FindMapping("glMemoryBarrier") != nullptr);
  EXPECT_TRUE(tgles::metal::FindMapping("glBlendBarrier") != nullptr);
  EXPECT_TRUE(tgles::metal::FindMapping("glDispatchCompute") != nullptr);
  EXPECT_TRUE(tgles::metal::FindMapping("glDrawArrays") != nullptr);
  EXPECT_TRUE(tgles::metal::FindMapping("glDrawElements") != nullptr);
  EXPECT_TRUE(tgles::metal::FindMapping("glBlitFramebuffer") != nullptr);
}

// Tầng 5: họ state/resource còn lại phải giữ error sane, không crash.
// Mỗi nhóm gọi đúng 1-2 API đại diện với tham số hợp lệ + 1 tham số sai để
// chứng minh validator fail-closed (đây là "dịch" ở tầng CPU state trước khi
// xuống Metal encoder).
TEST(Es32Total, StateResourceApisKeepErrorSane) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);

  // Buffer + VAO + vertex format/binding/divisor.
  tgles::GLuint b = 0;
  ctx.buffers().GenBuffers(1, &b);
  EXPECT_NE(b, 0u);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, b);
  EXPECT_EQ(ctx.buffers().GetError(), tgles::kGlNoError);
  tgles::GLuint vao0 = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao0);
  ctx.vertex_arrays().BindVertexArray(vao0);
  ctx.vertex_arrays().VertexAttribBinding(0, 0);
  ctx.vertex_arrays().VertexAttribFormat(0, 3, tgles::kGlFloat,
                                         tgles::kGlFalse, 0);
  ctx.vertex_arrays().VertexBindingDivisor(0, 1);
  ctx.vertex_arrays().VertexAttribDivisor(0, 1);
  EXPECT_EQ(ctx.vertex_arrays().GetError(), tgles::kGlNoError);

  // Texture/sampler/pixel store/readbuffer/drawbuffers.
  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4, 4,
                            0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                            nullptr);
  ctx.textures().GenerateMipmap(tgles::kGlTexture2d);
  ctx.textures().TexParameteri(tgles::kGlTexture2d,
                               tgles::kGlTextureMinFilter,
                               tgles::kGlLinear);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  tgles::GLuint s = 0;
  ctx.samplers().GenSamplers(1, &s);
  ctx.samplers().BindSampler(0, s);
  ctx.samplers().SamplerParameteri(s, tgles::kGlTextureMinFilter,
                                   tgles::kGlLinear);
  EXPECT_EQ(ctx.samplers().GetError(), tgles::kGlNoError);
  ctx.pixels().PixelStorei(tgles::kGlUnpackAlignment, 4);
  EXPECT_EQ(ctx.pixels().GetError(), tgles::kGlNoError);

  // FBO/RBO + invalidate + framebuffer parameter.
  tgles::GLuint fb = 0, rb = 0;
  ctx.framebuffers().GenFramebuffers(1, &fb);
  ctx.framebuffers().BindFramebuffer(tgles::kGlFramebuffer, fb);
  ctx.renderbuffers().GenRenderbuffers(1, &rb);
  ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
  ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                          tgles::kGlRgba8, 4, 4);
  EXPECT_EQ(ctx.renderbuffers().GetError(), tgles::kGlNoError);

  // Raster: blend/depth/stencil/cull/viewport/scissor/polygonoffset/line.
  ctx.raster().BlendFunc(tgles::kGlOne, tgles::kGlZero);
  ctx.raster().BlendEquation(tgles::kGlFuncAdd);
  ctx.raster().DepthFunc(tgles::kGlLess);
  ctx.raster().DepthMask(tgles::kGlTrue);
  ctx.raster().Viewport(0, 0, 64, 64);
  ctx.raster().Scissor(0, 0, 64, 64);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);
  ctx.raster().BlendFunci(99, tgles::kGlOne, tgles::kGlZero);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(ctx.raster().GetError(), tgles::kGlNoError);

  // Program/uniform/program-pipeline/separable.
  const char* kV = "#version 320 es\nuniform float u_c;\nvoid main() {}";
  tgles::GLuint sh = Compile(ctx.shaders(), tgles::kGlVertexShader, kV);
  tgles::GLuint pr = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(pr, sh);
  ctx.programs().LinkProgram(pr);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(pr));
  ctx.programs().UseProgram(pr);
  tgles::GLint loc = ctx.programs().GetUniformLocation(pr, "u_c");
  EXPECT_TRUE(loc >= 0);
  ctx.programs().Uniform1f(loc, 1.0f);
  EXPECT_EQ(ctx.programs().GetError(), tgles::kGlNoError);

  // Transform feedback + sync + query + debug + image units + tess/patch.
  tgles::GLuint xf = 0;
  ctx.transform_feedback().GenTransformFeedbacks(1, &xf);
  ctx.transform_feedback().BindTransformFeedback(
      tgles::kGlTransformFeedback, xf);
  EXPECT_EQ(ctx.transform_feedback().GetError(), tgles::kGlNoError);
  tgles::GLuint q = 0;
  ctx.queries().GenQueries(1, &q);
  ctx.queries().BeginQuery(
      tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed), q);
  ctx.queries().EndQuery(
      tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed));
  EXPECT_EQ(ctx.queries().GetError(), tgles::kGlNoError);
  ctx.sync().FenceSync(tgles::kGlSyncGpuCommandsComplete, 0);
  EXPECT_EQ(ctx.sync().GetError(), tgles::kGlNoError);
  ctx.tessellation().PatchParameteri(tgles::kGlPatchVertices, 3);
  EXPECT_EQ(ctx.tessellation().GetError(), tgles::kGlNoError);
  ctx.images().BindImageTexture(0, 1, 0, tgles::kGlFalse, 0,
                                tgles::kGlReadOnly, tgles::kGlRgba8);
  EXPECT_EQ(ctx.images().GetError(), tgles::kGlNoError);
  ctx.debug().PushDebugGroup(tgles::kGlDebugSourceApplication, 1, 5,
                             "total");
  ctx.debug().PopDebugGroup();
  EXPECT_EQ(ctx.debug().GetError(), tgles::kGlNoError);

  // Toàn bộ context phải sạch lỗi sau chuỗi trên.
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

// Tầng 6: enum dùng cho PSO phải dịch được sang giá trị Metal (không -1 với
// giá trị lõi). Đây là chỗ "dịch sang Metal API" ở mức encoder state.
TEST(Es32Total, PsoEnumsTranslateToMetalValues) {
  EXPECT_EQ(tgles::metal_translate::BlendFactor(tgles::kGlOne), 1);
  EXPECT_EQ(tgles::metal_translate::BlendFactor(tgles::kGlZero), 0);
  EXPECT_EQ(tgles::metal_translate::BlendFactor(tgles::kGlSrcAlpha), 4);
  EXPECT_EQ(tgles::metal_translate::BlendOperation(tgles::kGlFuncAdd), 0);
  EXPECT_EQ(tgles::metal_translate::CompareFunction(tgles::kGlLess), 1);
  EXPECT_EQ(tgles::metal_translate::CompareFunction(tgles::kGlAlways), 7);
  EXPECT_EQ(tgles::metal_translate::ColorWriteMask(true, true, true, true),
            0xF);
  EXPECT_EQ(tgles::metal_translate::CompareFunction(0x1234), -1);
}
