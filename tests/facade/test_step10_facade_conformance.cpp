// Step 10 tests: facade wiring, unified error polling and the conformance
// checklist that locks every minimum and every plan correction in place.

#include "test_framework.h"

#include <string>

#include "tgles/facade/gles.h"

TEST(Step10, FreshContextPassesChecklist) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  std::vector<std::string> failures = ctx.ConformanceChecklist();
  for (const auto& f : failures) {
    std::printf("  checklist failure: %s\n", f.c_str());
  }
  EXPECT_TRUE(failures.empty());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step10, VersionReportMentions32) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  std::string report = ctx.VersionReport();
  EXPECT_TRUE(report.find("OpenGL ES 3.2") != std::string::npos);
  EXPECT_TRUE(report.find("3.20") != std::string::npos);
}

TEST(Step10, ElementArrayBindingSyncsVao) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint buf = 0;
  ctx.buffers().GenBuffers(1, &buf);
  ctx.BindBuffer(tgles::kGlElementArrayBuffer, buf);
  EXPECT_EQ(ctx.vertex_arrays().ElementArrayBuffer(), buf);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  // ARRAY_BUFFER binding is global, not VAO state: no VAO change expected.
  ctx.BindBuffer(tgles::kGlArrayBuffer, buf);
  EXPECT_EQ(ctx.buffers().BoundBuffer(tgles::kGlArrayBuffer), buf);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step10, StaleBufferErrorDoesNotBlockEabVaoSync) {
  // Minecraft 26.1.2: a prior BufferData failure left BufferManager pending;
  // BindBuffer(EAB) used to skip SetElementArrayBuffer when HasPending(),
  // so DrawElements fail-closed forever (no ELEMENT_ARRAY_BUFFER).
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, 0);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, 16, nullptr,
                           tgles::kGlStaticDraw);  // INVALID_OPERATION on 0.
  EXPECT_TRUE(ctx.buffers().HasPending());
  while (ctx.buffers().HasPending()) (void)ctx.buffers().GetError();
  // Re-create pending without clearing (simulate app not polling glGetError).
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, 0);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, 16, nullptr,
                           tgles::kGlStaticDraw);
  EXPECT_TRUE(ctx.buffers().HasPending());
  tgles::GLuint eab = 0;
  ctx.buffers().GenBuffers(1, &eab);
  ctx.BindBuffer(tgles::kGlElementArrayBuffer, eab);
  // EAB is always a valid target: VAO mirror must land even with pending.
  EXPECT_EQ(ctx.vertex_arrays().ElementArrayBuffer(), eab);
  ctx.SyncElementState();
  tgles::GLuint eab2 = 0;
  ctx.buffers().GenBuffers(1, &eab2);
  ctx.BindBuffer(tgles::kGlElementArrayBuffer, eab2);
  EXPECT_EQ(ctx.vertex_arrays().ElementArrayBuffer(), eab2);
}

TEST(Step10, IndirectDrawSeesBufferBinding) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  ctx.DrawElementsIndirect(tgles::kGlTriangles,
                           tgles::kGlUnsignedShortIndex, 0);
  // No DRAW_INDIRECT_BUFFER bound -> INVALID_OPERATION surfaces unified.
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);
  tgles::GLuint buf = 0;
  ctx.buffers().GenBuffers(1, &buf);
  ctx.BindBuffer(tgles::kGlDrawIndirectBuffer, buf);
  ctx.buffers().BufferData(tgles::kGlDrawIndirectBuffer, 64, nullptr,
                           tgles::kGlStaticDraw);
  ctx.DrawElementsIndirect(tgles::kGlTriangles,
                           tgles::kGlUnsignedShortIndex, 0);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step10, ComputeDispatchNeedsComputeProgram) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  ctx.DispatchCompute(1, 1, 1);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidOperation);  // No program.
  const char* comp = "#version 320 es\nvoid main() {}";
  tgles::GLuint s = ctx.shaders().CreateShader(tgles::kGlComputeShader);
  ctx.shaders().ShaderSource(s, 1, &comp, nullptr);
  ctx.shaders().CompileShader(s);
  tgles::GLuint p = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(p, s);
  ctx.programs().LinkProgram(p);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(p));
  ctx.programs().UseProgram(p);
  ctx.DispatchCompute(2, 1, 1);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(ctx.compute().LastDispatch().x, 2u);
}

TEST(Step10, UnifiedErrorPollingOrder) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Two managers fail; foundation polls first, then buffers.
  ctx.foundation().Enable(0xDEADBEEFu);  // INVALID_ENUM in foundation.
  tgles::GLuint zero = 0;
  ctx.buffers().DeleteBuffers(-1, &zero);  // INVALID_VALUE in buffers.
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);  // Foundation first.
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidValue);  // Then buffers.
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step10, EndToEndTriangleSetup) {
  // A minimal but complete triangle pipeline setup across modules.
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // Buffers + VAO.
  tgles::GLuint vbo = 0;
  ctx.buffers().GenBuffers(1, &vbo);
  ctx.BindBuffer(tgles::kGlArrayBuffer, vbo);
  const float verts[12] = {0};
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(verts), verts,
                           tgles::kGlStaticDraw);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().EnableVertexAttribArray(0);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, vbo);
  // Program.
  const char* vs = "#version 320 es\nlayout(location=0) in vec4 p;\n"
                   "void main() { gl_Position = p; }";
  const char* fs = "#version 320 es\nprecision mediump float;\n"
                   "out vec4 c;\nvoid main() { c = vec4(1.0); }";
  tgles::GLuint s0 = ctx.shaders().CreateShader(tgles::kGlVertexShader);
  tgles::GLuint s1 = ctx.shaders().CreateShader(tgles::kGlFragmentShader);
  ctx.shaders().ShaderSource(s0, 1, &vs, nullptr);
  ctx.shaders().ShaderSource(s1, 1, &fs, nullptr);
  ctx.shaders().CompileShader(s0);
  ctx.shaders().CompileShader(s1);
  tgles::GLuint prog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(prog, s0);
  ctx.programs().AttachShader(prog, s1);
  ctx.programs().LinkProgram(prog);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  ctx.programs().UseProgram(prog);
  // FBO + raster + draw validation + backend plan.
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().BindTexture(tgles::kGlTexture2d, tex);
  ctx.textures().TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 64, 64);
  tgles::GLuint fb = 0;
  ctx.framebuffers().GenFramebuffers(1, &fb);
  ctx.framebuffers().BindFramebuffer(tgles::kGlFramebuffer, fb);
  ctx.framebuffers().FramebufferTexture2D(tgles::kGlFramebuffer,
                                          tgles::kGlColorAttachment0,
                                          tgles::kGlTexture2d, tex, 0);
  EXPECT_EQ(ctx.framebuffers().CheckFramebufferStatus(tgles::kGlFramebuffer),
            tgles::kGlFramebufferComplete);
  ctx.raster().Viewport(0, 0, 64, 64);
  ctx.draw().DrawArrays(tgles::kGlTriangles, 0, 3);
  ctx.command_plan().BeginRenderPass();
  ctx.command_plan().Draw();
  ctx.command_plan().EndRenderPass();
  ctx.command_plan().Commit();
  EXPECT_TRUE(ctx.command_plan().Committed());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}
