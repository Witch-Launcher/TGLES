#include "tgles/image_units.h"

#include "tgles/texture.h"

namespace tgles {

ImageUnitManager::ImageUnitManager() = default;

GLenum ImageUnitManager::GetError() { return errors_.Get(); }
bool ImageUnitManager::HasPending() const { return errors_.HasPending(); }

bool ImageUnitManager::IsAccess(GLenum access) {
  return access == kGlReadOnly || access == kGlWriteOnly ||
         access == kGlReadWrite;
}

bool ImageUnitManager::IsImageFormat(GLenum format) {
  // Sized internal formats valid for image load/store (spec Table 8.22
  // subset; TGL accepts the color-renderable + float set it can back).
  switch (format) {
    case kGlRgba8:
    case kGlRgb565:
    case kGlRgba16f:
    case kGlRgba32f:
    case kGlR32f:
    case kGlR16f:
      return true;
    default:
      return false;
  }
}

void ImageUnitManager::BindImageTexture(GLuint unit, GLuint texture,
                                        GLint level, GLboolean layered,
                                        GLint layer, GLenum access,
                                        GLenum format) {
  if (unit >= static_cast<GLuint>(kMaxImageUnitsValue)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsAccess(access)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (texture != 0) {
    if (level < 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    if (layer < 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    if (!IsImageFormat(format)) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
  }
  ImageBinding& b = units_[unit];
  b.texture = texture;
  b.level = level;
  b.layered = layered;
  b.layer = layer;
  b.access = access;
  b.format = (texture == 0) ? 0 : format;
}

GLuint ImageUnitManager::BoundTexture(GLuint unit) const {
  if (unit >= static_cast<GLuint>(kMaxImageUnitsValue)) return 0;
  return units_[unit].texture;
}

ImageBinding ImageUnitManager::Binding(GLuint unit) const {
  if (unit >= static_cast<GLuint>(kMaxImageUnitsValue)) return ImageBinding();
  return units_[unit];
}

}  // namespace tgles
