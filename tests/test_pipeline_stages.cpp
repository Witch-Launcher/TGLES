// Programmable pipeline order (spec Chapter 12+):
// vertex -> tess_control -> tess_evaluation -> geometry -> raster -> fragment,
// plus an independent compute pipeline.

#include "test_framework.h"

#include "tgles/metal_mapping.h"
#include "tgles/spec.h"

TEST(PipelineStages, StageOrderIsCanonical) {
  // Encode the canonical order as integers and verify monotonicity.
  const tgles::ShaderStage order[] = {
      tgles::ShaderStage::kVertex, tgles::ShaderStage::kTessControl,
      tgles::ShaderStage::kTessEvaluation, tgles::ShaderStage::kGeometry,
      tgles::ShaderStage::kFragment,
  };
  for (std::size_t i = 1; i < sizeof(order) / sizeof(order[0]); ++i) {
    EXPECT_TRUE(static_cast<int>(order[i]) > static_cast<int>(order[i - 1]));
  }
  // Compute is a standalone pipeline, not part of the draw order.
  EXPECT_NOT_NULL(tgles::ShaderStageName(tgles::ShaderStage::kCompute));
}

TEST(PipelineStages, TessellationNeedsPatchMode) {
  // Tessellation draws use GL_PATCHES + glPatchParameteri (both core).
  const tgles::metal::FeatureMapping* m =
      tgles::metal::FindMapping("glPatchParameteri");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == tgles::metal::MappingKind::kDirect ||
              m->kind == tgles::metal::MappingKind::kEmulated);
}

TEST(PipelineStages, GeometryHasNoDirectMapping) {
  const tgles::metal::FeatureMapping* m =
      tgles::metal::FindMapping("geometry_shader");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == tgles::metal::MappingKind::kEmulated);
}
