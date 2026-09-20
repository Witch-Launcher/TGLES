#ifndef TGLES_TESSELLATION_H
#define TGLES_TESSELLATION_H

// Tessellation stage state: patch size, limits and primitive helpers.

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

inline constexpr GLenum kGlPatches = 0x000E;
inline constexpr GLenum kGlPatchVertices = 0x8E72;
inline constexpr GLint kMaxPatchVertices = 32;  // Spec minimum.
inline constexpr GLint kMaxTessGenLevel = 64;   // Spec minimum.

class TessellationState {
 public:
  TessellationState();

  GLenum GetError();
  bool HasPending() const;

  void PatchParameteri(GLenum pname, GLint value);
  void GetPatchParameteriv(GLenum pname, GLint* params);
  GLint PatchSize() const;

 private:
  ErrorQueue errors_;
  GLint patch_vertices_ = 3;  // Spec initial value.
};

}  // namespace tgles

#endif  // TGLES_TESSELLATION_H
