#include "tgles/state/pixels.h"

namespace tgles {

PixelState::PixelState() = default;

GLenum PixelState::GetError() { return errors_.Get(); }
bool PixelState::HasPending() const { return errors_.HasPending(); }

bool PixelState::IsPixelStore(GLenum pname) {
  switch (pname) {
    case kGlPackAlignment:
    case kGlUnpackAlignment:
    case kGlPackRowLength:
    case kGlPackSkipRows:
    case kGlPackSkipPixels:
    case kGlUnpackRowLength:
    case kGlUnpackSkipRows:
    case kGlUnpackSkipPixels:
    case kGlUnpackSkipImages:
    case kGlUnpackImageHeight:
      return true;
    default:
      return false;
  }
}

bool PixelState::IsReadFormat(GLenum format) {
  switch (format) {
    case kGlRgbaFormat:
    case kGlRgbFormat:
    case kGlRedFormat:
    case kGlRgFormat:
    case kGlDepthComponentFormat:
      return true;
    default:
      return false;
  }
}

void PixelState::PixelStorei(GLenum pname, GLint param) {
  if (!IsPixelStore(pname)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (pname == kGlPackAlignment || pname == kGlUnpackAlignment) {
    if (param != 1 && param != 2 && param != 4 && param != 8) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    if (pname == kGlPackAlignment) {
      pack_alignment_ = param;
    } else {
      unpack_alignment_ = param;
    }
    return;
  }
  if (param < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  switch (pname) {
    case kGlPackRowLength:
      pack_row_length_ = param;
      break;
    case kGlPackSkipRows:
      pack_skip_rows_ = param;
      break;
    case kGlPackSkipPixels:
      pack_skip_pixels_ = param;
      break;
    case kGlUnpackRowLength:
      unpack_row_length_ = param;
      break;
    case kGlUnpackSkipRows:
      unpack_skip_rows_ = param;
      break;
    case kGlUnpackSkipPixels:
      unpack_skip_pixels_ = param;
      break;
    case kGlUnpackSkipImages:
      unpack_skip_images_ = param;
      break;
    case kGlUnpackImageHeight:
      unpack_image_height_ = param;
      break;
    default:
      break;
  }
}

void PixelState::GetPixelStorei(GLenum pname, GLint* param) {
  if (param == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  switch (pname) {
    case kGlPackAlignment:
      *param = pack_alignment_;
      return;
    case kGlUnpackAlignment:
      *param = unpack_alignment_;
      return;
    case kGlPackRowLength:
      *param = pack_row_length_;
      return;
    case kGlPackSkipRows:
      *param = pack_skip_rows_;
      return;
    case kGlPackSkipPixels:
      *param = pack_skip_pixels_;
      return;
    case kGlUnpackRowLength:
      *param = unpack_row_length_;
      return;
    case kGlUnpackSkipRows:
      *param = unpack_skip_rows_;
      return;
    case kGlUnpackSkipPixels:
      *param = unpack_skip_pixels_;
      return;
    case kGlUnpackSkipImages:
      *param = unpack_skip_images_;
      return;
    case kGlUnpackImageHeight:
      *param = unpack_image_height_;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void PixelState::ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                            GLenum format, GLenum type, GLsizei fb_width,
                            GLsizei fb_height, bool float_buffer) {
  if (!IsReadFormat(format)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (width < 0 || height < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (x < 0 || y < 0 || x + width > fb_width || y + height > fb_height) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Format/type compatibility with the READ buffer class.
  if (format == kGlDepthComponentFormat) {
    if (type != kGlUnsignedIntType && type != kGlFloatType) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
  } else {
    if (type == kGlFloatType && !float_buffer) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (type != kGlUnsignedByteType && type != kGlFloatType) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
  }
}

GLsizei PixelState::PackRowStride(GLsizei width,
                                  GLsizei bytes_per_pixel) const {
  const GLsizei effective =
      pack_row_length_ > 0 ? pack_row_length_ : width;
  const GLsizei raw = effective * bytes_per_pixel;
  const GLsizei align = pack_alignment_;
  return (raw + align - 1) / align * align;
}

}  // namespace tgles
