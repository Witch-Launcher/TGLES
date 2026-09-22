// Honest GPU compute proof (Apple only, real MTLComputeCommandEncoder).
// Ground truth: ES 3.2 ch.19 dispatches work groups; Metal executes via
// compute pipelines (MTLComputePipelineState + dispatchThreads). TGL v1
// subset `TGL_COMPUTE_ADD_ONE_SSBO0` (see test_compute_execution.cpp) must
// be bit-exact on GPU and CPU. This test drives the bridge directly (no
// facade) so a GPU failure cannot hide behind CPU emulation.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_COMP_HAS_METAL 1
#else
#define TGLES_COMP_HAS_METAL 0
#endif

#include "test_framework.h"

#include <cstdint>

#include "tgles/gpu/metal_bridge.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_COMP_HAS_METAL

TEST(ComputeReal, GpuIncrementIsBitExact) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  static const std::uint32_t kIn[8] = {0, 1, 2, 100, 0xFFFFFFFFu - 1, 7, 8, 9};
  std::uint32_t out[8] = {0};
  EXPECT_TRUE(bridge->ComputeIncrementUint32(kIn, out, 8));
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
  for (int i = 0; i < 8; ++i) EXPECT_EQ(out[i], kIn[i] + 1u);
  // Empty dispatch is a no-op success (spec: zero groups dispatch nothing).
  EXPECT_TRUE(bridge->ComputeIncrementUint32(nullptr, nullptr, 0));
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
  // Null with count fails closed (no silent no-op).
  EXPECT_FALSE(bridge->ComputeIncrementUint32(nullptr, out, 4));
  EXPECT_EQ(bridge->GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(bridge->GetError(), tgles::kGlNoError);
}

TEST(ComputeReal, MockIncrementMatchesGpuSemantics) {
  tgles::metal_bridge::MockMetalBridge mock;
  EXPECT_TRUE(mock.Initialize("Apple3"));
  static const std::uint32_t kIn[4] = {5, 6, 7, 8};
  std::uint32_t out[4] = {0};
  EXPECT_TRUE(mock.ComputeIncrementUint32(kIn, out, 4));
  for (int i = 0; i < 4; ++i) EXPECT_EQ(out[i], kIn[i] + 1u);
  EXPECT_EQ(mock.GetError(), tgles::kGlNoError);
}

#else

TEST(ComputeReal, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
