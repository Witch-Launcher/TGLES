// Step 9 tests: PSO cache reuse/eviction, resource placement advice,
// format family gating and command-encoder ordering.

#include "test_framework.h"

#include "tgles/gpu/backend.h"
#include "tgles/state/buffer.h"   // Usage hints.
#include "tgles/state/texture.h"  // Sized formats.

TEST(Step09, PsoCacheHitsAndEviction) {
  tgles::backend::PsoCache cache(2);
  tgles::backend::PsoKey a;
  a.vertex_program = 1;
  tgles::backend::PsoKey b = a;
  b.fragment_program = 2;
  bool hit = true;
  tgles::GLuint ha = cache.GetOrCreate(a, &hit);
  EXPECT_FALSE(hit);
  EXPECT_EQ(cache.GetOrCreate(a, &hit), ha);  // Same state -> same PSO.
  EXPECT_TRUE(hit);
  EXPECT_EQ(cache.Size(), 1u);
  cache.GetOrCreate(b, &hit);
  EXPECT_EQ(cache.Size(), 2u);
  tgles::backend::PsoKey c = a;
  c.sample_count = 4;
  cache.GetOrCreate(c, &hit);  // Over capacity -> FIFO evicts `a`.
  EXPECT_EQ(cache.Size(), 2u);
  EXPECT_EQ(cache.GetOrCreate(a, &hit), ha + 3);  // Recompiled handle.
  EXPECT_FALSE(hit);
}

TEST(Step09, PsoKeyDistinguishesState) {
  tgles::backend::PsoKey a, b;
  EXPECT_TRUE(a == b);
  b.blend_enabled = true;
  EXPECT_FALSE(a == b);
  b = a;
  b.depth_func = 0x0203u;  // LEQUAL.
  EXPECT_FALSE(a == b);
}

TEST(Step09, BufferStorageAdvice) {
  using tgles::backend::StorageMode;
  EXPECT_TRUE(tgles::backend::ResourcePlan::AdviseBufferStorage(
                  tgles::kGlStaticDraw) == StorageMode::kPrivate);
  EXPECT_TRUE(tgles::backend::ResourcePlan::AdviseBufferStorage(
                  tgles::kGlDynamicDraw) == StorageMode::kShared);
  EXPECT_TRUE(tgles::backend::ResourcePlan::AdviseBufferStorage(
                  tgles::kGlStreamRead) == StorageMode::kShared);
}

TEST(Step09, FormatFamilyGating) {
  // BC1 needs Apple9+; RGBA8 works everywhere.
  EXPECT_FALSE(tgles::backend::ResourcePlan::IsFormatSupported(0x83F1u,
                                                               "Apple7"));
  EXPECT_TRUE(
      tgles::backend::ResourcePlan::IsFormatSupported(0x83F1u, "Apple9"));
  EXPECT_TRUE(tgles::backend::ResourcePlan::IsFormatSupported(
      tgles::kGlRgba8, "Apple3"));
  EXPECT_FALSE(
      tgles::backend::ResourcePlan::IsFormatSupported(tgles::kGlRgba8, "Nope"));
  EXPECT_FALSE(
      tgles::backend::ResourcePlan::IsFormatSupported(tgles::kGlRgba8, nullptr));
}

TEST(Step09, SampleCountClamp) {
  EXPECT_EQ(
      tgles::backend::ResourcePlan::ClampSampleCount(8, "Apple7"), 4);
  EXPECT_EQ(
      tgles::backend::ResourcePlan::ClampSampleCount(2, "Apple3"), 2);
  EXPECT_EQ(
      tgles::backend::ResourcePlan::ClampSampleCount(1, "Apple3"), 1);
  EXPECT_EQ(
      tgles::backend::ResourcePlan::ClampSampleCount(4, "Nope"), 0);
}

TEST(Step09, CommandOrdering) {
  tgles::backend::CommandPlan plan;
  plan.Draw();  // No open pass.
  EXPECT_EQ(plan.GetError(), tgles::kGlInvalidOperation);
  plan.BeginRenderPass();
  plan.Draw();
  plan.Draw();
  EXPECT_EQ(plan.DrawCount(), 2u);
  plan.Blit();  // Blit inside a render pass.
  EXPECT_EQ(plan.GetError(), tgles::kGlInvalidOperation);
  plan.EndRenderPass();
  plan.Blit();
  EXPECT_EQ(plan.GetError(), tgles::kGlNoError);
  plan.Commit();
  EXPECT_TRUE(plan.Committed());
  plan.Draw();  // After commit.
  EXPECT_EQ(plan.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(plan.GetError(), tgles::kGlNoError);
}
