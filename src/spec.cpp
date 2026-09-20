#include "tgles/spec.h"

#include <cstring>

namespace tgles {

const char* GlslVersionDirective(int version) {
  switch (version) {
    case 100: return "#version 100 es";
    case 300: return "#version 300 es";
    case 310: return "#version 310 es";
    case 320: return "#version 320 es";
    default: return nullptr;
  }
}

const char* ShaderStageName(ShaderStage stage) {
  switch (stage) {
    case ShaderStage::kVertex: return "vertex";
    case ShaderStage::kTessControl: return "tess_control";
    case ShaderStage::kTessEvaluation: return "tess_evaluation";
    case ShaderStage::kGeometry: return "geometry";
    case ShaderStage::kFragment: return "fragment";
    case ShaderStage::kCompute: return "compute";
    default: return nullptr;
  }
}

const char* QueryTargetName(QueryTarget target) {
  switch (target) {
    case QueryTarget::kPrimitivesGenerated: return "GL_PRIMITIVES_GENERATED";
    case QueryTarget::kTransformFeedbackPrimitivesWritten:
      return "GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN";
    case QueryTarget::kAnySamplesPassed: return "GL_ANY_SAMPLES_PASSED";
    case QueryTarget::kAnySamplesPassedConservative:
      return "GL_ANY_SAMPLES_PASSED_CONSERVATIVE";
    default: return nullptr;
  }
}

unsigned QueryTargetEnum(QueryTarget target) {
  switch (target) {
    case QueryTarget::kPrimitivesGenerated: return 0x8C87;
    case QueryTarget::kTransformFeedbackPrimitivesWritten: return 0x8C88;
    case QueryTarget::kAnySamplesPassed: return 0x8C2F;
    case QueryTarget::kAnySamplesPassedConservative: return 0x8D6A;
    default: return 0;
  }
}

// Verified against gl32.h: none of these symbols exist in core headers.
// They live in glext.h as EXT/NV or in desktop GL only.
bool IsNonCoreFunction(const char* name) {
  static const char* const kNonCore[] = {
      "glPolygonMode",          // desktop + GL_NV_polygon_mode (EXT)
      "glClipControl",          // glClipControlEXT only
      "glMultiDrawArrays",      // glMultiDrawArraysEXT only
      "glMultiDrawElements",    // glMultiDrawElementsEXT only
      "glDrawTransformFeedback",  // no such ES entry point
      "glProvokingVertex",      // no such ES entry point
      "glPointSize",            // desktop GL only (ES uses gl_PointSize)
  };
  if (name == nullptr) return false;
  for (const char* candidate : kNonCore) {
    if (std::strcmp(name, candidate) == 0) return true;
  }
  return false;
}

bool IsNonCoreQueryEnum(const char* name) {
  static const char* const kNonCoreQueries[] = {
      "GL_TIME_ELAPSED",                       // EXT_disjoint_timer_query
      "GL_TIMESTAMP",                          // EXT_disjoint_timer_query
      "GL_TRANSFORM_FEEDBACK_STREAM_OVERFLOW",  // not in core nor glext.h
  };
  if (name == nullptr) return false;
  for (const char* candidate : kNonCoreQueries) {
    if (std::strcmp(name, candidate) == 0) return true;
  }
  return false;
}

}  // namespace tgles
