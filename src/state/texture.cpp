#include "tgles/state/texture.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <set>

#include "tgles/state/vertex_array.h"  // kGlUnsignedByte pixel type.

namespace tgles {

std::uint64_t NextTextureContentRevision() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

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

namespace {

// Pixel upload (ES 3.2 spec 8.5-8.6), in one place so TexImage* and
// TexSubImage* cannot disagree. The store is RGBA8-unpacked: every level keeps
// w*h*d*4 bytes regardless of the transfer format.
//
// Supported transfers (everything the test suite and the CTS RGBA8 paths use):
//   RGBA / UNSIGNED_BYTE -> verbatim copy
//   RGB  / UNSIGNED_BYTE -> expand, a = 255
//   RED  / UNSIGNED_BYTE -> (r, 0, 0, 255)
//   RG   / UNSIGNED_BYTE -> (r, g, 0, 255)
// Result codes: kOk (dst filled), kBadEnum (unknown format/type — caller
// records INVALID_ENUM), kNoUnpacker (legal combo with no converter yet —
// caller records INVALID_OPERATION instead of corrupting the level).
enum class UnpackResult { kOk, kBadEnum, kNoUnpacker };

UnpackResult UnpackToRgba8(GLenum format, GLenum type, const void* src,
                           std::size_t w, std::size_t h, std::size_t d,
                           std::vector<std::uint8_t>& dst) {
  if (type != kGlUnsignedByte) {
    // UNSIGNED_BYTE is the only transfer type with a converter. The remaining
    // core types (FLOAT, HALF_FLOAT, packed 4_4_4_4/5_5_5_1/5_6_5, ...) are
    // legal enums, so they are "no unpacker", not "bad enum".
    switch (type) {
      case 0x1406:  // FLOAT
      case 0x140B:  // HALF_FLOAT
      case 0x8033:  // UNSIGNED_SHORT_4_4_4_4
      case 0x8034:  // UNSIGNED_SHORT_5_5_5_1
      case 0x8363:  // UNSIGNED_SHORT_5_6_5
      case 0x84FA:  // UNSIGNED_INT_2_10_10_10_REV
      case 0x8368:  // UNSIGNED_INT_10F_11F_11F_REV
      case 0x8D61:  // UNSIGNED_INT_5_9_9_9_REV
      case 0x1401:  // UNSIGNED_BYTE (== kGlUnsignedByte, kept for clarity)
        break;
      default:
        return UnpackResult::kBadEnum;
    }
    if (type != kGlUnsignedByte) return UnpackResult::kNoUnpacker;
  }
  int src_channels = 0;
  switch (format) {
    case kGlRgba:
      src_channels = 4;
      break;
    case kGlRgb:
      src_channels = 3;
      break;
    case kGlRg:
      src_channels = 2;
      break;
    case kGlRed:
    case kGlDepthComponent:
      src_channels = 1;
      break;
    case kGlDepthStencil:
      // Legal transfer, packed layout, no converter yet — fail closed.
      return UnpackResult::kNoUnpacker;
    default:
      return UnpackResult::kBadEnum;
  }
  const std::uint8_t* s = static_cast<const std::uint8_t*>(src);
  dst.resize(w * h * d * 4);
  for (std::size_t i = 0, n = w * h * d; i < n; ++i) {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    if (src_channels >= 1) r = s[i * src_channels + 0];
    if (src_channels >= 2) g = s[i * src_channels + 1];
    if (src_channels >= 3) b = s[i * src_channels + 2];
    if (src_channels >= 4) a = s[i * src_channels + 3];
    dst[i * 4 + 0] = r;
    dst[i * 4 + 1] = g;
    dst[i * 4 + 2] = b;
    dst[i * 4 + 3] = a;
  }
  return UnpackResult::kOk;
}

}  // namespace

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

// Texel byte size for view-compatibility (EXT_texture_view classes).
// Unknown formats report -1: views onto them fail closed.
GLsizei TextureViewFormatBytes(GLenum internalformat) {
  switch (internalformat) {
    case kGlRgba8:
    case kGlRgb10A2:
    case kGlSrgb8Alpha8:
    case kGlDepth24Stencil8:
      return 4;
    case kGlRgb8:
    case kGlDepthComponent24:
      return 3;
    case kGlRgb565:
    case kGlRgba4:
    case kGlRgb5A1:
    case kGlRg8:
    case kGlR16f:
    case kGlDepthComponent16:
      return 2;
    case kGlR8:
      return 1;
    case kGlRgba16f:
      return 8;
    case kGlRgba32f:
    case kGlR32f:
      return 4;
    default:
      return -1;
  }
}

void TextureManager::TextureView(GLuint texture, GLenum target,
                                 GLuint origtexture, GLenum internalformat,
                                 GLuint minlevel, GLuint numlevels,
                                 GLuint minlayer, GLuint numlayers) {
  if (texture == 0 || origtexture == 0 || texture == origtexture) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  auto vit = textures_.find(texture);
  if (vit == textures_.end()) {
    errors_.Record(kGlInvalidValue);  // Never generated.
    return;
  }
  Texture* view = &vit->second;
  if (view->target != 0 || view->alive) {
    errors_.Record(kGlInvalidOperation);  // View name already bound/used.
    return;
  }
  auto oit = textures_.find(origtexture);
  if (oit == textures_.end() || !oit->second.alive) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const Texture* orig = &oit->second;
  if (!orig->immutable || orig->is_view) {
    errors_.Record(kGlInvalidOperation);  // Mutable original / view of view.
    return;
  }
  if (target != orig->target) {
    errors_.Record(kGlInvalidOperation);  // Same-target subset.
    return;
  }
  if (!IsSizedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  // Format class must match (color/depth/depth-stencil stay apart).
  const bool want_color = GlIsColorRenderable(internalformat);
  const bool want_depth = GlIsDepthFormat(internalformat);
  const bool want_ds = GlIsDepthStencilFormat(internalformat);
  // Reference level-0 format of the original for the compatibility check.
  GLenum orig_fmt = 0;
  {
    auto ofit = orig->images.find(target);
    if (ofit == orig->images.end()) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    auto olit = ofit->second.find(static_cast<GLint>(minlevel));
    if (olit == ofit->second.end() || olit->second == nullptr ||
        !olit->second->defined) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    orig_fmt = olit->second->internalformat;
  }
  const bool have_color = GlIsColorRenderable(orig_fmt);
  const bool have_depth = GlIsDepthFormat(orig_fmt);
  const bool have_ds = GlIsDepthStencilFormat(orig_fmt);
  if (want_color != have_color || want_depth != have_depth ||
      want_ds != have_ds) {
    errors_.Record(kGlInvalidOperation);  // Cross-class reinterpretation.
    return;
  }
  if (TextureViewFormatBytes(internalformat) < 0 ||
      TextureViewFormatBytes(internalformat) !=
          TextureViewFormatBytes(orig_fmt)) {
    errors_.Record(kGlInvalidOperation);  // Different texel size.
    return;
  }
  if (numlevels < 1 || minlayer != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Faces covered by this target: cube views span all six faces.
  std::vector<GLenum> faces;
  if (target == kGlTextureCubeMap) {
    for (int f = 0; f < 6; ++f) {
      faces.push_back(kGlTextureCubeMapPositiveX + static_cast<GLenum>(f));
    }
    if (numlayers != 6) {
      errors_.Record(kGlInvalidValue);
      return;
    }
  } else {
    faces.push_back(target);
  }
  // Every level in range must exist and be defined on every face; 3D/array
  // levels must each carry exactly numlayers slices.
  for (GLenum face : faces) {
    auto ofit = orig->images.find(face);
    if (ofit == orig->images.end()) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    for (GLuint l = 0; l < numlevels; ++l) {
      auto olit = ofit->second.find(static_cast<GLint>(minlevel + l));
      if (olit == ofit->second.end() || olit->second == nullptr ||
          !olit->second->defined) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
      if (target != kGlTexture2d && target != kGlTextureCubeMap) {
        if (numlayers != static_cast<GLuint>(olit->second->depth) &&
            !(olit->second->depth <= 1 && numlayers == 1)) {
          errors_.Record(kGlInvalidValue);
          return;
        }
      } else if (numlayers != 1) {
        errors_.Record(kGlInvalidValue);
        return;
      }
      view->images[face][static_cast<GLint>(l)] = olit->second;
    }
  }
  view->alive = true;
  view->target = target;
  view->immutable = true;
  view->is_view = true;
  view->view_orig = origtexture;
  view->view_format = internalformat;
  view->view_minlevel = static_cast<GLint>(minlevel);
  view->view_numlevels = static_cast<GLint>(numlevels);
  view->params = orig->params;  // Independent sampler state from here on.
}

void TextureManager::TexStorage2D(GLenum target, GLsizei levels,
                                  GLenum internalformat, GLsizei width,
                                  GLsizei height) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
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
      lvl.depth = 1;
      // CPU store: zero-filled RGBA8 unpack for color-renderable levels so
      // Clear/FillLevel and ReadPixels have a deterministic store.
      if (IsColorRenderableSized(internalformat)) {
        lvl.pixels.assign((std::size_t)lvl.width * lvl.height * 4, 0);
      }
      tex->images[face + f][l] = std::make_shared<TextureLevel>(lvl);
    }
  }
}

void TextureManager::TexStorage3D(GLenum target, GLsizei levels,
                                  GLenum internalformat, GLsizei width,
                                  GLsizei height, GLsizei depth) {
  Texture* tex = BoundForWrite(target, false);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
    return;
  }
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
    if (IsColorRenderableSized(internalformat)) {
      lvl.pixels.assign((std::size_t)lvl.width * lvl.height * depth * 4, 0);
    }
    tex->images[target][l] = std::make_shared<TextureLevel>(lvl);
  }
}

void TextureManager::TexImage2D(GLenum target, GLint level, GLint internalformat,
                                GLsizei width, GLsizei height, GLint border,
                                GLenum format, GLenum type, const void* pixels) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
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
  if (level < 0 || width < 0 || height < 0 || width > MaxDimension(target) ||
      height > MaxDimension(target)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  GLenum face = target;  // Per-face storage (cube faces keep their own level).
  // Format/type errors are generated even for a NULL upload (spec 8.5), so
  // classify first: an empty region converts zero texels but still reports a
  // bad enum. A NULL upload then allocates a defined, zero-filled level
  // (contents are undefined per spec; zero is this CPU model's deterministic
  // choice).
  std::vector<std::uint8_t> unpacked;
  const std::size_t w = width > 0 ? static_cast<std::size_t>(width) : 0;
  const std::size_t h = height > 0 ? static_cast<std::size_t>(height) : 0;
  if (w > 0 && h > 0) {
    const void* src = pixels;
    std::uint8_t one[4] = {};  // Stand-in so a NULL upload still checks enums.
    if (src == nullptr) src = one;
    switch (UnpackToRgba8(format, type, src, pixels != nullptr ? w : 0,
                           pixels != nullptr ? h : 0, 1, unpacked)) {
      case UnpackResult::kOk:
        break;
      case UnpackResult::kBadEnum:
        errors_.Record(kGlInvalidEnum);
        return;
      case UnpackResult::kNoUnpacker:
        // A NULL upload converts nothing, so only a real upload can hit a
        // missing converter. NULL + legal-but-unconverted = zero-filled level.
        if (pixels != nullptr) {
          errors_.Record(kGlInvalidOperation);
          return;
        }
        break;
    }
  }
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = static_cast<GLenum>(internalformat);
  lvl.upload_format = format;
  lvl.upload_type = type;
  lvl.width = width;
  lvl.height = height;
  lvl.depth = 1;
  if (w > 0 && h > 0) {
    if (pixels != nullptr) {
      lvl.pixels = std::move(unpacked);
    } else {
      lvl.pixels.assign(w * h * 4, 0);
    }
  }
  tex->images[face][level] = std::make_shared<TextureLevel>(lvl);
}

void TextureManager::TexImage3D(GLenum target, GLint level,
                                GLint internalformat, GLsizei width,
                                GLsizei height, GLsizei depth, GLint border,
                                GLenum format, GLenum type,
                                const void* pixels) {
  Texture* tex = BoundForWrite(target, false);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
    return;
  }
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
  // Same enum rules as TexImage2D, extended over depth slices.
  std::vector<std::uint8_t> unpacked;
  const std::size_t w = width > 0 ? static_cast<std::size_t>(width) : 0;
  const std::size_t h = height > 0 ? static_cast<std::size_t>(height) : 0;
  const std::size_t d = depth > 0 ? static_cast<std::size_t>(depth) : 0;
  if (w > 0 && h > 0 && d > 0) {
    const void* src = pixels;
    std::uint8_t one[4] = {};
    if (src == nullptr) src = one;
    const std::size_t cw = pixels != nullptr ? w : 0;
    const std::size_t ch = pixels != nullptr ? h : 0;
    const std::size_t cd = pixels != nullptr ? d : 0;
    switch (UnpackToRgba8(format, type, src, cw, ch, cd, unpacked)) {
      case UnpackResult::kOk:
        break;
      case UnpackResult::kBadEnum:
        errors_.Record(kGlInvalidEnum);
        return;
      case UnpackResult::kNoUnpacker:
        if (pixels != nullptr) {
          errors_.Record(kGlInvalidOperation);
          return;
        }
        break;
    }
  }
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = static_cast<GLenum>(internalformat);
  lvl.width = width;
  lvl.height = height;
  lvl.depth = depth;
  if (w > 0 && h > 0 && d > 0) {
    if (pixels != nullptr) {
      lvl.pixels = std::move(unpacked);
    } else {
      lvl.pixels.assign(w * h * d * 4, 0);
    }
  }
  tex->images[target][level] = std::make_shared<TextureLevel>(lvl);
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
  if (lit == fit->second.end() || !lit->second->defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  TextureLevel& lvl = *lit->second;
  if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 ||
      xoffset + width > lvl.width || yoffset + height > lvl.height) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (width == 0 || height == 0) return;  // Empty patch: still validates above.
  if (pixels == nullptr) return;          // No source: bounds were the check.
  // Unpack the patch, then stamp it row by row into the level's RGBA8 store.
  // Row stride is the *level* width, not the patch width — that is the whole
  // point of a sub-image.
  std::vector<std::uint8_t> patch;
  switch (UnpackToRgba8(format, type, pixels, width, height, 1, patch)) {
    case UnpackResult::kOk:
      break;
    case UnpackResult::kBadEnum:
      errors_.Record(kGlInvalidEnum);
      return;
    case UnpackResult::kNoUnpacker:
      errors_.Record(kGlInvalidOperation);
      return;
  }
  const std::size_t row_texels = static_cast<std::size_t>(lvl.width);
  for (GLsizei row = 0; row < height; ++row) {
    const std::size_t dst_row =
        static_cast<std::size_t>(yoffset + row) * row_texels +
        static_cast<std::size_t>(xoffset);
    std::memcpy(lvl.pixels.data() + dst_row * 4,
                patch.data() + static_cast<std::size_t>(row) * width * 4,
                static_cast<std::size_t>(width) * 4);
  }
  lvl.upload_format = format;
  lvl.upload_type = type;
  lvl.revision = NextTextureContentRevision();
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
  if (lit == fit->second.end() || !lit->second->defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  TextureLevel& lvl = *lit->second;
  if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 ||
      depth < 0 || xoffset + width > lvl.width ||
      yoffset + height > lvl.height || zoffset + depth > lvl.depth) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (width == 0 || height == 0 || depth == 0) return;
  if (pixels == nullptr) return;
  // Same row-stamping as TexSubImage2D, repeated per depth slice.
  std::vector<std::uint8_t> patch;
  switch (UnpackToRgba8(format, type, pixels, width, height, depth, patch)) {
    case UnpackResult::kOk:
      break;
    case UnpackResult::kBadEnum:
      errors_.Record(kGlInvalidEnum);
      return;
    case UnpackResult::kNoUnpacker:
      errors_.Record(kGlInvalidOperation);
      return;
  }
  const std::size_t slice_texels =
      static_cast<std::size_t>(lvl.width) * lvl.height;
  const std::size_t patch_slice =
      static_cast<std::size_t>(width) * height;
  for (GLsizei z = 0; z < depth; ++z) {
    for (GLsizei row = 0; row < height; ++row) {
      const std::size_t dst_row =
          (static_cast<std::size_t>(zoffset + z)) * slice_texels +
          static_cast<std::size_t>(yoffset + row) * lvl.width +
          static_cast<std::size_t>(xoffset);
      const std::size_t src_row =
          static_cast<std::size_t>(z) * patch_slice +
          static_cast<std::size_t>(row) * width;
      std::memcpy(lvl.pixels.data() + dst_row * 4,
                  patch.data() + src_row * 4,
                  static_cast<std::size_t>(width) * 4);
    }
  }
  lvl.revision = NextTextureContentRevision();
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
  if (slit == sfit->second.end() || !slit->second->defined ||
      dlit == dfit->second.end() || !dlit->second->defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const TextureLevel& sl = *slit->second;
  TextureLevel& dl = *dlit->second;
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
        // Earlier rows may already have landed — arm dst revision anyway.
        dl.revision = NextTextureContentRevision();
        errors_.Record(kGlInvalidOperation);
        return;
      }
      std::memcpy(dl.pixels.data() + d, sl.pixels.data() + s,
                  static_cast<std::size_t>(width) * kBpp);
    }
  }
  dl.revision = NextTextureContentRevision();
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
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
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
  tex->images[IsCubeFace(target) ? target : target][level] = std::make_shared<TextureLevel>(lvl);
}

void TextureManager::CompressedTexImage3D(GLenum target, GLint level,
                                          GLenum internalformat, GLsizei width,
                                          GLsizei height, GLsizei depth,
                                          GLint border, GLsizei image_size,
                                          const void* data) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
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
  if (!IsCompressedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (level < 0 || width < 0 || height < 0 || depth < 0 || image_size < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = internalformat;
  lvl.width = width;
  lvl.height = height;
  lvl.depth = depth;
  if (data != nullptr && image_size > 0) {
    lvl.pixels.assign(static_cast<const std::uint8_t*>(data),
                      static_cast<const std::uint8_t*>(data) + image_size);
  }
  tex->images[target][level] = std::make_shared<TextureLevel>(lvl);
}

void TextureManager::CompressedTexSubImage2D(GLenum target, GLint level,
                                             GLint xoffset, GLint yoffset,
                                             GLsizei width, GLsizei height,
                                             GLenum format, GLsizei image_size,
                                             const void* data) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (!IsCompressedFormat(format)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  GLenum face = IsCubeFace(target) ? target : target;
  auto fit = tex->images.find(face);
  if (fit == tex->images.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || !lit->second->defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 || image_size < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (xoffset + width > lit->second->width ||
      yoffset + height > lit->second->height) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // CPU model stores the raw blocks; sub-range copy is a no-op success when
  // data is null (size query), else replace the stored blocks.
  if (data != nullptr && image_size > 0) {
    lit->second->pixels.assign(static_cast<const std::uint8_t*>(data),
                              static_cast<const std::uint8_t*>(data) +
                                  image_size);
    lit->second->revision = NextTextureContentRevision();
  }
}

void TextureManager::CompressedTexSubImage3D(
    GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
    GLsizei width, GLsizei height, GLsizei depth, GLenum format,
    GLsizei image_size, const void* data) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (!IsCompressedFormat(format)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  auto fit = tex->images.find(target);
  if (fit == tex->images.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || !lit->second->defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 ||
      depth < 0 || image_size < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (xoffset + width > lit->second->width ||
      yoffset + height > lit->second->height ||
      zoffset + depth > lit->second->depth) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (data != nullptr && image_size > 0) {
    lit->second->pixels.assign(static_cast<const std::uint8_t*>(data),
                              static_cast<const std::uint8_t*>(data) +
                                  image_size);
    lit->second->revision = NextTextureContentRevision();
  }
}

void TextureManager::CopyTexImage2D(GLenum target, GLint level,
                                    GLenum internalformat, GLsizei width,
                                    GLsizei height, GLint border,
                                    const std::uint8_t rgba[4]) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
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
  if (!IsSizedFormat(internalformat) && !IsCompressedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (level < 0 || width < 0 || height < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Framebuffer-sourced define: RGBA8-unpacked fill with the read color.
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = internalformat;
  lvl.width = width;
  lvl.height = height;
  lvl.depth = 1;
  lvl.pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
  for (std::size_t i = 0; i < lvl.pixels.size(); i += 4) {
    lvl.pixels[i + 0] = rgba[0];
    lvl.pixels[i + 1] = rgba[1];
    lvl.pixels[i + 2] = rgba[2];
    lvl.pixels[i + 3] = rgba[3];
  }
  tex->images[IsCubeFace(target) ? target : target][level] = std::make_shared<TextureLevel>(lvl);
}

void TextureManager::CopyTexSubImage3D(GLenum target, GLint level,
                                       GLint xoffset, GLint yoffset,
                                       GLint zoffset, GLsizei width,
                                       GLsizei height) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  auto fit = tex->images.find(target);
  if (fit == tex->images.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || !lit->second->defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (xoffset + width > lit->second->width ||
      yoffset + height > lit->second->height) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Source-rectangle vs READ drawable is facade-validated; state fills white.
  (void)zoffset;
}

void TextureManager::TexStorage2DMultisample(GLenum target, GLsizei samples,
                                             GLenum internalformat,
                                             GLsizei width, GLsizei height,
                                             GLboolean fixedsamplelocations) {
  static constexpr GLenum kTex2DMS = 0x9100u;
  if (target != kTex2DMS) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (samples < 0 || samples > kMaxSamplesValue || width <= 0 || height <= 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsSizedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
    return;
  }
  if (tex->immutable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  (void)fixedsamplelocations;
  tex->immutable = true;
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = internalformat;
  lvl.width = width;
  lvl.height = height;
  lvl.samples = samples;
  tex->images[target][0] = std::make_shared<TextureLevel>(lvl);
}

void TextureManager::TexStorage3DMultisample(
    GLenum target, GLsizei samples, GLenum internalformat, GLsizei width,
    GLsizei height, GLsizei depth, GLboolean fixedsamplelocations) {
  static constexpr GLenum kTex2DMSArray = 0x9102u;
  if (target != kTex2DMSArray) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (samples < 0 || samples > kMaxSamplesValue || width <= 0 || height <= 0 ||
      depth <= 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsSizedFormat(internalformat)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
    return;
  }
  if (tex->immutable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  (void)fixedsamplelocations;
  tex->immutable = true;
  TextureLevel lvl;
  lvl.defined = true;
  lvl.internalformat = internalformat;
  lvl.width = width;
  lvl.height = height;
  lvl.depth = depth;
  lvl.samples = samples;
  tex->images[target][0] = std::make_shared<TextureLevel>(lvl);
}

void TextureManager::TexBuffer(GLenum target, GLenum internalformat,
                               GLuint buffer) {
  if (target != kGlTextureBuffer) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  Texture* tex = BoundForWrite(target, false);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
    return;
  }
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
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
    return;
  }
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

// Integer vector entry point (spec 8.10, gl32.h): same pname table as
// TexParameteri, params[0] for scalar state. Float-valued params
// (BORDER_COLOR, MIN/MAX_LOD) fail closed with INVALID_OPERATION instead of
// converting (mirrors GetTexParameterI* rule).
void TextureManager::TexParameteriv(GLenum target, GLenum pname,
                                    const GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (pname == kGlTextureBorderColor || pname == kGlTextureMinLod ||
      pname == kGlTextureMaxLod) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  TexParameteri(target, pname, params[0]);
}

void TextureManager::TexParameterIiv(GLenum target, GLenum pname,
                                     const GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (pname == kGlTextureBorderColor) {
    for (int i = 0; i < 4; ++i) {
      tex->params.border_color[i] = static_cast<GLfloat>(params[i]);
    }
    return;
  }
  if (pname == kGlTextureMinLod) {
    tex->params.min_lod = static_cast<GLfloat>(params[0]);
    return;
  }
  if (pname == kGlTextureMaxLod) {
    tex->params.max_lod = static_cast<GLfloat>(params[0]);
    return;
  }
  TexParameteri(target, pname, params[0]);
}

void TextureManager::TexParameterIuiv(GLenum target, GLenum pname,
                                      const GLuint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (pname == kGlTextureBorderColor) {
    for (int i = 0; i < 4; ++i) {
      tex->params.border_color[i] = static_cast<GLfloat>(params[i]);
    }
    return;
  }
  if (pname == kGlTextureMinLod) {
    tex->params.min_lod = static_cast<GLfloat>(params[0]);
    return;
  }
  if (pname == kGlTextureMaxLod) {
    tex->params.max_lod = static_cast<GLfloat>(params[0]);
    return;
  }
  TexParameteri(target, pname, static_cast<GLint>(params[0]));
}

void TextureManager::GenerateMipmap(GLenum target) {
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  if (tex->is_view) {
    errors_.Record(kGlInvalidOperation);  // Views share storage; redefinition is illegal.
    return;
  }
  if (target == kGlTexture2dMultisample ||
      target == kGlTexture2dMultisampleArray || target == kGlTextureBuffer) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  // Cube maps carry six per-face level maps; every other target carries one.
  std::vector<GLenum> faces;
  if (target == kGlTextureCubeMap) {
    for (int f = 0; f < 6; ++f) {
      faces.push_back(kGlTextureCubeMapPositiveX + static_cast<GLenum>(f));
    }
  } else {
    faces.push_back(target);
  }
  // Synthesize each face/level chain with exact box filtering (level 0 must
  // exist and be RGBA8-unpacked; compressed levels fail closed). Missing
  // levels are allocated down to 1x1x1, so mipmap min filters sample a
  // complete chain afterwards (spec 8.14). 3D/array levels halve depth too.
  for (GLenum face : faces) {
    auto it = tex->images.find(face);
    if (it == tex->images.end()) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    auto l0it = it->second.find(0);
    if (l0it == it->second.end() || l0it->second == nullptr ||
        !l0it->second->defined) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    const TextureLevel& l0 = *l0it->second;
    if (l0.pixels.empty() || l0.width <= 0 || l0.height <= 0 ||
        l0.depth <= 0) {
      errors_.Record(kGlInvalidOperation);  // Compressed/empty: no synthesis.
      return;
    }
    GLsizei w = l0.width, h = l0.height, d = l0.depth;
    GLint level = 0;
    std::shared_ptr<TextureLevel> prev = l0it->second;
    while (w > 1 || h > 1 || d > 1) {
      const GLsizei nw = std::max<GLsizei>(1, w / 2);
      const GLsizei nh = std::max<GLsizei>(1, h / 2);
      const GLsizei nd = std::max<GLsizei>(1, d / 2);
      auto next = std::make_shared<TextureLevel>();
      next->defined = true;
      next->internalformat = prev->internalformat;
      next->width = nw;
      next->height = nh;
      next->depth = nd;
      next->pixels.assign(static_cast<std::size_t>(nw) * nh * nd * 4, 0);
      for (GLsizei z = 0; z < nd; ++z) {
        for (GLsizei y = 0; y < nh; ++y) {
          for (GLsizei x = 0; x < nw; ++x) {
            std::uint32_t sum[4] = {0, 0, 0, 0};
            int n = 0;
            for (GLsizei dz = 0; dz < 2 && 2 * z + dz < d; ++dz) {
              for (GLsizei dy = 0; dy < 2 && 2 * y + dy < h; ++dy) {
                for (GLsizei dx = 0; dx < 2 && 2 * x + dx < w; ++dx) {
                  const std::size_t s =
                      ((static_cast<std::size_t>(2 * z + dz) * h +
                        (2 * y + dy)) *
                           w +
                       (2 * x + dx)) *
                      4;
                  for (int c = 0; c < 4; ++c) sum[c] += prev->pixels[s + c];
                  ++n;
                }
              }
            }
            const std::size_t t =
                ((static_cast<std::size_t>(z) * nh + y) * nw + x) * 4;
            for (int c = 0; c < 4; ++c) {
              // Rounded box average: bit-exact, so device tests can assert it.
              next->pixels[t + c] =
                  static_cast<std::uint8_t>((sum[c] + n / 2) / n);
            }
          }
        }
      }
      ++level;
      // GenerateMipmap REGENERATES the chain per spec (levels >0 replaced).
      it->second[level] = next;
      prev = next;
      w = nw;
      h = nh;
      d = nd;
    }
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

std::size_t TextureManager::TotalPixelBytes() const {
  // Views alias the original's shared_ptr<TextureLevel>: count each storage
  // exactly once so the number is a real footprint, not a per-name sum.
  std::set<const TextureLevel*> seen;
  std::size_t total = 0;
  for (const auto& [name, tex] : textures_) {
    if (!tex.alive) continue;
    for (const auto& [face, levels] : tex.images) {
      for (const auto& [level, img] : levels) {
        if (img && seen.insert(img.get()).second) {
          total += img->pixels.size();
        }
      }
    }
  }
  return total;
}

bool TextureManager::FillLevel(GLuint texture, GLenum face_target,
                               GLint level, const std::uint8_t rgba[4]) {
  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.alive) return false;
  auto fit = it->second.images.find(face_target);
  if (fit == it->second.images.end()) return false;
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || !lit->second->defined) return false;
  TextureLevel& lvl = *lit->second;
  if (lvl.internalformat != kGlRgba8) return false;
  if (lvl.width <= 0 || lvl.height <= 0 || lvl.depth <= 0) return false;
  if (lvl.pixels.size() !=
      static_cast<std::size_t>(lvl.width) * lvl.height * lvl.depth * 4) {
    return false;
  }
  for (std::size_t i = 0; i < lvl.pixels.size(); i += 4) {
    lvl.pixels[i + 0] = rgba[0];
    lvl.pixels[i + 1] = rgba[1];
    lvl.pixels[i + 2] = rgba[2];
    lvl.pixels[i + 3] = rgba[3];
  }
  lvl.revision = NextTextureContentRevision();
  return true;
}

void TextureManager::GetTexParameterIiv(GLenum target, GLenum pname,
                                         GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname == kGlTextureBorderColor || pname == kGlTextureMinLod ||
      pname == kGlTextureMaxLod) {
    // Float-valued params have no integer read (spec 8.10).
    Texture* tex = BoundForWrite(target, true);
    if (tex == nullptr) return;
    errors_.Record(kGlInvalidOperation);
    return;
  }
  GetTexParameteriv(target, pname, params);
}

void TextureManager::GetTexParameterIuiv(GLenum target, GLenum pname,
                                         GLuint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname == kGlTextureBorderColor || pname == kGlTextureMinLod ||
      pname == kGlTextureMaxLod) {
    Texture* tex = BoundForWrite(target, true);
    if (tex == nullptr) return;
    errors_.Record(kGlInvalidOperation);
    return;
  }
  GLint v = 0;
  GetTexParameteriv(target, pname, &v);
  if (HasPending()) return;  // Error (bad target/pname) already recorded.
  *params = static_cast<GLuint>(v);
}

void TextureManager::GetTexLevelParameteriv(GLenum target, GLint level,
                                            GLenum pname, GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Texture* tex = BoundForWrite(target, true);
  if (tex == nullptr) return;
  auto fit = tex->images.find(target);
  if (fit == tex->images.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || !lit->second->defined) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const TextureLevel& lvl = *lit->second;
  switch (pname) {
    case kGlTextureLevelWidth:
      *params = lvl.width;
      return;
    case kGlTextureLevelHeight:
      *params = lvl.height;
      return;
    case kGlTextureLevelDepth:
      *params = lvl.depth;
      return;
    case kGlTextureLevelInternalFormat:
      // Views reinterpret storage: report the view format, not the shared
      // level's original tag.
      *params = static_cast<GLint>(tex->is_view ? tex->view_format
                                                : lvl.internalformat);
      return;
    case kGlTextureLevelSamples:
      *params = lvl.samples;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void TextureManager::GetTexLevelParameterfv(GLenum target, GLint level,
                                            GLenum pname, GLfloat* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  GLint v = 0;
  GetTexLevelParameteriv(target, level, pname, &v);
  if (HasPending()) return;  // Error already recorded; touch nothing.
  *params = static_cast<GLfloat>(v);
}

TextureLevel TextureManager::LevelState(GLuint texture, GLenum face_target,
                                        GLint level) const {
  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.alive) return TextureLevel();
  auto fit = it->second.images.find(face_target);
  if (fit == it->second.images.end()) return TextureLevel();
  auto lit = fit->second.find(level);
  return lit == fit->second.end() ? TextureLevel() : *lit->second;
}

const TextureLevel* TextureManager::LevelStateRef(GLuint texture,
                                                  GLenum face_target,
                                                  GLint level) const {
  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.alive) return nullptr;
  auto fit = it->second.images.find(face_target);
  if (fit == it->second.images.end()) return nullptr;
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || lit->second == nullptr) return nullptr;
  return lit->second.get();
}

GLuint TextureManager::BoundTexture(GLenum target) const {
  auto it = unit_bindings_[active_unit_].find(target);
  return it == unit_bindings_[active_unit_].end() ? 0 : it->second;
}

GLuint TextureManager::BoundTextureForUnit(GLuint unit, GLenum target) const {
  if (unit >= unit_bindings_.size()) return 0;
  auto it = unit_bindings_[unit].find(target);
  return it == unit_bindings_[unit].end() ? 0 : it->second;
}

GLenum TextureManager::LevelInternalFormat(GLuint texture, GLenum face_target,
                                           GLint level) const {
  auto it = textures_.find(texture);
  if (it == textures_.end() || !it->second.alive) return 0;
  if (it->second.is_view) return it->second.view_format;
  auto fit = it->second.images.find(face_target);
  if (fit == it->second.images.end()) return 0;
  auto lit = fit->second.find(level);
  if (lit == fit->second.end() || lit->second == nullptr) return 0;
  return lit->second->internalformat;
}

GLuint TextureManager::ActiveUnit() const { return active_unit_; }

}  // namespace tgles
