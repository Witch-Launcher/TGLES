#ifndef TGLES_SPEC_H
#define TGLES_SPEC_H

#include <cstddef>

namespace tgles {

// ---------------------------------------------------------------------------
// Ground truth derived from docs/reference/gl32.h + es_spec_3.2.pdf.
// Every value is verified by tests/ against the official sources.
// ---------------------------------------------------------------------------

// Number of unique GL_APICALL entry points in official gl32.h.
// Verified by parsing docs/reference/gl32.h (358 lines with
// GL_APICALL ... GL_APIENTRY, including 3 pointer-returning functions
// glGetString, glGetStringi, glMapBufferRange that naive regexes miss).
inline constexpr std::size_t kCoreEntryPointCount = 358;

// GLSL ES language versions supported by OpenGL ES 3.2 implementations.
inline constexpr int kSupportedGlslVersions[] = {100, 300, 310, 320};
inline constexpr std::size_t kSupportedGlslVersionCount = 4;

// Required "#version" directive tokens per language version.
const char* GlslVersionDirective(int version);

// __VERSION__ integer reported by the GLSL ES 3.20 compiler frontend.
inline constexpr int kGlsl320VersionNumber = 320;

// Core shader types in ES 3.2 (6 stages).
enum class ShaderStage {
  kVertex = 0,
  kTessControl,
  kTessEvaluation,
  kGeometry,
  kFragment,
  kCompute
};
inline constexpr std::size_t kShaderStageCount = 6;
const char* ShaderStageName(ShaderStage stage);

// Core async query targets from spec Table 4.2 (exactly 4).
enum class QueryTarget {
  kPrimitivesGenerated = 0,
  kTransformFeedbackPrimitivesWritten,
  kAnySamplesPassed,
  kAnySamplesPassedConservative
};
inline constexpr std::size_t kCoreQueryTargetCount = 4;
const char* QueryTargetName(QueryTarget target);
unsigned QueryTargetEnum(QueryTarget target);  // GL enum value.

// Functions that are NOT part of core ES 3.2 (desktop GL or EXT only).
// The transpiler frontend must reject them as core API.
bool IsNonCoreFunction(const char* name);

// Query enums that are NOT core (only available via EXT_disjoint_timer_query).
bool IsNonCoreQueryEnum(const char* name);

}  // namespace tgles

#endif  // TGLES_SPEC_H
