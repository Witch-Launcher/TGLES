// Negative API tests: symbols the plan lists as core ES 3.2 but that are
// actually desktop-GL-only or EXT-only. Verified against gl32.h + glext.h.

#include "test_framework.h"

#include <fstream>
#include <string>

#include "tgles/base/spec.h"

namespace {

bool HeaderContains(const std::string& path, const char* symbol) {
  std::ifstream in(path);
  if (!in) return false;
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return content.find(symbol) != std::string::npos;
}

}  // namespace

TEST(NegativeApi, NonCoreFunctionsFlagged) {
  EXPECT_TRUE(tgles::IsNonCoreFunction("glPolygonMode"));
  EXPECT_TRUE(tgles::IsNonCoreFunction("glClipControl"));
  EXPECT_TRUE(tgles::IsNonCoreFunction("glMultiDrawArrays"));
  EXPECT_TRUE(tgles::IsNonCoreFunction("glMultiDrawElements"));
  EXPECT_TRUE(tgles::IsNonCoreFunction("glDrawTransformFeedback"));
  EXPECT_TRUE(tgles::IsNonCoreFunction("glProvokingVertex"));
  EXPECT_TRUE(tgles::IsNonCoreFunction("glPointSize"));
}

TEST(NegativeApi, CoreFunctionsNotFlagged) {
  EXPECT_FALSE(tgles::IsNonCoreFunction("glDrawArrays"));
  EXPECT_FALSE(tgles::IsNonCoreFunction("glPatchParameteri"));
  EXPECT_FALSE(tgles::IsNonCoreFunction("glDispatchCompute"));
  EXPECT_FALSE(tgles::IsNonCoreFunction("glPolygonOffset"));
  EXPECT_FALSE(tgles::IsNonCoreFunction("glLineWidth"));
  EXPECT_FALSE(tgles::IsNonCoreFunction(nullptr));
}

TEST(NegativeApi, MissingFromCoreHeader) {
  const char* dir = TGLES_REFERENCE_DIR;
  std::string core = std::string(dir) + "/gl32.h";
  std::ifstream probe(core);
  if (!probe) return;
  EXPECT_FALSE(HeaderContains(core, "glPolygonMode"));
  EXPECT_FALSE(HeaderContains(core, "glClipControl"));
  EXPECT_FALSE(HeaderContains(core, "glMultiDrawArrays"));
  EXPECT_FALSE(HeaderContains(core, "glDrawTransformFeedback"));
  EXPECT_FALSE(HeaderContains(core, "GL_PROGRAM_POINT_SIZE"));
}

TEST(NegativeApi, PresentOnlyAsExtension) {
  const char* dir = TGLES_REFERENCE_DIR;
  std::string ext = std::string(dir) + "/glext.h";
  std::ifstream probe(ext);
  if (!probe) return;
  // These exist but ONLY with EXT/NV suffix, never as core names.
  EXPECT_TRUE(HeaderContains(ext, "glMultiDrawArraysEXT"));
  EXPECT_TRUE(HeaderContains(ext, "glClipControlEXT"));
  EXPECT_TRUE(HeaderContains(ext, "glPolygonModeNV"));
}
