// Texture views (EXT/OES_texture_view): storage aliasing with reinterpreted
// format, verified by real bytes. Ground truth: Khronos EXT_texture_view
// spec (view shares the original's storage; writes either way are visible
// both ways; redefinition through a view is illegal) + MobileGL
// DirectGLES/Managers.cpp:3594 (gated by SupportsTextureView + entry point).

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "tgles/facade/gles.h"

namespace {

void FillSolid(tgles::TextureManager& tm, tgles::GLuint tex,
                  std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  std::uint8_t px[4 * 4 * 4];
  for (int i = 0; i < 16; ++i) {
    px[i * 4 + 0] = r;
    px[i * 4 + 1] = g;
    px[i * 4 + 2] = b;
    px[i * 4 + 3] = 255;
  }
  tm.BindTexture(tgles::kGlTexture2d, tex);
  tm.TexSubImage2D(tgles::kGlTexture2d, 0, 0, 0, 4, 4, tgles::kGlRgba,
                   tgles::kGlUnsignedByte, px);
}

}  // namespace

TEST(TextureView, AliasSharesBytesBothDirections) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint orig = 0, view = 0;
  ctx.textures().GenTextures(1, &orig);
  ctx.textures().GenTextures(1, &view);
  ctx.textures().BindTexture(tgles::kGlTexture2d, orig);
  ctx.textures().TexStorage2D(tgles::kGlTexture2d, 2, tgles::kGlRgba8, 4, 4);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  FillSolid(ctx.textures(), orig, 255, 0, 0);  // Red via original.
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);

  ctx.textures().TextureView(view, tgles::kGlTexture2d, orig,
                             tgles::kGlSrgb8Alpha8, 0, 2, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);

  // Format reinterpretation is visible in level queries...
  tgles::GLint origFmt = 0, viewFmt = 0;
  ctx.textures().BindTexture(tgles::kGlTexture2d, orig);
  ctx.textures().GetTexLevelParameteriv(tgles::kGlTexture2d, 0,
                                        tgles::kGlTextureLevelInternalFormat,
                                        &origFmt);
  ctx.textures().BindTexture(tgles::kGlTexture2d, view);
  ctx.textures().GetTexLevelParameteriv(tgles::kGlTexture2d, 0,
                                        tgles::kGlTextureLevelInternalFormat,
                                        &viewFmt);
  EXPECT_EQ(origFmt, (tgles::GLint)tgles::kGlRgba8);
  EXPECT_EQ(viewFmt, (tgles::GLint)tgles::kGlSrgb8Alpha8);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);

  // ...and bytes written through the view read back through the original.
  FillSolid(ctx.textures(), view, 0, 255, 0);  // Green via view.
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  tgles::GLuint dst = 0;
  ctx.textures().GenTextures(1, &dst);
  ctx.textures().BindTexture(tgles::kGlTexture2d, dst);
  ctx.textures().TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 4, 4);
  ctx.textures().BindTexture(tgles::kGlTexture2d, orig);
  ctx.textures().CopyImageSubData(orig, tgles::kGlTexture2d, 0, 0, 0, 0, dst,
                                  tgles::kGlTexture2d, 0, 0, 0, 0, 4, 4, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  // Byte proof via the FBO readback path (CPU store is the readback source).
  tgles::GLuint fbo = 0;
  ctx.framebuffers().GenFramebuffers(1, &fbo);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, dst, 0);
  ctx.framebuffers().BindFramebuffer(tgles::kGlReadFramebuffer, fbo);
  std::vector<std::uint8_t> px(64, 0);
  ctx.ReadPixels(0, 0, 4, 4, tgles::kGlRgba, tgles::kGlUnsignedByte,
                 px.data());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  EXPECT_EQ(px[0], 0u);  // Green written through the view survived the copy.
  EXPECT_EQ(px[1], 255u);
  EXPECT_EQ(px[2], 0u);
}

TEST(TextureView, RejectsBadViews) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  tgles::GLuint orig = 0, view = 0, other = 0;
  ctx.textures().GenTextures(1, &orig);
  ctx.textures().GenTextures(1, &view);
  ctx.textures().GenTextures(1, &other);
  // Mutable original: not viewable.
  ctx.textures().BindTexture(tgles::kGlTexture2d, orig);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4, 4, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  ctx.textures().TextureView(view, tgles::kGlTexture2d, orig,
                             tgles::kGlRgba8, 0, 1, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  // Immutable original: viewable.
  ctx.textures().TexStorage2D(tgles::kGlTexture2d, 2, tgles::kGlRgba8, 4, 4);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  ctx.textures().TextureView(view, tgles::kGlTexture2d, orig,
                             tgles::kGlRgba8, 0, 2, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  // Bound view name cannot be reused.
  ctx.textures().BindTexture(tgles::kGlTexture2d, view);
  ctx.textures().TextureView(other, tgles::kGlTexture2d, orig,
                             tgles::kGlRgba8, 0, 1, 0, 1);
  ctx.textures().BindTexture(tgles::kGlTexture2d, view);
  ctx.textures().TextureView(view, tgles::kGlTexture2d, orig,
                             tgles::kGlRgba8, 0, 1, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  // View of a view is rejected (documented subset).
  ctx.textures().TextureView(other, tgles::kGlTexture2d, view,
                             tgles::kGlRgba8, 0, 1, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  // Out-of-range levels and cross-class formats fail.
  ctx.textures().TextureView(other, tgles::kGlTexture2d, orig,
                             tgles::kGlRgba8, 0, 5, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlInvalidOperation);
  ctx.textures().TextureView(other, tgles::kGlTexture2d, orig,
                             tgles::kGlDepthComponent24, 0, 1, 0, 1);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  // Redefinition through the view is illegal; sub-images are legal.
  ctx.textures().BindTexture(tgles::kGlTexture2d, view);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4, 4, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlInvalidOperation);
  ctx.textures().TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 4, 4);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlInvalidOperation);
  ctx.textures().TexSubImage2D(tgles::kGlTexture2d, 0, 0, 0, 1, 1,
                               tgles::kGlRgba, tgles::kGlUnsignedByte,
                               nullptr);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
}
