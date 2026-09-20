#include "tgles/msl_translation.h"

#include <cstring>

namespace tgles {
namespace msl {

const char* CorrectedTriangleVertexShader() {
  // Valid MSL: varyings travel in a struct, not as "thread ... [[user]]"
  // function parameters.
  return R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  float4 position [[attribute(0)]];
  float4 color    [[attribute(1)]];
};

struct Uniforms {
  float4x4 u_modelViewProj;
};

struct Varyings {
  float4 position [[position]];
  float4 color    [[user(locn0)]];
};

vertex Varyings vert_main(VertexIn in [[stage_in]],
                          constant Uniforms& uni [[buffer(0)]]) {
  Varyings out;
  out.position = uni.u_modelViewProj * in.position;
  out.color = in.color;
  return out;
}

fragment float4 frag_main(Varyings in [[stage_in]]) {
  return in.color;
}
)MSL";
}

bool UsesInvalidThreadUserVarying(const char* snippet) {
  if (snippet == nullptr) return false;
  // The plan's invalid pattern: "thread" + "[[user(" in the same snippet.
  return std::strstr(snippet, "thread") != nullptr &&
         std::strstr(snippet, "[[user(") != nullptr &&
         std::strstr(snippet, "struct Varyings") == nullptr;
}

bool UsesVaryingsStruct(const char* snippet) {
  if (snippet == nullptr) return false;
  return std::strstr(snippet, "struct Varyings") != nullptr &&
         std::strstr(snippet, "[[user(locn0)]]") != nullptr;
}

}  // namespace msl
}  // namespace tgles
