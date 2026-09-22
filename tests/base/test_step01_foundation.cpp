// Step 1 foundation tests: error model (spec 2.3.1), enable defaults
// (spec 15.1.7, Table 21.53), string/version queries (spec 20.2-20.3).

#include "test_framework.h"

#include <cstring>
#include <fstream>
#include <string>

#include "tgles/state/context.h"
#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"

namespace {

bool HeaderDefines(const std::string& path, const char* text) {
  std::ifstream in(path);
  if (!in) return false;
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return content.find(text) != std::string::npos;
}

std::string ReferencePath(const char* file) {
  return std::string(TGLES_REFERENCE_DIR) + "/" + file;
}

}  // namespace

// --- ErrorQueue semantics (spec 2.3.1) ---

TEST(Step01, ErrorQueueStartsClean) {
  tgles::ErrorQueue q;
  EXPECT_FALSE(q.HasPending());
  EXPECT_EQ(q.Get(), tgles::kGlNoError);
}

TEST(Step01, FirstErrorWins) {
  tgles::ErrorQueue q;
  q.Record(tgles::kGlInvalidEnum);
  q.Record(tgles::kGlInvalidValue);  // Must not replace the first error.
  EXPECT_TRUE(q.HasPending());
  EXPECT_EQ(q.Get(), tgles::kGlInvalidEnum);
  EXPECT_EQ(q.Get(), tgles::kGlNoError);  // Latch cleared.
  EXPECT_FALSE(q.HasPending());
}

TEST(Step01, RecordNoErrorIsIgnored) {
  tgles::ErrorQueue q;
  q.Record(tgles::kGlNoError);
  EXPECT_FALSE(q.HasPending());
  EXPECT_EQ(q.Get(), tgles::kGlNoError);
}

// --- Type/constant ground truth matches gl32.h ---

TEST(Step01, ErrorConstantsMatchOfficialHeader) {
  std::ifstream probe(ReferencePath("gl32.h"));
  if (!probe) return;
  EXPECT_EQ(tgles::kGlNoError, 0u);
  EXPECT_EQ(tgles::kGlInvalidEnum, 0x0500u);
  EXPECT_EQ(tgles::kGlInvalidValue, 0x0501u);
  EXPECT_EQ(tgles::kGlInvalidOperation, 0x0502u);
  EXPECT_EQ(tgles::kGlOutOfMemory, 0x0505u);
  EXPECT_EQ(tgles::kGlContextLost, 0x0507u);
  EXPECT_TRUE(HeaderDefines(ReferencePath("gl32.h"), "#define GL_NO_ERROR"));
  EXPECT_TRUE(
      HeaderDefines(ReferencePath("gl32.h"), "#define GL_INVALID_ENUM"));
  EXPECT_TRUE(
      HeaderDefines(ReferencePath("gl32.h"), "#define GL_CONTEXT_LOST"));
}

TEST(Step01, EnableCapConstantsMatchOfficialHeader) {
  std::ifstream probe(ReferencePath("gl32.h"));
  if (!probe) return;
  EXPECT_EQ(tgles::kGlBlend, 0x0BE2u);
  EXPECT_EQ(tgles::kGlDepthTest, 0x0B71u);
  EXPECT_EQ(tgles::kGlDither, 0x0BD0u);
  EXPECT_EQ(tgles::kGlScissorTest, 0x0C11u);
  EXPECT_EQ(tgles::kGlStencilTest, 0x0B90u);
  EXPECT_EQ(tgles::kGlCullFace, 0x0B44u);
  EXPECT_EQ(tgles::kGlPolygonOffsetFill, 0x8037u);
  EXPECT_EQ(tgles::kGlPrimitiveRestartFixedIndex, 0x8D69u);
  EXPECT_EQ(tgles::kGlRasterizerDiscard, 0x8C89u);
  EXPECT_EQ(tgles::kGlSampleAlphaToCoverage, 0x809Eu);
  EXPECT_EQ(tgles::kGlSampleCoverage, 0x80A0u);
  EXPECT_EQ(tgles::kGlDebugOutput, 0x92E0u);
  EXPECT_EQ(tgles::kGlDebugOutputSynchronous, 0x8242u);
}

// --- Context defaults ---

TEST(Step01, NonDebugContextDefaults) {
  tgles::Context ctx = tgles::Context::Create(false);
  EXPECT_FALSE(ctx.IsDebugContext());
  // Dither starts enabled (spec 15.1.7); everything else disabled.
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlDither), tgles::kGlTrue);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlBlend), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlDepthTest), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlCullFace), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlScissorTest), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlStencilTest), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlPolygonOffsetFill), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlPrimitiveRestartFixedIndex),
            tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlRasterizerDiscard), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlSampleAlphaToCoverage), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlSampleCoverage), tgles::kGlFalse);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlDebugOutputSynchronous), tgles::kGlFalse);
  // Non-debug context: DEBUG_OUTPUT starts disabled (Table 21.53).
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlDebugOutput), tgles::kGlFalse);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step01, DebugContextEnablesDebugOutput) {
  tgles::Context ctx = tgles::Context::Create(true);
  EXPECT_TRUE(ctx.IsDebugContext());
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlDebugOutput), tgles::kGlTrue);
  tgles::GLint flags = 0;
  ctx.GetIntegerv(tgles::kGlContextFlags, &flags);
  EXPECT_TRUE((flags & static_cast<tgles::GLint>(tgles::kGlContextFlagDebugBit)) != 0);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

// --- Enable / Disable / IsEnabled round-trips ---

TEST(Step01, EnableDisableRoundTripAllCaps) {
  static const tgles::GLenum kCaps[] = {
      tgles::kGlBlend,
      tgles::kGlCullFace,
      tgles::kGlDebugOutput,
      tgles::kGlDebugOutputSynchronous,
      tgles::kGlDepthTest,
      tgles::kGlDither,
      tgles::kGlPolygonOffsetFill,
      tgles::kGlPrimitiveRestartFixedIndex,
      tgles::kGlRasterizerDiscard,
      tgles::kGlSampleAlphaToCoverage,
      tgles::kGlSampleCoverage,
      tgles::kGlScissorTest,
      tgles::kGlStencilTest,
  };
  EXPECT_EQ(sizeof(kCaps) / sizeof(kCaps[0]),
            static_cast<std::size_t>(tgles::kCoreEnableCapCount));
  tgles::Context ctx = tgles::Context::Create(false);
  for (tgles::GLenum cap : kCaps) {
    ctx.Enable(cap);
    EXPECT_EQ(ctx.IsEnabled(cap), tgles::kGlTrue);
    ctx.Disable(cap);
    EXPECT_EQ(ctx.IsEnabled(cap), tgles::kGlFalse);
  }
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step01, InvalidCapRecordsInvalidEnumWithoutSideEffects) {
  tgles::Context ctx = tgles::Context::Create(false);
  ctx.Enable(0xDEADBEEFu);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
  ctx.Disable(0xDEADBEEFu);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
  // Value-returning command returns zero (FALSE) on error.
  EXPECT_EQ(ctx.IsEnabled(0xDEADBEEFu), tgles::kGlFalse);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
  // Failed commands changed nothing: DITHER still on, BLEND still off.
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlDither), tgles::kGlTrue);
  EXPECT_EQ(ctx.IsEnabled(tgles::kGlBlend), tgles::kGlFalse);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

// --- Server strings (spec 20.2) ---

TEST(Step01, GetStringReturnsFiveNames) {
  tgles::Context ctx = tgles::Context::Create(false);
  EXPECT_NOT_NULL(ctx.GetString(tgles::kGlVendor));
  EXPECT_NOT_NULL(ctx.GetString(tgles::kGlRenderer));
  EXPECT_NOT_NULL(ctx.GetString(tgles::kGlVersion));
  EXPECT_NOT_NULL(ctx.GetString(tgles::kGlShadingLanguageVersion));
  EXPECT_NOT_NULL(ctx.GetString(tgles::kGlExtensions));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step01, GetStringVersionFormats) {
  tgles::Context ctx = tgles::Context::Create(false);
  const char* version =
      reinterpret_cast<const char*>(ctx.GetString(tgles::kGlVersion));
  // "OpenGL ES N.M ..." with N.M == 3.2.
  EXPECT_TRUE(std::strncmp(version, "OpenGL ES 3.2", 13) == 0);
  const char* sl = reinterpret_cast<const char*>(
      ctx.GetString(tgles::kGlShadingLanguageVersion));
  // SL minor is always two digits: "3.20".
  EXPECT_TRUE(std::strstr(sl, "3.20") != nullptr);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step01, GetStringInvalidNameFailsCleanly) {
  tgles::Context ctx = tgles::Context::Create(false);
  EXPECT_TRUE(ctx.GetString(0xDEADBEEFu) == nullptr);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
}

// --- Indexed strings (spec 20.3) ---

TEST(Step01, GetStringiRejectsNonExtensionNames) {
  tgles::Context ctx = tgles::Context::Create(false);
  EXPECT_TRUE(ctx.GetStringi(tgles::kGlVersion, 0) == nullptr);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
}

TEST(Step01, GetStringiOutOfRangeIsInvalidValue) {
  tgles::Context ctx = tgles::Context::Create(false);
  tgles::GLint n = -1;
  ctx.GetIntegerv(tgles::kGlNumExtensions, &n);
  EXPECT_TRUE(n >= 1);
  // Every advertised index resolves; n itself is out of range.
  for (tgles::GLint i = 0; i < n; ++i) {
    const tgles::GLubyte* s =
        ctx.GetStringi(tgles::kGlExtensions, (tgles::GLuint)i);
    EXPECT_TRUE(s != nullptr);
    EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  }
  EXPECT_TRUE(ctx.GetStringi(tgles::kGlExtensions, (tgles::GLuint)n) ==
              nullptr);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidValue);
}

// Advertised extensions are fully executed features only (MobileGL gates on
// these strings): buffer_storage (persistent/coherent CPU-store mapping),
// multi_draw_indirect, draw_elements_base_vertex + multi_draw_arrays,
// base_instance, texture_view. Timer/dual-source/sampling features stay
// unadvertised until they execute.
// The space-joined EXTENSIONS string and the indexed list must agree.
TEST(Step01, AdvertisedExtensionsAreExecutable) {
  tgles::Context ctx = tgles::Context::Create(false);
  const char* joined =
      reinterpret_cast<const char*>(ctx.GetString(tgles::kGlExtensions));
  EXPECT_TRUE(joined != nullptr && joined[0] != '\0');
  tgles::GLint n = -1;
  ctx.GetIntegerv(tgles::kGlNumExtensions, &n);
  EXPECT_TRUE(n >= 1);
  // Split the joined string and compare element-wise with GetStringi.
  std::string rest(joined);
  for (tgles::GLint i = 0; i < n; ++i) {
    std::string::size_type sp = rest.find(' ');
    std::string first =
        sp == std::string::npos ? rest : rest.substr(0, sp);
    EXPECT_TRUE(!first.empty());
    const char* indexed = reinterpret_cast<const char*>(
        ctx.GetStringi(tgles::kGlExtensions, (tgles::GLuint)i));
    EXPECT_TRUE(indexed != nullptr);
    if (indexed != nullptr) EXPECT_TRUE(first == indexed);
    rest = sp == std::string::npos ? "" : rest.substr(sp + 1);
    EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  }
  EXPECT_TRUE(rest.empty());
  EXPECT_TRUE(std::strstr(joined, "GL_EXT_buffer_storage") != nullptr);
  EXPECT_TRUE(std::strstr(joined, "GL_EXT_multi_draw_indirect") != nullptr);
  EXPECT_TRUE(std::strstr(joined, "GL_EXT_base_instance") != nullptr);
  EXPECT_TRUE(std::strstr(joined, "GL_EXT_texture_view") != nullptr);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

// --- Numeric queries ---

TEST(Step01, VersionQueriesReport32) {
  tgles::Context ctx = tgles::Context::Create(false);
  tgles::GLint major = 0, minor = 0;
  ctx.GetIntegerv(tgles::kGlMajorVersion, &major);
  ctx.GetIntegerv(tgles::kGlMinorVersion, &minor);
  EXPECT_EQ(major, 3);
  EXPECT_EQ(minor, 2);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step01, MaxTextureSizeMeetsSpecMinimum) {
  tgles::Context ctx = tgles::Context::Create(false);
  tgles::GLint size = 0;
  ctx.GetIntegerv(tgles::kGlMaxTextureSize, &size);
  EXPECT_TRUE(size >= 2048);  // Spec-mandated minimum.
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(Step01, UnknownPnameLeavesBufferUntouched) {
  tgles::Context ctx = tgles::Context::Create(false);
  tgles::GLint sentinel = 12345;
  ctx.GetIntegerv(0xDEADBEEFu, &sentinel);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidEnum);
  // Spec 2.3.1: no pointer writes on error.
  EXPECT_EQ(sentinel, 12345);
}

TEST(Step01, NullParamsIsInvalidValue) {
  tgles::Context ctx = tgles::Context::Create(false);
  ctx.GetIntegerv(tgles::kGlMajorVersion, nullptr);
  EXPECT_EQ(ctx.GetError(), tgles::kGlInvalidValue);
}

// --- Context independence ---

TEST(Step01, ContextsAreIndependent) {
  tgles::Context a = tgles::Context::Create(false);
  tgles::Context b = tgles::Context::Create(false);
  a.Enable(tgles::kGlBlend);
  EXPECT_EQ(a.IsEnabled(tgles::kGlBlend), tgles::kGlTrue);
  EXPECT_EQ(b.IsEnabled(tgles::kGlBlend), tgles::kGlFalse);
  a.Enable(0xDEADBEEFu);
  EXPECT_EQ(a.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(b.GetError(), tgles::kGlNoError);
}
