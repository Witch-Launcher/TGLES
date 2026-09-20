// Step 6 tests: tessellation patch state, geometry primitive rules and
// compute dispatch/barrier validation.

#include "test_framework.h"

#include "tgles/compute.h"
#include "tgles/draw.h"  // kGlTriangleStrip.
#include "tgles/geometry.h"
#include "tgles/tessellation.h"

TEST(Step06, PatchSizeDefaultsAndValidation) {
  tgles::TessellationState tess;
  EXPECT_EQ(tgles::kMaxPatchVertices, 32);  // Spec minimum.
  EXPECT_EQ(tgles::kMaxTessGenLevel, 64);   // Spec minimum.
  EXPECT_EQ(tess.PatchSize(), 3);           // Spec initial value.
  tess.PatchParameteri(tgles::kGlPatchVertices, 16);
  EXPECT_EQ(tess.PatchSize(), 16);
  tgles::GLint v = 0;
  tess.GetPatchParameteriv(tgles::kGlPatchVertices, &v);
  EXPECT_EQ(v, 16);
  tess.PatchParameteri(tgles::kGlPatchVertices, 0);
  EXPECT_EQ(tess.GetError(), tgles::kGlInvalidValue);
  tess.PatchParameteri(tgles::kGlPatchVertices, 33);
  EXPECT_EQ(tess.GetError(), tgles::kGlInvalidValue);
  tess.PatchParameteri(0x1234u, 4);
  EXPECT_EQ(tess.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(tess.PatchSize(), 16);  // Failed calls change nothing.
  EXPECT_EQ(tess.GetError(), tgles::kGlNoError);
}

TEST(Step06, GeometryPrimitiveRules) {
  EXPECT_TRUE(tgles::GeometryState::IsValidInputPrimitive(0x0004u));
  EXPECT_TRUE(tgles::GeometryState::IsValidInputPrimitive(
      tgles::kGlTrianglesAdjacency));
  EXPECT_FALSE(
      tgles::GeometryState::IsValidInputPrimitive(tgles::kGlPatches));
  EXPECT_TRUE(tgles::GeometryState::IsValidOutputPrimitive(
      tgles::kGlTriangleStrip));
  EXPECT_FALSE(tgles::GeometryState::IsValidOutputPrimitive(0x0004u));
  EXPECT_TRUE(tgles::kMaxGeometryOutputVertices >= 256);  // Spec minimum.
  EXPECT_TRUE(tgles::kMaxGeometryTotalOutputComponents > 0);
}

TEST(Step06, ComputeLimitsMeetMinimums) {
  EXPECT_EQ(tgles::kMaxComputeWorkGroupCount[0], 65535);
  EXPECT_EQ(tgles::kMaxComputeWorkGroupCount[1], 65535);
  EXPECT_EQ(tgles::kMaxComputeWorkGroupCount[2], 65535);
  EXPECT_EQ(tgles::kMaxComputeWorkGroupSize[0], 1024);
  EXPECT_EQ(tgles::kMaxComputeWorkGroupSize[2], 64);
  EXPECT_EQ(tgles::kMaxComputeWorkGroupInvocations, 128);
  EXPECT_EQ(tgles::kMaxComputeSharedMemorySize, 16384);
}

TEST(Step06, DispatchNeedsProgramAndBounds) {
  tgles::ComputeState comp;
  comp.DispatchCompute(1, 1, 1);
  EXPECT_EQ(comp.GetError(), tgles::kGlInvalidOperation);  // No program.
  comp.SetComputeProgramBound(true);
  comp.DispatchCompute(4, 2, 1);
  EXPECT_EQ(comp.GetError(), tgles::kGlNoError);
  EXPECT_EQ(comp.LastDispatch().x, 4u);
  comp.DispatchCompute(70000, 1, 1);  // Past the x limit.
  EXPECT_EQ(comp.GetError(), tgles::kGlInvalidValue);
  comp.DispatchCompute(0, 0, 0);  // Legal no-op dispatch.
  EXPECT_EQ(comp.GetError(), tgles::kGlNoError);
  comp.DispatchComputeIndirect(3);  // Misaligned offset.
  EXPECT_EQ(comp.GetError(), tgles::kGlInvalidValue);
  comp.DispatchComputeIndirect(16);
  EXPECT_EQ(comp.GetError(), tgles::kGlNoError);
}

TEST(Step06, BarrierBitsChecked) {
  tgles::ComputeState comp;
  comp.MemoryBarrier(tgles::kGlShaderStorageBarrierBit |
                     tgles::kGlUniformBarrierBit);
  EXPECT_EQ(comp.GetError(), tgles::kGlNoError);
  comp.MemoryBarrier(tgles::kGlAllBarrierBits);
  EXPECT_EQ(comp.GetError(), tgles::kGlNoError);
  comp.MemoryBarrier(0x00010000u);  // Unknown bit.
  EXPECT_EQ(comp.GetError(), tgles::kGlInvalidValue);
  comp.MemoryBarrierByRegion(tgles::kGlFramebufferBarrierBit);
  EXPECT_EQ(comp.GetError(), tgles::kGlNoError);
}
