#ifndef TGLES_PIXELS_H
#define TGLES_PIXELS_H

// Pixel storage and readback validation (spec 8.4 + ReadPixels):
// PACK/UNPACK store state plus bounds/format-checked ReadPixels and
// CopyTexSubImage against explicit framebuffer extents (the facade passes
// the real READ drawable size; the state layer does the math).

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

// Pixel-store parameters (core set from gl32.h).
inline constexpr GLenum kGlPackAlignment = 0x0D05;
inline constexpr GLenum kGlUnpackAlignment = 0x0CF5;
inline constexpr GLenum kGlPackRowLength = 0x0D02;
inline constexpr GLenum kGlPackSkipRows = 0x0D03;
inline constexpr GLenum kGlPackSkipPixels = 0x0D04;
inline constexpr GLenum kGlUnpackRowLength = 0x0CF2;
inline constexpr GLenum kGlUnpackSkipRows = 0x0CF3;
inline constexpr GLenum kGlUnpackSkipPixels = 0x0CF4;
inline constexpr GLenum kGlUnpackSkipImages = 0x806D;
inline constexpr GLenum kGlUnpackImageHeight = 0x806E;

// ReadPixels formats/types (core subset).
inline constexpr GLenum kGlRgbaFormat = 0x1908;
inline constexpr GLenum kGlRgbFormat = 0x1907;
inline constexpr GLenum kGlRedFormat = 0x1903;
inline constexpr GLenum kGlRgFormat = 0x8227;
inline constexpr GLenum kGlDepthComponentFormat = 0x1902;
inline constexpr GLenum kGlUnsignedByteType = 0x1401;
inline constexpr GLenum kGlFloatType = 0x1406;
inline constexpr GLenum kGlUnsignedIntType = 0x1405;

class PixelState {
 public:
  PixelState();

  GLenum GetError();
  bool HasPending() const;

  void PixelStorei(GLenum pname, GLint param);
  void GetPixelStorei(GLenum pname, GLint* param);

  // Validates a ReadPixels against explicit READ-drawable extents.
  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, GLsizei fb_width,
                  GLsizei fb_height, bool float_buffer);

  // Row stride in bytes for a pack/unpack transfer (PACK state).
  GLsizei PackRowStride(GLsizei width, GLsizei bytes_per_pixel) const;

 private:
  static bool IsPixelStore(GLenum pname);
  static bool IsReadFormat(GLenum format);

  ErrorQueue errors_;
  GLint pack_alignment_ = 4;
  GLint unpack_alignment_ = 4;
  GLint pack_row_length_ = 0;
  GLint pack_skip_rows_ = 0;
  GLint pack_skip_pixels_ = 0;
  GLint unpack_row_length_ = 0;
  GLint unpack_skip_rows_ = 0;
  GLint unpack_skip_pixels_ = 0;
  GLint unpack_skip_images_ = 0;
  GLint unpack_image_height_ = 0;
};

}  // namespace tgles

#endif  // TGLES_PIXELS_H
