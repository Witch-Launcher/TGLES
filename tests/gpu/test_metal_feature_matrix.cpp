// Apple GPU family matrix from Metal-Feature-Set-Tables.pdf (May 21, 2026).
// Corrects plan errors: BC starts at Apple9 (not A14), mesh needs Metal 3,
// and there is no A20 family in the published tables.

#include "test_framework.h"

#include "tgles/gpu/metal_mapping.h"

TEST(MetalFeatureMatrix, EightFamiliesApple3ToApple10) {
  std::size_t n = 0;
  const tgles::metal::GpuFamilyInfo* table =
      tgles::metal::GpuFamilyTable(&n);
  EXPECT_NOT_NULL(table);
  EXPECT_EQ(n, static_cast<std::size_t>(8));
}

TEST(MetalFeatureMatrix, TessellationAvailableFromApple3) {
  for (const char* f :
       {"Apple3", "Apple5", "Apple7", "Apple9", "Apple10"}) {
    const auto* info = tgles::metal::FindGpuFamily(f);
    EXPECT_NOT_NULL(info);
    EXPECT_TRUE(info->supports_tessellation);
  }
}

TEST(MetalFeatureMatrix, BcCompressionOnlyApple9Plus) {
  // Plan wrongly claimed BC on A14+. Tables say Apple9+.
  EXPECT_FALSE(tgles::metal::FindGpuFamily("Apple3")->supports_bc_compression);
  EXPECT_FALSE(tgles::metal::FindGpuFamily("Apple7")->supports_bc_compression);
  EXPECT_FALSE(tgles::metal::FindGpuFamily("Apple8")->supports_bc_compression);
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple9")->supports_bc_compression);
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple10")->supports_bc_compression);
}

TEST(MetalFeatureMatrix, MeshShaderNeedsMetal3Families) {
  EXPECT_FALSE(tgles::metal::FindGpuFamily("Apple3")->supports_mesh_shader);
  EXPECT_FALSE(tgles::metal::FindGpuFamily("Apple6")->supports_mesh_shader);
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple7")->supports_mesh_shader);
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple10")->supports_mesh_shader);
}

TEST(MetalFeatureMatrix, NoA20FamilyInPublishedTables) {
  EXPECT_TRUE(tgles::metal::FindGpuFamily("A20") == nullptr);
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple11") == nullptr);
  EXPECT_TRUE(tgles::metal::FindGpuFamily(nullptr) == nullptr);
}
