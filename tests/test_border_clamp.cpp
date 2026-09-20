// Border-clamp TDD (red first): GL_CLAMP_TO_BORDER (gl32.h 0x812D) with
// float border color via TEXTURE_BORDER_COLOR (gl32.h 0x1004). Previously
// rejected with INVALID_ENUM (see CtsSubset.BorderClampRejectedHonestly,
// now updated to acceptance below).

#include "test_framework.h"

#include "tgles/sampler.h"
#include "tgles/texture.h"

TEST(BorderClamp, TextureWrapAndBorderColor) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureWrapS,
                   tgles::kGlClampToBorder);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  const tgles::GLfloat color[4] = {1.0f, 0.5f, 0.0f, 1.0f};
  tm.TexParameterfv(tgles::kGlTexture2d, tgles::kGlTextureBorderColor,
                    color);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexParameterfv(tgles::kGlTexture2d, tgles::kGlTextureBorderColor,
                    nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  tgles::GLfloat out[4] = {0};
  tm.GetTexParameterfv(tgles::kGlTexture2d, tgles::kGlTextureBorderColor,
                       out);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(out[0] == 1.0f && out[1] == 0.5f && out[2] == 0.0f &&
              out[3] == 1.0f);
  tgles::GLint wrap = 0;
  tm.GetTexParameteriv(tgles::kGlTexture2d, tgles::kGlTextureWrapS, &wrap);
  EXPECT_EQ(wrap, static_cast<tgles::GLint>(tgles::kGlClampToBorder));
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

TEST(BorderClamp, SamplerWrapAndBorderColor) {
  tgles::SamplerManager sm;
  tgles::GLuint s = 0;
  sm.GenSamplers(1, &s);
  sm.BindSampler(0, s);
  sm.SamplerParameteri(s, tgles::kGlTextureWrapT, tgles::kGlClampToBorder);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
  const tgles::GLfloat color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  sm.SamplerParameterfv(s, tgles::kGlTextureBorderColor, color);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
  tgles::GLfloat out[4] = {9, 9, 9, 9};
  sm.GetSamplerParameterfv(s, tgles::kGlTextureBorderColor, out);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f &&
              out[3] == 0.0f);
  sm.SamplerParameterfv(s, tgles::kGlTextureBorderColor, nullptr);
  EXPECT_EQ(sm.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
}
