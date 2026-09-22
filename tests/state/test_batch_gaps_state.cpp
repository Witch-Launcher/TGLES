// Type-1 test: state-manager unit coverage for the same batch as
// test_batch_gaps_abi.cpp (Get* + VertexAttrib + Draw + sot). Written to pin
// manager behavior independently of the ABI layer (red would be compile-time
// missing methods; green proves the state layer is real).
//
// Ground truth: docs.gl/es3 + docs/reference/gl32.h (same as the ABI test).

#include "test_framework.h"

#include <cstring>

#include "tgles/facade/gles.h"
#include "tgles/pipeline/draw.h"
#include "tgles/state/buffer.h"
#include "tgles/state/debug.h"
#include "tgles/state/program.h"
#include "tgles/state/query.h"
#include "tgles/state/sampler.h"
#include "tgles/state/shader.h"
#include "tgles/state/texture.h"
#include "tgles/state/vertex_array.h"

static tgles::GLuint LinkedProgramWith(tgles::ShaderManager& sm,
                                tgles::ProgramManager& pm, const char* vs_body,
                                const char* fs_body) {
  const char* vs = vs_body;
  const char* fs = fs_body;
  tgles::GLuint v = sm.CreateShader(tgles::kGlVertexShader);
  sm.ShaderSource(v, 1, &vs, nullptr);
  sm.CompileShader(v);
  tgles::GLuint f = sm.CreateShader(tgles::kGlFragmentShader);
  sm.ShaderSource(f, 1, &fs, nullptr);
  sm.CompileShader(f);
  tgles::GLuint p = pm.CreateProgram();
  pm.AttachShader(p, v);
  pm.AttachShader(p, f);
  pm.LinkProgram(p);
  return p;
}

TEST(BatchStateSot, Uniform1ivAnd4fv) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  const char* vs = "#version 320 es\nvoid main(){}";
  const char* fs =
      "#version 320 es\nprecision mediump float;\nuniform int u_i;\nuniform "
      "vec4 u_v;\nvoid main(){}";
  tgles::GLuint p = LinkedProgramWith(sm, pm, vs, fs);
  EXPECT_TRUE(pm.LinkSucceeded(p));
  pm.UseProgram(p);
  tgles::GLint li = pm.GetUniformLocation(p, "u_i");
  tgles::GLint lv = pm.GetUniformLocation(p, "u_v");
  EXPECT_TRUE(li >= 0 && lv >= 0);
  const tgles::GLint iv[1] = {7};
  pm.Uniform1iv(li, 1, iv);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  const tgles::GLfloat fv[4] = {1, 2, 3, 4};
  pm.Uniform4fv(lv, 1, fv);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  pm.Uniform1iv(li, -1, iv);
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidValue);
}

TEST(BatchStateSot, TexAndSamplerIv) {
  tgles::TextureManager tm;
  tm.GenTextures(1, nullptr);  // Bad args fail closed.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  tgles::GLuint t = 0;
  // Gen via direct manager (needs array).
  tgles::GLuint ids[1] = {0};
  tm.GenTextures(1, ids);
  t = ids[0];
  tm.BindTexture(tgles::kGlTexture2d, t);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  const tgles::GLint wrap = tgles::kGlRepeat;
  tm.TexParameteriv(tgles::kGlTexture2d, tgles::kGlTextureWrapS, &wrap);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tgles::GLint got = 0;
  tm.GetTexParameteriv(tgles::kGlTexture2d, tgles::kGlTextureWrapS, &got);
  EXPECT_EQ(got, wrap);

  tgles::SamplerManager sm;
  tgles::GLuint sids[1] = {0};
  sm.GenSamplers(1, sids);
  sm.BindSampler(0, sids[0]);
  const tgles::GLint lin = tgles::kGlLinear;
  sm.SamplerParameteriv(sids[0], tgles::kGlTextureMagFilter, &lin);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
  sm.SamplerParameteriv(9999u, tgles::kGlTextureMagFilter, &lin);
  EXPECT_EQ(sm.GetError(), tgles::kGlInvalidOperation);
}

TEST(BatchStateGet, BufferPointer) {
  tgles::BufferManager bm;
  tgles::GLuint b = 0;
  bm.GenBuffers(1, &b);
  bm.BindBuffer(tgles::kGlArrayBuffer, b);
  bm.BufferData(tgles::kGlArrayBuffer, 16, nullptr, tgles::kGlDynamicDraw);
  void* p = bm.MapBufferRange(tgles::kGlArrayBuffer, 0, 16,
                              tgles::kGlMapWriteBit);
  EXPECT_TRUE(p != nullptr);
  void* out = nullptr;
  bm.GetBufferPointerv(tgles::kGlArrayBuffer, 0x88BDu, &out);
  EXPECT_EQ(bm.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(out == p);
  bm.UnmapBuffer(tgles::kGlArrayBuffer);
  bm.GetBufferPointerv(tgles::kGlArrayBuffer, 0x88BDu, &out);
  EXPECT_TRUE(out == nullptr);
  bm.GetBufferPointerv(0x9999u, 0x88BDu, &out);
  EXPECT_EQ(bm.GetError(), tgles::kGlInvalidEnum);
}

TEST(BatchStateGet, Internalformat) {
  tgles::RenderbufferManager rb;
  tgles::GLint v[2] = {};
  rb.GetInternalformativ(0x8D41u, 0x8058u, 0x9380u, 2, v);
  EXPECT_EQ(rb.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(v[0] >= 1);
  rb.GetInternalformativ(0x9999u, 0x8058u, 0x80A9u, 2, v);
  EXPECT_EQ(rb.GetError(), tgles::kGlInvalidEnum);
}

TEST(BatchStateVertex, SeparateBinding) {
  tgles::VertexArrayManager vao;
  vao.BindVertexBuffer(0, 7, 0, 12);
  EXPECT_EQ(vao.GetError(), tgles::kGlNoError);
  EXPECT_EQ(vao.BindingState(0).buffer, 7u);
  vao.VertexAttribFormat(0, 3, tgles::kGlFloat, tgles::kGlFalse, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlNoError);
  vao.VertexAttribBinding(0, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlNoError);
  vao.VertexBindingDivisor(0, 1);
  EXPECT_EQ(vao.GetError(), tgles::kGlNoError);
  EXPECT_EQ(vao.BindingState(0).divisor, 1u);
  vao.VertexAttribFormat(99u, 3, tgles::kGlFloat, tgles::kGlFalse, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlInvalidValue);
  vao.VertexAttribIFormat(1, 4, tgles::kGlInt, 0);
  EXPECT_EQ(vao.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(vao.AttribState(1).pure_integer);
}

TEST(BatchStateDraw, InstancedValidation) {
  tgles::DrawValidator d;
  d.DrawArraysInstanced(0x0004u, 0, 3, 2);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
  d.DrawArraysInstanced(0x9999u, 0, 3, 1);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidEnum);
  d.DrawArraysInstanced(0x0004u, 0, 3, -1);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidValue);
  d.SetIndirectBufferBound(false);
  d.DrawArraysIndirect(0x0004u, 0);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidOperation);
}

TEST(BatchStatePipeline, ProgramPipelineQuery) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint pipes[1] = {0};
  pm.GenProgramPipelines(1, pipes);
  pm.ValidateProgramPipeline(pipes[0]);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  tgles::GLint v = -1;
  pm.GetProgramPipelineiv(pipes[0], 0x8259u /*ACTIVE_PROGRAM*/, &v);
  EXPECT_EQ(v, 0);
  pm.GetProgramPipelineiv(9999u, 0x8259u, &v);
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidOperation);
}

TEST(BatchStateQueryExt, CounterRoundTrip) {
  tgles::QueryManager q;
  tgles::GLuint id = 0;
  q.GenQueries(1, &id);
  q.QueryCounterEXT(id, 0x8E28u /*TIMESTAMP*/);
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
  tgles::GLint v = -1;
  q.GetQueryObjectivEXT(id, 0x8866u /*RESULT*/, &v);
  EXPECT_EQ(v, 0);
  q.QueryCounterEXT(id, 0x9999u);
  EXPECT_EQ(q.GetError(), tgles::kGlInvalidEnum);
}

TEST(BatchStateFacade, PixelsAndTransformFeedbackMembersExist) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  ctx.pixels().PixelStorei(tgles::kGlUnpackAlignment, 1);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  tgles::GLuint ids[1] = {0};
  ctx.transform_feedback().GenTransformFeedbacks(1, ids);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}
