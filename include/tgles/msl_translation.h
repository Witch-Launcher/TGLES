#ifndef TGLES_MSL_TRANSLATION_H
#define TGLES_MSL_TRANSLATION_H

namespace tgles {
namespace msl {

// Returns the CORRECTED MSL translation of the plan's triangle example.
// The plan's snippet used an invalid "thread float4& ... [[user(locn0)]]"
// function parameter. Valid MSL passes varyings via a struct.
const char* CorrectedTriangleVertexShader();

// Returns true when a candidate MSL snippet uses the invalid pattern.
bool UsesInvalidThreadUserVarying(const char* snippet);

// Returns true when a candidate MSL snippet declares a varyings struct
// with a [[user(locn0)]] member (the correct pattern).
bool UsesVaryingsStruct(const char* snippet);

}  // namespace msl
}  // namespace tgles

#endif  // TGLES_MSL_TRANSLATION_H
