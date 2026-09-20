#ifndef TGLES_TEXTURE_H
#define TGLES_TEXTURE_H

// Textures (spec chapter 8): targets, bindings per texture unit, mutable and
// immutable storage, sub-image updates, compressed images (ETC2/EAC/ASTC),
// buffer textures, parameters and mipmap generation.

#include <array>
#include <map>
#include <vector>

#include "tgles/buffer.h"  // kGlTextureBuffer target.
#include "tgles/error.h"
#include "tgles/gl_types.h"
namespace tgles {

// Texture targets.
inline constexpr GLenum kGlTexture2d = 0x0DE1;
inline constexpr GLenum kGlTexture3d = 0x806F;
inline constexpr GLenum kGlTextureCubeMap = 0x8513;
inline constexpr GLenum kGlTextureCubeMapPositiveX = 0x8515;
inline constexpr GLenum kGlTexture2dArray = 0x8C1A;
inline constexpr GLenum kGlTextureCubeMapArray = 0x9009;
inline constexpr GLenum kGlTexture2dMultisample = 0x9100;
inline constexpr GLenum kGlTexture2dMultisampleArray = 0x9102;
// NOTE: kGlTextureBuffer (0x8C2A) lives in buffer.h to avoid duplication.

// Sized internal formats (required subset, spec Tables 8.10-8.13).
inline constexpr GLenum kGlRgba8 = 0x8058;
inline constexpr GLenum kGlRgb565 = 0x8D62;
inline constexpr GLenum kGlRgb8 = 0x8051;
inline constexpr GLenum kGlRgba4 = 0x8056;
inline constexpr GLenum kGlRgb5A1 = 0x8057;
inline constexpr GLenum kGlRgb10A2 = 0x8059;
inline constexpr GLenum kGlR8 = 0x8229;
inline constexpr GLenum kGlRg8 = 0x822B;
inline constexpr GLenum kGlR16f = 0x822D;
inline constexpr GLenum kGlRgba16f = 0x881A;
inline constexpr GLenum kGlR32f = 0x822E;
inline constexpr GLenum kGlRgba32f = 0x8814;
inline constexpr GLenum kGlDepthComponent16 = 0x81A5;
inline constexpr GLenum kGlDepthComponent24 = 0x81A6;
inline constexpr GLenum kGlDepthComponent32f = 0x8CAC;
inline constexpr GLenum kGlDepth24Stencil8 = 0x88F0;
inline constexpr GLenum kGlSrgb8Alpha8 = 0x8C43;
inline constexpr GLenum kGlEtc2Rgb8 = 0x9274;
inline constexpr GLenum kGlEacR11 = 0x9270;
inline constexpr GLenum kGlCompressedRgbaAstc4x4 = 0x93B0;  // KHR ASTC LDR.

// Unsized base formats (TexImage only).
inline constexpr GLenum kGlRgb = 0x1907;
inline constexpr GLenum kGlRgba = 0x1908;
inline constexpr GLenum kGlRed = 0x1903;
inline constexpr GLenum kGlGreen = 0x1904;
inline constexpr GLenum kGlBlue = 0x1905;
inline constexpr GLenum kGlAlpha = 0x1906;
inline constexpr GLenum kGlRg = 0x8227;
inline constexpr GLenum kGlDepthComponent = 0x1902;
inline constexpr GLenum kGlDepthStencil = 0x84F9;

// Pixel types (generic UNSIGNED_BYTE/SHORT live in vertex_array.h).
inline constexpr GLenum kGlUnsignedShort565 = 0x8363;
inline constexpr GLenum kGlUnsignedShort4444 = 0x8033;
inline constexpr GLenum kGlUnsignedShort5551 = 0x8034;

// Texture parameters.
inline constexpr GLenum kGlTextureMinFilter = 0x2801;
inline constexpr GLenum kGlTextureMagFilter = 0x2800;
inline constexpr GLenum kGlTextureWrapS = 0x2802;
inline constexpr GLenum kGlTextureWrapT = 0x2803;
inline constexpr GLenum kGlTextureWrapR = 0x8072;
inline constexpr GLenum kGlTextureMinLod = 0x813A;
inline constexpr GLenum kGlTextureMaxLod = 0x813B;
inline constexpr GLenum kGlTextureBaseLevel = 0x813C;
inline constexpr GLenum kGlTextureMaxLevel = 0x813D;
inline constexpr GLenum kGlTextureCompareMode = 0x884C;
inline constexpr GLenum kGlTextureCompareFunc = 0x884D;
inline constexpr GLenum kGlTextureSwizzleR = 0x8E42;
inline constexpr GLenum kGlTextureSwizzleG = 0x8E43;
inline constexpr GLenum kGlTextureSwizzleB = 0x8E44;
inline constexpr GLenum kGlTextureSwizzleA = 0x8E45;
inline constexpr GLenum kGlTextureImmutableFormat = 0x912F;

// Filter / wrap modes.
inline constexpr GLenum kGlNearest = 0x2600;
inline constexpr GLenum kGlLinear = 0x2601;
inline constexpr GLenum kGlNearestMipmapNearest = 0x2700;
inline constexpr GLenum kGlLinearMipmapNearest = 0x2701;
inline constexpr GLenum kGlNearestMipmapLinear = 0x2702;
inline constexpr GLenum kGlLinearMipmapLinear = 0x2703;
inline constexpr GLenum kGlClampToEdge = 0x812F;
inline constexpr GLenum kGlRepeat = 0x2901;
inline constexpr GLenum kGlMirroredRepeat = 0x8370;
inline constexpr GLenum kGlClampToBorder = 0x812D;
inline constexpr GLenum kGlTextureBorderColor = 0x1004;
inline constexpr GLenum kGlNone = 0;
inline constexpr GLenum kGlCompareRefToTexture = 0x884E;

// Reporting limits (all >= spec minimums).
inline constexpr GLint kMaxTextureImageUnits = 16;
inline constexpr GLint kMaxVertexTextureImageUnits = 16;
inline constexpr GLint kMaxCombinedTextureImageUnits = 32;
inline constexpr GLint kMaxTextureSizeValue = 8192;       // Min 2048.
inline constexpr GLint kMaxCubeMapTextureSizeValue = 8192;  // Min 2048.
inline constexpr GLint kMax3dTextureSizeValue = 2048;       // Min 256.
inline constexpr GLint kMaxArrayTextureLayersValue = 2048;  // Min 256.
inline constexpr GLint kMaxSamplesValue = 4;                // Min 4.
inline constexpr GLint kMaxTextureBufferSizeValue = 131072;  // Min 65536.
// GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT (gl32.h 0x919F): TexBufferRange offset
// must be a multiple of this. TGL reports 1 (unconstrained), the same
// default MobileGL uses when the driver value is unavailable
// (BackendObject.h: "1 means the offset is unconstrained").
inline constexpr GLint kTextureBufferOffsetAlignmentValue = 1;

struct TextureLevel {
  bool defined = false;
  GLenum internalformat = 0;
  GLsizei width = 0;
  GLsizei height = 0;
  GLsizei depth = 0;
  GLsizei samples = 0;
  std::vector<std::uint8_t> pixels;
};

struct TextureParams {
  GLenum min_filter = kGlNearestMipmapLinear;
  GLenum mag_filter = kGlLinear;
  GLenum wrap_s = kGlClampToEdge;
  GLenum wrap_t = kGlClampToEdge;
  GLenum wrap_r = kGlClampToEdge;
  GLfloat min_lod = -1000.0f;
  GLfloat max_lod = 1000.0f;
  GLint base_level = 0;
  GLint max_level = 1000;
  GLenum compare_mode = kGlNone;
  GLenum compare_func = 0x0202;  // LEQUAL.
  GLenum swizzle[4] = {kGlRed, kGlGreen, kGlBlue, kGlAlpha};
  GLfloat border_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Format classification shared with the framebuffer module (spec 9.4).
// Color-renderable sized formats may back COLOR_ATTACHMENTs; depth/stencil
// formats may back DEPTH/STENCIL attachments.
bool GlIsColorRenderable(GLenum internalformat);
bool GlIsDepthFormat(GLenum internalformat);
bool GlIsDepthStencilFormat(GLenum internalformat);

class TextureManager {
 public:
  TextureManager();

  GLenum GetError();
  bool HasPending() const;

  void GenTextures(GLsizei n, GLuint* textures);
  void DeleteTextures(GLsizei n, const GLuint* textures);
  GLboolean IsTexture(GLuint texture);
  void ActiveTexture(GLenum texture);
  void BindTexture(GLenum target, GLuint texture);

  void TexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width, GLsizei height);
  void TexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width, GLsizei height, GLsizei depth);
  void TexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const void* pixels);
  void TexImage3D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum format, GLenum type, const void* pixels);
  void TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void* pixels);
  void TexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLint zoffset, GLsizei width, GLsizei height,
                     GLsizei depth, GLenum format, GLenum type,
                     const void* pixels);
  void CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                          GLint yoffset, GLint x, GLint y, GLsizei width,
                          GLsizei height);
  // State-level glCopyImageSubData for 2D/cube-face images (CTS CopyImage
  // group): equal-format row copies on the RGBA8-unpacked store. 3D/array
  // slices (srcZ/dstZ/depth != 0/0/1) report INVALID_VALUE; compressed
  // formats report INVALID_OPERATION.
  void CopyImageSubData(GLuint src_name, GLenum src_target, GLint src_level,
                        GLint src_x, GLint src_y, GLint src_z, GLuint dst_name,
                        GLenum dst_target, GLint dst_level, GLint dst_x,
                        GLint dst_y, GLint dst_z, GLsizei width,
                        GLsizei height, GLsizei depth);
  void CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei image_size, const void* data);
  void TexBuffer(GLenum target, GLenum internalformat, GLuint buffer);
  void TexBufferRange(GLenum target, GLenum internalformat, GLuint buffer,
                      GLintptr offset, GLsizeiptr size);
  void TexParameteri(GLenum target, GLenum pname, GLint param);
  void TexParameterf(GLenum target, GLenum pname, GLfloat param);
  void TexParameterfv(GLenum target, GLenum pname, const GLfloat* params);
  void GenerateMipmap(GLenum target);
  void GetTexParameteriv(GLenum target, GLenum pname, GLint* params);
  void GetTexParameterfv(GLenum target, GLenum pname, GLfloat* params);

  // Test helpers.
  bool IsImmutable(GLuint texture) const;
  TextureLevel LevelState(GLuint texture, GLenum face_target,
                          GLint level) const;
  GLuint BoundTexture(GLenum target) const;
  GLuint ActiveUnit() const;

  // Shared with framebuffer.cpp (public so the FBO module can classify).
  static bool IsColorRenderableSized(GLenum internalformat);

 private:
  struct Texture {
    bool alive = false;
    GLenum target = 0;  // 0 = never bound.
    bool immutable = false;
    TextureParams params;
    // face_target -> level -> image.
    std::map<GLenum, std::map<GLint, TextureLevel>> images;
    GLuint buffer = 0;
    GLenum buffer_format = 0;
    GLintptr buffer_offset = 0;
    GLsizeiptr buffer_size = -1;  // -1 means the whole buffer store.
  };

  static bool IsBindTarget(GLenum target);
  static bool IsCubeFace(GLenum target);
  static bool IsSizedFormat(GLenum internalformat);
  static bool IsCompressedFormat(GLenum internalformat);
  static GLsizei MaxDimension(GLenum target);
  Texture* BoundForWrite(GLenum target, bool cube_face_ok);
  Texture* Find(GLuint texture);

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, Texture> textures_;
  GLuint active_unit_ = 0;
  std::array<std::map<GLenum, GLuint>, kMaxCombinedTextureImageUnits>
      unit_bindings_;
};

}  // namespace tgles

#endif  // TGLES_TEXTURE_H
