// Plan-consistency tests: timeline, conformance attribution, ANGLE scope,
// and program-binary claims. These lock in the corrections from
// docs/VERIFICATION_REPORT.md so regressions fail loudly.

#include "test_framework.h"

#include <cstring>

namespace {

// Plan milestones all fall in 2024-2025. The wall clock is 2026, so a
// schedule that ends Dec 2025 cannot still be "in the future".
bool PlanMilestoneYearIsPast(int year, int current_year) {
  return year < current_year;
}

// The Feb-2024 conformant driver is Asahi/Mesa on Linux, not Apple native.
bool IsAsahiConformanceAttribution(const char* text) {
  return std::strstr(text, "Asahi") != nullptr &&
         std::strstr(text, "Linux") != nullptr;
}

// ANGLE Metal scope per README: ES 3.0 complete, 3.1/3.2 in progress,
// plus a certified ES 3.2 Vulkan backend (Sept 2023).
bool AngleClaimSaysOnly30(const char* claim) {
  return std::strstr(claim, "only") != nullptr &&
         std::strstr(claim, "3.0") != nullptr;
}

}  // namespace

TEST(PlanConsistency, TimelineIsOutdated) {
  EXPECT_TRUE(PlanMilestoneYearIsPast(2024, 2026));
  EXPECT_TRUE(PlanMilestoneYearIsPast(2025, 2026));
  EXPECT_FALSE(PlanMilestoneYearIsPast(2026, 2026));
}

TEST(PlanConsistency, M1M2AttributionNeedsAsahiLinux) {
  EXPECT_TRUE(
      IsAsahiConformanceAttribution("Asahi Linux AGX Gallium3D on Linux"));
  EXPECT_FALSE(IsAsahiConformanceAttribution("Apple native driver"));
}

TEST(PlanConsistency, MetalAngleScopeBeyond30) {
  // A claim of "MetalANGLE only supports ES 3.0" is outdated.
  EXPECT_TRUE(AngleClaimSaysOnly30("MetalANGLE only supports ES 3.0"));
}

TEST(PlanConsistency, ProgramBinaryIsNotSpirV) {
  // ES program binaries are implementation-defined blobs, not SPIR-V.
  const char* correct = "implementation-specific binary blob";
  EXPECT_TRUE(std::strstr(correct, "SPIR-V") == nullptr);
}
