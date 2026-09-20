#ifndef TGLES_IMAGE_UNITS_H
#define TGLES_IMAGE_UNITS_H

// Image units for compute shaders (spec 8.22 + glBindImageTexture):
// per-unit texture/level/layered/layer/access/format bindings.
// Core ES 3.1+ (gl32.h) and REQUIRED by MobileGL loader (INIT_GLES_FUNC);
// DirectGLES issues one glBindImageTexture per unit (DirectGLES.cpp:1706/1772).

#include <array>

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

// Access enums (gl32.h).
inline constexpr GLenum kGlReadOnly = 0x88B8;
inline constexpr GLenum kGlWriteOnly = 0x88B9;
inline constexpr GLenum kGlReadWrite = 0x88BA;
// Image query for limits.
inline constexpr GLenum kGlMaxImageUnits = 0x8F38;
// Minimum per ES 3.2 (MobileGL Loader.cpp defaults maxImageUnits=8 and queries
// GL_MAX_IMAGE_UNITS from the host).
inline constexpr GLint kMaxImageUnitsValue = 8;

struct ImageBinding {
  GLuint texture = 0;
  GLint level = 0;
  GLboolean layered = kGlFalse;
  GLint layer = 0;
  GLenum access = kGlReadOnly;
  GLenum format = 0;
};

class ImageUnitManager {
 public:
  ImageUnitManager();

  GLenum GetError();
  bool HasPending() const;

  void BindImageTexture(GLuint unit, GLuint texture, GLint level,
                        GLboolean layered, GLint layer, GLenum access,
                        GLenum format);
  GLuint BoundTexture(GLuint unit) const;
  ImageBinding Binding(GLuint unit) const;
  GLint MaxUnits() const { return kMaxImageUnitsValue; }

 private:
  static bool IsAccess(GLenum access);
  static bool IsImageFormat(GLenum format);

  ErrorQueue errors_;
  std::array<ImageBinding, kMaxImageUnitsValue> units_;
};

}  // namespace tgles

#endif  // TGLES_IMAGE_UNITS_H
