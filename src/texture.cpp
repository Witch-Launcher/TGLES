#include "tgles/texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "tgles/vertex_array.h"  // kGlUnsignedByte pixel type.

namespace tgles {

bool GlIsColorRenderable(GLenum internalformat) {
  // Mirrors TextureManager::IsColorRenderableSized (kept free so the FBO
  // module does not need friendship into TextureManager internals).
  switch (internalformat) {
    case kGlRgba8:
    case kGlRgb565:
    case kGlRgb8:
    case kGlRgba4:
    case kGlRgb5A1:
    case kGlRgb10A2:
    case kGlR8:
    case kGlRg8:
    case kGlR16f:
    case kGlRgba16f:
    case kGlR32f:
    case kGlRgba32f:
    case kGlSrgb8Alpha8:
      return true;
    default:
      return false;
  }
}

bool GlIsDepthFormat(GLenum internalformat) {
  return internalformat == kGlDepthComponent16 ||
         internalformat == kGlDepthComponent24 ||
         internalformat == kGlDepthComponent32f;
}

bool GlIsDepthStencilFormat(GLenum internalformat) {
  return internalformat == kGlDepth24Stencil8;
}

TextureManager::TextureManager() = default;

GLenum TextureManager::GetError() { return errors_.Get(); }
bool TextureManager::HasPending() const { return errors_.HasPending(); }

bool TextureManager::IsBindTarget(GLenum target) {
  switch (target) {
    case kGlTexture2d:
    case kGlTexture3d:
    case kGlTextureCubeMap:
    case kGlTexture2dArray:
    case kGlTextureCubeMapArray:
    case kGlTexture2dMultisample:
    case kGlTexture2dMultisampleArray:
    case kGlTextureBuffer:
      return true;
    default:
      return false;
  }
}

bool TextureManager::IsCubeFace(GLenum target) {
  return target >= kGlTextureCubeMapPositiveX &&
         target < kGlTextureCubeMapPositiveX + 6;
}

bool TextureManager::IsSizedFormat(GLenum internalformat) {
  switch (internalformat) {
    case kGlRgba8:
    case kGlRgb565:
    case kGlRgb8:
    case kGlRgba4:
    case kGlRgb5A1:
    case kGlRgb10A2:
    case kGlR8:
    case kGlRg8:
    case kGlR16f:
    case kGlRgba16f:
    case kGlR32f:
    case kGlRgba32f:
    case kGlDepthComponent16:
    case kGlDepthComponent24:
    case kGlDepthComponent32f:
    case kGlDepth24Stencil8:
    case kGlSrgb8Alpha8:
      return true;
    default:
      return false;
  }
}

bool TextureManager::IsCompressedFormat(GLenum internalformat) {
  switch (internalformat) {
    case kGlEtc2Rgb8:
    case kGlEacR11:
    case kGlCompressedRgbaAstc4x4:
      return true;
    default:
      return false;
  }
}

bool TextureManager::IsColorRenderableSized(GLenum internalformat) {
  return IsSizedFormat(internalformat) &&
         internalformat != kGlDepthComponent16 &&
         internalformat != kGlDepthComponent24 &&
         internalformat != kGlDepthComponent32f &&
         internalformat != kGlDepth24Stencil8;
}

GLsizei TextureManager::MaxDimension(GLenum target) {
  switch (target) {
    case kGlTexture3d:
      return kMax3dTextureSizeValue;
    case kGlTextureCubeMap:
    case kGlTextureCubeMapPositiveX:
      return kMaxCubeMapTextureSizeValue;
    default:
      return kMaxTextureSizeValue;
  }
}

TextureManager::Texture* TextureManager::Find(GLuint texture) {
  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.alive) return nullptr;
  return &it->second;
}

TextureManager::Texture* TextureManager::BoundForWrite(GLenum target,
                                                       bool cube_face_ok) {
  GLenum key = target;
  if (cube_face_ok && IsCubeFace(target)) key = kGlTextureCubeMap;
  if (!IsBindTarget(key)) {
    errors_.Record(kGlInvalidEnum);
    return nullptr;
  }
  auto it = unit_bindings_[active_unit_].find(key);
  if (it == unit_bindings_[active_unit_].end() || it->second == 0) {
    errors_.Record(kGlInvalidOperation);  // No texture bound.
    return nullptr;
  }
  Texture* tex = Find(it->second);
  if (tex == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return nullptr;
  }
  return tex;
}

void TextureManager::GenTextures(GLsizei n, GLuint* textures) {
  if (n < 0 || (n > 0 && textures == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    textures[i] = next_name_++;
    textures_[textures[i]] = Texture();
  }
}

void TextureManager::DeleteTextures(GLsizei n, const GLuint* textures) {
  if (n < 0 || (n > 0 && textures == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    if (textures[i] == 0) continue;
    textures_.erase(textures[i]);
    for (auto& unit : unit_bindings_) {
      for (auto& kv : unit) {
        if (kv.second == textures[i]) kv.second = 0;
      }
    }
  }
}

GLboolean TextureManager::IsTexture(GLuint texture) {
  if (texture == 0) return kGlFalse;
  return Find(texture) != nullptr ? kGlTrue : kGlFalse;
}

void TextureManager::ActiveTexture(GLenum texture) {
  if (texture < 0x84C0u ||
      texture >= 0x84C0u + static_cast<GLenum>(kMaxCombinedTextureImageUnits)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  active_unit_ = texture - 0x84C0u;
}

void TextureManager::BindTexture(GLenum target, GLuint texture) {
  if (!IsBindTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (texture != 0) {
    auto it = textures_.find(texture);
    if (it == textures_.end()) {
      textures_[texture] = Texture();
      it = textures_.find(texture);
    }
    if (!it->second.alive) {
      it->second.alive = true;
      it->second.target = target;
    } else if (it->second.target != 0 && it->second.target != target) {
      // Re-binding an object to a different target is allowed; the object
      // adopts the new target only if never given one? Per spec, a texture
      // object has one target after first bind; mismatched binds are errors
      // only for proxy... In ES, binding to another target is INVALID_OPERATION.
      errors_.Record(kGlInvalidOperation);
      return;
    } else {
      it->second.target = target;
    }
  }
  unit_bindings_[active_unit_][target] = texture;
}

void TextureManager::TexStorage2D(GLenum target, GLsizei levels,
                                  GLenum internalformat, GLsizei width,
                                  GLsizei height) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (tex->immutable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (!IsSizedFormat(internalformat) && !IsCompressedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (levels < 1 || width < 1 || height < 1 ||
      width > MaxDimension(target) || height > MaxDimension(target)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const GLsizei max_levels = static_cast<GLsizei>(
                                 std::log2(static_cast<double>(
                                     std::max(width, height)))) +
                             1;
  if (levels > max_levels) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  tex->immutable = true;
  tex->images.clear();
  GLenum face = (target == kGlTextureCubeMap) ? kGlTextureCubeMapPositiveX
                                              : target;
  const int faces = (target == kGlTextureCubeMap) ? 6 : 1;
  for (int f = 0; f < faces; ++f) {
    for (GLsizei l = 0; l < levels; ++l) {
      TextureLevel lvl;
      lvl.defined = true;
      lvl.internalformat = internalformat;
      lvl.width = std::max<GLsizei>(1, width >> l);
      lvl.height = std::max<GLsizei>(1, height >> l);
      tex->images[face + f][l] = lvl;
    }
  }
}

void TextureManager::TexStorage3D(GLenum target, GLsizei levels,
                                  GLenum internalformat, GLsizei width,
                                  GLsizei height, GLsizei depth) {
  Texture* tex = BoundForWrite(target, false);
  if (tex == nullptr) return;
  if (target != kGlTexture3d && target != kGlTexture2dArray &&
      target != kGlTextureCubeMapArray) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (tex->immutable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (!IsSizedFormat(internalformat) && !IsCompressedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  const GLsizei max_dim = (target == kGlTexture3d) ? kMax3dTextureSizeValue
                                                  : kMaxTextureSizeValue;
  if (levels < 1 || width < 1 || height < 1 || depth < 1 || width > max_dim ||
      height > max_dim || depth > kMaxArrayTextureLayersValue) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  tex->immutable = true;
  tex->images.clear();
  for (GLsizei l = 0; l < levels; ++l) {
    TextureLevel lvl;
    lvl.defined = true;
    lvl.internalformat = internalformat;
    lvl.width = std::max<GLsizei>(1, width >> l);
    lvl.height = std::max<GLsizei>(1, height >> l);
    lvl.depth = depth;
    tex->images[target][l] = lvl;
  }
}

void TextureManager::TexImage2D(GLenum target, GLint level, GLint internalformat,
                                GLsizei width, GLsizei height, GLint border,
                                GLenum format, GLenum type, const void* pixels) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (border != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (tex->immutable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const bool sized = IsSizedFormat(static_cast<GLenum>(internalformat));
  const bool unsized =
      internalformat == static_cast<GLint>(kGlRgb) ||
      internalformat == static_cast<GLint>(kGlRgba) ||
      internalformat == static_cast<GLint>(kGlRed) ||
      internalformat == static_cast<GLint>(kGlRg) ||
      internalformat == static_cast<GLint>(kGlDepthComponent) ||
      internalformat == static_cast<GLint>(kGlDepthStencil);
  if (!sized && !unsized) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (level < 0 || width < 0 || height < 0 || width > MaxDimension(target) ||
      height > MaxDimension(target)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  GLenum face = target;
  if (IsCubeFace(target)) face = target;  // Per-face storage.
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = static_cast<GLenum>(internalformat);
  lvl.width = width;
  lvl.height = height;
  // Spec: storage is allocated even when pixels is NULL (contents then
  // undefined). CTS CopyImage uploads real data, but a null upload must
  // still leave a defined, sized level behind.
  if (width > 0 && height > 0) {
    lvl.pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
    if (pixels != nullptr) {
      std::memcpy(lvl.pixels.data(), pixels,
                  std::min<std::size_t>(lvl.pixels.size(), 64));
    }
  }
  tex->images[face][level] = lvl;
}

void TextureManager::TexImage3D(GLenum target, GLint level,
                                GLint internalformat, GLsizei width,
                                GLsizei height, GLsizei depth, GLint border,
                                GLenum format, GLenum type,
                                const void* pixels) {
  Texture* tex = BoundForWrite(target, false);
  if (tex == nullptr) return;
  if (target != kGlTexture3d && target != kGlTexture2dArray) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (border != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (tex->immutable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const bool sized = IsSizedFormat(static_cast<GLenum>(internalformat));
  const bool unsized =
      internalformat == static_cast<GLint>(kGlRgb) ||
      internalformat == static_cast<GLint>(kGlRgba) ||
      internalformat == static_cast<GLint>(kGlRed) ||
      internalformat == static_cast<GLint>(kGlRg) ||
      internalformat == static_cast<GLint>(kGlDepthComponent) ||
      internalformat == static_cast<GLint>(kGlDepthStencil);
  if (!sized && !unsized) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  const GLsizei max_dim =
      (target == kGlTexture3d) ? kMax3dTextureSizeValue : kMaxTextureSizeValue;
  if (level < 0 || width < 0 || height < 0 || depth < 0 ||
      width > max_dim || height > max_dim ||
      depth > kMaxArrayTextureLayersValue) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = static_cast<GLenum>(internalformat);
  lvl.width = width;
  lvl.height = height;
  lvl.depth = depth;
  if (width > 0 && height > 0 && depth > 0) {
    lvl.pixels.assign(
        static_cast<std::size_t>(width) * height * depth * 4, 0);
    if (pixels != nullptr) {
      std::memcpy(lvl.pixels.data(), pixels,
                  std::min<std::size_t>(lvl.pixels.size(), 64));
    }
  }
  tex->images[target][level] = lvl;
  (void)format;
  (void)type;
}

void TextureManager::TexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                   GLint yoffset, GLsizei width, GLsizei height,
                                   GLenum format, GLenum type,
                                   const void* pixels) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  auto fit = tex->images.find(IsCubeFace(target) ? target : target);
  if (fit == tex->images.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || !lit->second.defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const TextureLevel& lvl = lit->second;
  if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 ||
      xoffset + width > lvl.width || yoffset + height > lvl.height) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  (void)format;
  (void)type;
  (void)pixels;
}

void TextureManager::TexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                   GLint yoffset, GLint zoffset, GLsizei width,
                                   GLsizei height, GLsizei depth, GLenum format,
                                   GLenum type, const void* pixels) {
  Texture* tex = BoundForWrite(target, false);
  if (tex == nullptr) return;
  if (target != kGlTexture3d && target != kGlTexture2dArray) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  auto fit = tex->images.find(target);
  if (fit == tex->images.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || !lit->second.defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const TextureLevel& lvl = lit->second;
  if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 ||
      depth < 0 || xoffset + width > lvl.width ||
      yoffset + height > lvl.height || zoffset + depth > lvl.depth) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  (void)format;
  (void)type;
  (void)pixels;
}

void TextureManager::CopyImageSubData(GLuint src_name, GLenum src_target,
                                        GLint src_level, GLint src_x,
                                        GLint src_y, GLint src_z,
                                        GLuint dst_name, GLenum dst_target,
                                        GLint dst_level, GLint dst_x,
                                        GLint dst_y, GLint dst_z,
                                        GLsizei width, GLsizei height,
                                        GLsizei depth) {
  // State-level glCopyImageSubData for 2D/cube-face/3D/array images (CTS
  // CopyImage group): equal-format row copies on the RGBA8-unpacked store
  // (w*h*4 for 2D faces, w*h*d*4 for 3D/array levels). Compressed formats
  // report INVALID_OPERATION.
  const bool src_ok = src_target == kGlTexture2d ||
                      src_target == kGlTexture3d ||
                      src_target == kGlTexture2dArray ||
                      IsCubeFace(src_target);
  const bool dst_ok = dst_target == kGlTexture2d ||
                      dst_target == kGlTexture3d ||
                      dst_target == kGlTexture2dArray ||
                      IsCubeFace(dst_target);
  if (!src_ok || !dst_ok) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  Texture* src = Find(src_name);
  Texture* dst = Find(dst_name);
  if (src == nullptr || !src->alive || dst == nullptr || !dst->alive) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (src_level < 0 || dst_level < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (width <= 0 || height <= 0 || depth <= 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const bool src_3d =
      src_target == kGlTexture3d || src_target == kGlTexture2dArray;
  const bool dst_3d =
      dst_target == kGlTexture3d || dst_target == kGlTexture2dArray;
  // 2D faces carry a single slice; 3D/array levels carry lvl.depth slices.
  if ((!src_3d && (src_z != 0 || depth != 1)) ||
      (!dst_3d && (dst_z != 0 || depth != 1))) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (src_z < 0 || dst_z < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const GLenum src_face = src_target;
  const GLenum dst_face = dst_target;
  auto sfit = src->images.find(src_face);
  auto dfit = dst->images.find(dst_face);
  if (sfit == src->images.end() || dfit == dst->images.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto slit = sfit->second.find(src_level);
  auto dlit = dfit->second.find(dst_level);
  if (slit == sfit->second.end() || !slit->second.defined ||
      dlit == dfit->second.end() || !dlit->second.defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const TextureLevel& sl = slit->second;
  TextureLevel& dl = dlit->second;
  if (sl.internalformat != dl.internalformat) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (IsCompressedFormat(sl.internalformat)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  // 2D faces carry a single slice (depth field stays 0); 3D/array levels
  // carry lvl.depth slices. Normalize both before bounds checks.
  const GLsizei src_depth = src_3d ? sl.depth : 1;
  const GLsizei dst_depth = dst_3d ? dl.depth : 1;
  if (src_x < 0 || src_y < 0 || dst_x < 0 || dst_y < 0 ||
      src_x + width > sl.width || src_y + height > sl.height ||
      dst_x + width > dl.width || dst_y + height > dl.height ||
      src_z + depth > src_depth || dst_z + depth > dst_depth) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  constexpr std::size_t kBpp = 4;  // RGBA8-unpacked store (see TexImage2D).
  for (GLsizei slice = 0; slice < depth; ++slice) {
    const std::size_t s_plane =
        src_3d ? static_cast<std::size_t>(src_z + slice) *
                     static_cast<std::size_t>(sl.width) * sl.height
               : 0;
    const std::size_t d_plane =
        dst_3d ? static_cast<std::size_t>(dst_z + slice) *
                     static_cast<std::size_t>(dl.width) * dl.height
               : 0;
    for (GLsizei row = 0; row < height; ++row) {
      const std::size_t s =
          (s_plane + static_cast<std::size_t>(src_y + row) *
                         static_cast<std::size_t>(sl.width) +
           static_cast<std::size_t>(src_x)) *
          kBpp;
      const std::size_t d =
          (d_plane + static_cast<std::size_t>(dst_y + row) *
                         static_cast<std::size_t>(dl.width) +
           static_cast<std::size_t>(dst_x)) *
          kBpp;
      if (s + static_cast<std::size_t>(width) * kBpp > sl.pixels.size() ||
          d + static_cast<std::size_t>(width) * kBpp > dl.pixels.size()) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
      std::memcpy(dl.pixels.data() + d, sl.pixels.data() + s,
                  static_cast<std::size_t>(width) * kBpp);
    }
  }
}

void TextureManager::CopyTexSubImage2D(GLenum target, GLint level,
                                       GLint xoffset, GLint yoffset, GLint x,
                                       GLint y, GLsizei width, GLsizei height) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  // Source-rectangle checks against the READ drawable belong to the facade
  // (which knows its size); the state layer validates the destination.
  if (x < 0 || y < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  TexSubImage2D(target, level, xoffset, yoffset, width, height, kGlRgba,
                kGlUnsignedByte, nullptr);
}

void TextureManager::CompressedTexImage2D(GLenum target, GLint level,
                                          GLenum internalformat, GLsizei width,
                                          GLsizei height, GLint border,
                                          GLsizei image_size, const void* data) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (border != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (tex->immutable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (!IsCompressedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (level < 0 || width < 0 || height < 0 || image_size < 0 ||
      (image_size > 0 && data == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (internalformat == kGlCompressedRgbaAstc4x4 &&
      (width % 4 != 0 || height % 4 != 0) && image_size > 0) {
    errors_.Record(kGlInvalidValue);  // ASTC 4x4 needs block alignment.
    return;
  }
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = internalformat;
  lvl.width = width;
  lvl.height = height;
  if (data != nullptr && image_size > 0) {
    lvl.pixels.assign(static_cast<const std::uint8_t*>(data),
                      static_cast<const std::uint8_t*>(data) + image_size);
  }
  tex->images[IsCubeFace(target) ? target : target][level] = lvl;
}

void TextureManager::TexBuffer(GLenum target, GLenum internalformat,
                               GLuint buffer) {
  if (target != kGlTextureBuffer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  Texture* tex = BoundForWrite(target, false);
  if (tex == nullptr) return;
  if (!IsSizedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  tex->buffer = buffer;
  tex->buffer_format = internalformat;
  tex->buffer_offset = 0;
  tex->buffer_size = -1;
}

void TextureManager::TexBufferRange(GLenum target, GLenum internalformat,
                                    GLuint buffer, GLintptr offset,
                                    GLsizeiptr size) {
  if (target != kGlTextureBuffer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  Texture* tex = BoundForWrite(target, false);
  if (tex == nullptr) return;
  if (!IsSizedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (buffer == 0) {  // Detach, like TexBuffer(0).
    tex->buffer = 0;
    tex->buffer_format = 0;
    tex->buffer_offset = 0;
    tex->buffer_size = -1;
    return;
  }
  if (offset < 0 || size <= 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (offset % kTextureBufferOffsetAlignmentValue != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Bounds against the buffer store are enforced at draw/dispatch time by
  // the facade (which owns the BufferManager); the state layer records.
  tex->buffer = buffer;
  tex->buffer_format = internalformat;
  tex->buffer_offset = offset;
  tex->buffer_size = size;
}

void TextureManager::TexParameteri(GLenum target, GLenum pname, GLint param) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  TextureParams& p = tex->params;
  switch (pname) {
    case kGlTextureMinFilter:
      switch (param) {
        case kGlNearest:
        case kGlLinear:
        case kGlNearestMipmapNearest:
        case kGlLinearMipmapNearest:
        case kGlNearestMipmapLinear:
        case kGlLinearMipmapLinear:
          p.min_filter = static_cast<GLenum>(param);
          return;
        default:
          break;
      }
      errors_.Record(kGlInvalidEnum);
      return;
    case kGlTextureMagFilter:
      if (param == static_cast<GLint>(kGlNearest) ||
          param == static_cast<GLint>(kGlLinear)) {
        p.mag_filter = static_cast<GLenum>(param);
        return;
      }
      errors_.Record(kGlInvalidEnum);
      return;
    case kGlTextureWrapS:
    case kGlTextureWrapT:
    case kGlTextureWrapR:
      if (param == static_cast<GLint>(kGlClampToEdge) ||
          param == static_cast<GLint>(kGlRepeat) ||
          param == static_cast<GLint>(kGlMirroredRepeat) ||
          param == static_cast<GLint>(kGlClampToBorder)) {
        if (pname == kGlTextureWrapS) p.wrap_s = static_cast<GLenum>(param);
        if (pname == kGlTextureWrapT) p.wrap_t = static_cast<GLenum>(param);
        if (pname == kGlTextureWrapR) p.wrap_r = static_cast<GLenum>(param);
        return;
      }
      errors_.Record(kGlInvalidEnum);
      return;
    case kGlTextureBaseLevel:
    case kGlTextureMaxLevel:
      if (param < 0) {
        errors_.Record(kGlInvalidValue);
        return;
      }
      if (pname == kGlTextureBaseLevel) p.base_level = param;
      if (pname == kGlTextureMaxLevel) p.max_level = param;
      return;
    case kGlTextureCompareMode:
      if (param == static_cast<GLint>(kGlNone) ||
          param == static_cast<GLint>(kGlCompareRefToTexture)) {
        p.compare_mode = static_cast<GLenum>(param);
        return;
      }
      errors_.Record(kGlInvalidEnum);
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void TextureManager::TexParameterf(GLenum target, GLenum pname, GLfloat param) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (pname == kGlTextureMinLod) {
    tex->params.min_lod = param;
    return;
  }
  if (pname == kGlTextureMaxLod) {
    tex->params.max_lod = param;
    return;
  }
  // Integer-valued params also accept float entry points.
  TexParameteri(target, pname, static_cast<GLint>(param));
}

void TextureManager::TexParameterfv(GLenum target, GLenum pname,
                                    const GLfloat* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (pname == kGlTextureBorderColor) {
    for (int i = 0; i < 4; ++i) tex->params.border_color[i] = params[i];
    return;
  }
  // Scalar float params share the float entry point.
  TexParameterf(target, pname, params[0]);
}

void TextureManager::GenerateMipmap(GLenum target) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (target == kGlTexture2dMultisample ||
      target == kGlTexture2dMultisampleArray || target == kGlTextureBuffer) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto it = tex->images.find(target == kGlTextureCubeMap
                                 ? kGlTextureCubeMapPositiveX
                                 : target);
  if (it == tex->images.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  // Mark that mipmaps were generated (level 0 must exist).
  if (it->second.find(0) == it->second.end()) {
    errors_.Record(kGlInvalidOperation);
  }
}

void TextureManager::GetTexParameteriv(GLenum target, GLenum pname,
                                       GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  const TextureParams& p = tex->params;
  switch (pname) {
    case kGlTextureMinFilter:
      *params = static_cast<GLint>(p.min_filter);
      return;
    case kGlTextureMagFilter:
      *params = static_cast<GLint>(p.mag_filter);
      return;
    case kGlTextureWrapS:
      *params = static_cast<GLint>(p.wrap_s);
      return;
    case kGlTextureWrapT:
      *params = static_cast<GLint>(p.wrap_t);
      return;
    case kGlTextureWrapR:
      *params = static_cast<GLint>(p.wrap_r);
      return;
    case kGlTextureImmutableFormat:
      *params = tex->immutable ? 1 : 0;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void TextureManager::GetTexParameterfv(GLenum target, GLenum pname,
                                       GLfloat* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  const TextureParams& p = tex->params;
  if (pname == kGlTextureBorderColor) {
    for (int i = 0; i < 4; ++i) params[i] = p.border_color[i];
    return;
  }
  if (pname == kGlTextureMinLod) {
    params[0] = p.min_lod;
    return;
  }
  if (pname == kGlTextureMaxLod) {
    params[0] = p.max_lod;
    return;
  }
  // Scalar params share the float getter via their int values.
  GLint v = 0;
  GetTexParameteriv(target, pname, &v);
  if (HasPending()) return;
  params[0] = static_cast<GLfloat>(v);
}

bool TextureManager::IsImmutable(GLuint texture) const {
  auto it = textures_.find(texture);
  return it != textures_.end() && it->second.alive && it->second.immutable;
}

TextureLevel TextureManager::LevelState(GLuint texture, GLenum face_target,
                                        GLint level) const {
  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.alive) return TextureLevel();
  auto fit = it->second.images.find(face_target);
  if (fit == it->second.images.end()) return TextureLevel();
  auto lit = fit->second.find(level);
  return lit == fit->second.end() ? TextureLevel() : lit->second;
}

GLuint TextureManager::BoundTexture(GLenum target) const {
  auto it = unit_bindings_[active_unit_].find(target);
  return it == unit_bindings_[active_unit_].end() ? 0 : it->second;
}

GLuint TextureManager::ActiveUnit() const { return active_unit_; }

}  // namespace tgles
