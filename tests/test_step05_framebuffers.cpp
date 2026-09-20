// Step 5 tests: renderbuffers + FBO attach/completeness/draw-buffers/blit.

#include "test_framework.h"

#include "tgles/framebuffer.h"
#include "tgles/texture.h"

namespace {

struct Rig {
  tgles::TextureManager textures;
  tgles::RenderbufferManager rbos;
  tgles::FramebufferManager fbos;
  Rig() : fbos(&textures, &rbos) {}
};

tgles::GLuint MakeColorTexture(Rig& rig, tgles::GLsizei w, tgles::GLsizei h) {
  tgles::GLuint t = 0;
  rig.textures.GenTextures(1, &t);
  rig.textures.BindTexture(tgles::kGlTexture2d, t);
  rig.textures.TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, w, h);
  return t;
}

tgles::GLuint MakeDepthRb(Rig& rig, tgles::GLsizei w, tgles::GLsizei h) {
  tgles::GLuint r = 0;
  rig.rbos.GenRenderbuffers(1, &r);
  rig.rbos.BindRenderbuffer(tgles::kGlRenderbuffer, r);
  rig.rbos.RenderbufferStorage(tgles::kGlRenderbuffer,
                               tgles::kGlDepthComponent24, w, h);
  return r;
}

}  // namespace

TEST(Step05, RenderbufferStorage) {
  Rig rig;
  EXPECT_TRUE(tgles::kMaxRenderbufferSizeValue >= 2048);  // Spec minimum.
  tgles::GLuint r = 0;
  rig.rbos.GenRenderbuffers(1, &r);
  rig.rbos.BindRenderbuffer(tgles::kGlRenderbuffer, r);
  rig.rbos.RenderbufferStorage(tgles::kGlRenderbuffer, tgles::kGlRgba8, 64, 48);
  EXPECT_EQ(rig.rbos.GetError(), tgles::kGlNoError);
  tgles::GLint w = 0, samples = -1;
  rig.rbos.GetRenderbufferParameteriv(tgles::kGlRenderbuffer,
                                      tgles::kGlRenderbufferWidth, &w);
  rig.rbos.GetRenderbufferParameteriv(tgles::kGlRenderbuffer,
                                      tgles::kGlRenderbufferSamples, &samples);
  EXPECT_EQ(w, 64);
  EXPECT_EQ(samples, 0);
  rig.rbos.RenderbufferStorageMultisample(tgles::kGlRenderbuffer, 4,
                                          tgles::kGlRgba8, 64, 48);
  EXPECT_EQ(rig.rbos.GetError(), tgles::kGlNoError);
  rig.rbos.RenderbufferStorageMultisample(tgles::kGlRenderbuffer, 99,
                                          tgles::kGlRgba8, 64, 48);
  EXPECT_EQ(rig.rbos.GetError(), tgles::kGlInvalidValue);  // Past MAX_SAMPLES.
  rig.rbos.RenderbufferStorage(tgles::kGlRenderbuffer, 0x1234u, 64, 48);
  EXPECT_EQ(rig.rbos.GetError(), tgles::kGlInvalidEnum);
}

TEST(Step05, CompleteColorDepthFbo) {
  Rig rig;
  tgles::GLuint f = 0;
  rig.fbos.GenFramebuffers(1, &f);
  rig.fbos.BindFramebuffer(tgles::kGlFramebuffer, f);
  rig.fbos.FramebufferTexture2D(tgles::kGlFramebuffer,
                                tgles::kGlColorAttachment0,
                                tgles::kGlTexture2d, MakeColorTexture(rig, 64, 64),
                                0);
  rig.fbos.FramebufferRenderbuffer(
      tgles::kGlFramebuffer, tgles::kGlDepthAttachment,
      tgles::kGlRenderbuffer, MakeDepthRb(rig, 64, 64));
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlNoError);
  EXPECT_EQ(rig.fbos.CheckFramebufferStatus(tgles::kGlFramebuffer),
            tgles::kGlFramebufferComplete);
}

TEST(Step05, MissingAttachment) {
  Rig rig;
  tgles::GLuint f = 0;
  rig.fbos.GenFramebuffers(1, &f);
  rig.fbos.BindFramebuffer(tgles::kGlFramebuffer, f);
  EXPECT_EQ(rig.fbos.CheckFramebufferStatus(tgles::kGlFramebuffer),
            tgles::kGlFramebufferIncompleteMissingAttachment);
}

TEST(Step05, DimensionMismatch) {
  Rig rig;
  tgles::GLuint f = 0;
  rig.fbos.GenFramebuffers(1, &f);
  rig.fbos.BindFramebuffer(tgles::kGlFramebuffer, f);
  rig.fbos.FramebufferTexture2D(tgles::kGlFramebuffer,
                                tgles::kGlColorAttachment0,
                                tgles::kGlTexture2d, MakeColorTexture(rig, 64, 64),
                                0);
  rig.fbos.FramebufferRenderbuffer(tgles::kGlFramebuffer,
                                   tgles::kGlDepthAttachment,
                                   tgles::kGlRenderbuffer,
                                   MakeDepthRb(rig, 32, 32));
  EXPECT_EQ(rig.fbos.CheckFramebufferStatus(tgles::kGlFramebuffer),
            tgles::kGlFramebufferIncompleteDimensions);
}

TEST(Step05, WrongFormatClassForSlot) {
  Rig rig;
  tgles::GLuint f = 0;
  rig.fbos.GenFramebuffers(1, &f);
  rig.fbos.BindFramebuffer(tgles::kGlFramebuffer, f);
  // Depth texture on a COLOR slot.
  tgles::GLuint dt = 0;
  rig.textures.GenTextures(1, &dt);
  rig.textures.BindTexture(tgles::kGlTexture2d, dt);
  rig.textures.TexStorage2D(tgles::kGlTexture2d, 1,
                            tgles::kGlDepthComponent24, 32, 32);
  rig.fbos.FramebufferTexture2D(tgles::kGlFramebuffer,
                                tgles::kGlColorAttachment0,
                                tgles::kGlTexture2d, dt, 0);
  EXPECT_EQ(rig.fbos.CheckFramebufferStatus(tgles::kGlFramebuffer),
            tgles::kGlFramebufferIncompleteAttachment);
}

TEST(Step05, DrawBuffersValidation) {
  Rig rig;
  EXPECT_TRUE(tgles::kMaxDrawBuffers >= 4);  // Spec minimum.
  EXPECT_TRUE(tgles::kMaxColorAttachments >= 4);
  tgles::GLuint f = 0;
  rig.fbos.GenFramebuffers(1, &f);
  rig.fbos.BindFramebuffer(tgles::kGlFramebuffer, f);
  const tgles::GLenum mrt[2] = {tgles::kGlColorAttachment0,
                                tgles::kGlColorAttachment0 + 1};
  rig.fbos.DrawBuffers(2, mrt);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlNoError);
  const tgles::GLenum bad[1] = {0x1234u};
  rig.fbos.DrawBuffers(1, bad);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlInvalidEnum);
  rig.fbos.DrawBuffers(99, mrt);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlInvalidValue);
  // Default framebuffer only takes BACK/NONE.
  rig.fbos.BindFramebuffer(tgles::kGlFramebuffer, 0);
  rig.fbos.DrawBuffers(1, mrt);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlInvalidOperation);
  const tgles::GLenum back[1] = {tgles::kGlBack};
  rig.fbos.DrawBuffers(1, back);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlNoError);
}

TEST(Step05, BlitValidation) {
  Rig rig;
  rig.fbos.BlitFramebuffer(0, 0, 8, 8, 0, 0, 8, 8, tgles::kGlColorBufferBit,
                           tgles::kGlNearest);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlNoError);
  rig.fbos.BlitFramebuffer(0, 0, 8, 8, 0, 0, 8, 8, 0xFFFFFFFFu,
                           tgles::kGlNearest);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlInvalidValue);
  rig.fbos.BlitFramebuffer(0, 0, 8, 8, 0, 0, 8, 8, tgles::kGlDepthBufferBit,
                           tgles::kGlLinear);  // LINEAR + depth.
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlInvalidOperation);
  const tgles::GLenum atts[1] = {tgles::kGlColorAttachment0};
  rig.fbos.InvalidateFramebuffer(tgles::kGlFramebuffer, 1, atts);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlNoError);
  const tgles::GLenum bad[1] = {0x1234u};
  rig.fbos.InvalidateFramebuffer(tgles::kGlFramebuffer, 1, bad);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlInvalidEnum);
}

TEST(Step05, AttachmentQueries) {
  Rig rig;
  tgles::GLuint f = 0;
  rig.fbos.GenFramebuffers(1, &f);
  rig.fbos.BindFramebuffer(tgles::kGlFramebuffer, f);
  tgles::GLuint t = MakeColorTexture(rig, 16, 16);
  rig.fbos.FramebufferTexture2D(tgles::kGlFramebuffer,
                                tgles::kGlColorAttachment0,
                                tgles::kGlTexture2d, t, 0);
  tgles::GLint type = 0, name = 0, level = -1;
  rig.fbos.GetFramebufferAttachmentParameteriv(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlFramebufferAttachmentObjectType, &type);
  rig.fbos.GetFramebufferAttachmentParameteriv(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlFramebufferAttachmentObjectName, &name);
  rig.fbos.GetFramebufferAttachmentParameteriv(
      tgles::kGlFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlFramebufferAttachmentTextureLevel, &level);
  EXPECT_EQ(type, 0x1702);  // GL_TEXTURE.
  EXPECT_EQ(name, static_cast<tgles::GLint>(t));
  EXPECT_EQ(level, 0);
  EXPECT_EQ(rig.fbos.GetError(), tgles::kGlNoError);
}
