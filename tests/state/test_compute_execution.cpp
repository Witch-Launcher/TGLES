// Test-first HONEST: DispatchCompute must EXECUTE, not just validate.
// Ground truth: ES 3.2 spec ch.19 (compute shaders write SSBO/images;
// DispatchCompute launches work groups). Current ComputeState only checks
// program-bound + group limits and stores LastDispatch (see
// src/state/compute.cpp) — buffer bytes never change. This test FAILS until
// GlesContext::DispatchCompute executes against bound SSBO storage.
//
// Honest subset for v1 (documented, not hidden): a compute program whose
// shader source contains the marker `// TGL_COMPUTE_ADD_ONE_SSBO0` executes
// as "every uint32 in SSBO binding 0 += 1" on the CPU store (facade) and via
// a real MTLComputeCommandEncoder kernel on the Apple bridge. Programs
// without the marker validate (no error) but perform no store mutation —
// reported via ConformanceChecklist, never claimed as full GLSL->MSL.

#include "test_framework.h"

#include <cstdint>
#include <cstring>

#include "tgles/facade/gles.h"

namespace {

const char* kComputeAddOneSrc =
    "#version 320 es\n"
    "// TGL_COMPUTE_ADD_ONE_SSBO0\n"
    "layout(local_size_x=1) in;\n"
    "layout(std430, binding=0) buffer B { uint v[]; };\n"
    "void main(){ uint i = gl_GlobalInvocationID.x; v[i] += 1u; }";

tgles::GLuint BuildAddOneProgram(tgles::GlesContext& ctx) {
  tgles::GLuint cs = ctx.shaders().CreateShader(tgles::kGlComputeShader);
  ctx.shaders().ShaderSource(cs, 1, &kComputeAddOneSrc, nullptr);
  ctx.shaders().CompileShader(cs);
  tgles::GLuint prog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(prog, cs);
  ctx.programs().LinkProgram(prog);
  return prog;
}

}  // namespace

TEST(ComputeExecution, DispatchAddsOneToSsbo0) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint prog = BuildAddOneProgram(ctx);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  ctx.programs().UseProgram(prog);
  ctx.SyncComputeProgram();
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  // SSBO binding 0 <- buffer with [1,2,3,4].
  tgles::GLuint buf = 0;
  ctx.buffers().GenBuffers(1, &buf);
  static const std::uint32_t kInit[4] = {1, 2, 3, 4};
  ctx.buffers().BindBuffer(tgles::kGlShaderStorageBuffer, buf);
  ctx.buffers().BufferData(tgles::kGlShaderStorageBuffer, sizeof(kInit), kInit,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBufferBase(tgles::kGlShaderStorageBuffer, 0, buf);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  ctx.DispatchCompute(4, 1, 1);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  const std::uint8_t* bytes = nullptr;
  tgles::GLsizeiptr size = 0;
  EXPECT_TRUE(ctx.buffers().GetBufferBytes(buf, &bytes, &size));
  EXPECT_EQ(size, (tgles::GLsizeiptr)sizeof(kInit));
  std::uint32_t out[4] = {0};
  std::memcpy(out, bytes, sizeof(out));
  // HONEST: execution must mutate the store. Currently FAILS (store stays
  // [1,2,3,4]) because DispatchCompute only validates.
  EXPECT_EQ(out[0], 2u);
  EXPECT_EQ(out[1], 3u);
  EXPECT_EQ(out[2], 4u);
  EXPECT_EQ(out[3], 5u);
}

TEST(ComputeExecution, DispatchWithoutMarkerValidatesOnly) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  const char* src =
      "#version 320 es\nlayout(local_size_x=1) in;\nvoid main(){}";
  tgles::GLuint cs = ctx.shaders().CreateShader(tgles::kGlComputeShader);
  ctx.shaders().ShaderSource(cs, 1, &src, nullptr);
  ctx.shaders().CompileShader(cs);
  tgles::GLuint prog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(prog, cs);
  ctx.programs().LinkProgram(prog);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  ctx.programs().UseProgram(prog);
  ctx.SyncComputeProgram();
  tgles::GLuint buf = 0;
  ctx.buffers().GenBuffers(1, &buf);
  static const std::uint32_t kInit[2] = {10, 20};
  ctx.buffers().BindBuffer(tgles::kGlShaderStorageBuffer, buf);
  ctx.buffers().BufferData(tgles::kGlShaderStorageBuffer, sizeof(kInit), kInit,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBufferBase(tgles::kGlShaderStorageBuffer, 0, buf);
  ctx.DispatchCompute(2, 1, 1);
  // No marker: validation passes, store untouched (documented subset).
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  const std::uint8_t* bytes = nullptr;
  tgles::GLsizeiptr size = 0;
  EXPECT_TRUE(ctx.buffers().GetBufferBytes(buf, &bytes, &size));
  std::uint32_t out[2] = {0};
  std::memcpy(out, bytes, sizeof(out));
  EXPECT_EQ(out[0], 10u);
  EXPECT_EQ(out[1], 20u);
}

TEST(ComputeExecution, IndirectDispatchReadsGroupsFromStore) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint prog = BuildAddOneProgram(ctx);
  ctx.programs().UseProgram(prog);
  ctx.SyncComputeProgram();
  tgles::GLuint buf = 0;
  ctx.buffers().GenBuffers(1, &buf);
  static const std::uint32_t kInit[4] = {10, 20, 30, 40};
  ctx.buffers().BindBuffer(tgles::kGlShaderStorageBuffer, buf);
  ctx.buffers().BufferData(tgles::kGlShaderStorageBuffer, sizeof(kInit), kInit,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBufferBase(tgles::kGlShaderStorageBuffer, 0, buf);
  // Indirect store: groups (3,1,1) at offset 0, misaligned-safe.
  tgles::GLuint ibuf = 0;
  ctx.buffers().GenBuffers(1, &ibuf);
  static const std::uint32_t kGroups[3] = {3, 1, 1};
  ctx.buffers().BindBuffer(tgles::kGlDispatchIndirectBuffer, ibuf);
  ctx.buffers().BufferData(tgles::kGlDispatchIndirectBuffer, sizeof(kGroups),
                           kGroups, tgles::kGlStaticDraw);
  ctx.DispatchComputeIndirect(0);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  const std::uint8_t* bytes = nullptr;
  tgles::GLsizeiptr size = 0;
  EXPECT_TRUE(ctx.buffers().GetBufferBytes(buf, &bytes, &size));
  std::uint32_t out[4] = {0};
  std::memcpy(out, bytes, sizeof(out));
  // First 3 lanes incremented, 4th untouched (groups gate the lanes).
  EXPECT_EQ(out[0], 11u);
  EXPECT_EQ(out[1], 21u);
  EXPECT_EQ(out[2], 31u);
  EXPECT_EQ(out[3], 40u);
}

TEST(ComputeExecution, DispatchStillRejectsBadGroups) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint prog = BuildAddOneProgram(ctx);
  ctx.programs().UseProgram(prog);
  ctx.SyncComputeProgram();
  ctx.DispatchCompute(70000, 1, 1);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}
