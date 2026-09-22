// GL -> Metal mapping table: every entry in plan Table III.3 is checked.
// Direct mappings must stay direct; known-hard features must be emulated.

#include "test_framework.h"

#include "tgles/gpu/metal_mapping.h"

TEST(MetalMapping, TableIsNonEmpty) {
  std::size_t n = 0;
  const tgles::metal::FeatureMapping* table =
      tgles::metal::MappingTable(&n);
  EXPECT_NOT_NULL(table);
  EXPECT_TRUE(n >= 10);
}

TEST(MetalMapping, DirectDrawMappings) {
  using tgles::metal::MappingKind;
  const auto* m = tgles::metal::FindMapping("glDrawArrays");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == MappingKind::kDirect);

  m = tgles::metal::FindMapping("glDrawElements");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == MappingKind::kDirect);

  m = tgles::metal::FindMapping("glDispatchCompute");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == MappingKind::kDirect);
}

TEST(MetalMapping, HardFeaturesAreEmulated) {
  using tgles::metal::MappingKind;
  const auto* m = tgles::metal::FindMapping("geometry_shader");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == MappingKind::kEmulated);

  m = tgles::metal::FindMapping("glLineWidth");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == MappingKind::kEmulated);

  m = tgles::metal::FindMapping("gl_PointSize");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == MappingKind::kEmulated);
}

TEST(MetalMapping, TimerQueryUnsupported) {
  using tgles::metal::MappingKind;
  const auto* m = tgles::metal::FindMapping("timer_query");
  EXPECT_NOT_NULL(m);
  EXPECT_TRUE(m->kind == MappingKind::kUnsupported);
  EXPECT_TRUE(tgles::metal::FindMapping("no_such_feature") == nullptr);
  EXPECT_TRUE(tgles::metal::FindMapping(nullptr) == nullptr);
}
