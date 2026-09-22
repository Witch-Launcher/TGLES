// Tessellation stage state (spec 11.1): patch vertices and queries.
// Mirrors include/tgles/state/tessellation.h.

#include "tgles/state/tessellation.h"

namespace tgles {

TessellationState::TessellationState() = default;

GLenum TessellationState::GetError() { return errors_.Get(); }
bool TessellationState::HasPending() const { return errors_.HasPending(); }

void TessellationState::PatchParameteri(GLenum pname, GLint value) {
  if (pname != kGlPatchVertices) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (value < 1 || value > kMaxPatchVertices) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  patch_vertices_ = value;
}

void TessellationState::GetPatchParameteriv(GLenum pname, GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname != kGlPatchVertices) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  *params = patch_vertices_;
}

GLint TessellationState::PatchSize() const { return patch_vertices_; }

}  // namespace tgles
