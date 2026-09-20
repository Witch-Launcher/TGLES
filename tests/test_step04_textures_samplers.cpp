// Step 4 tests: textures (spec ch.8) and samplers (spec 8.2).

#include "test_framework.h"

#include "tgles/sampler.h"
#include "tgles/texture.h"
#include "tgles/vertex_array.h"  // kGlUnsignedByte pixel type.

TEST(Step04, TextureLifecycleAndUnits) {
  tgles::TextureManager tm;
  EXPECT_EQ(tgles::kMaxCombinedTextureImageUnits, 32);  // Spec minimum.
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  EXPECT_NE(t, 0u);
  EXPECT_EQ(tm.IsTexture(t), tgles::kGlFalse);  // Not bound yet.
  tm.ActiveTexture(0x84C0u + 3);                // TEXTURE3.
  EXPECT_EQ(tm.ActiveUnit(), 3u);
  tm.BindTexture(tgles::kGlTexture2d, t);
  EXPECT_EQ(tm.IsTexture(t), tgles::kGlTrue);
  EXPECT_EQ(tm.BoundTexture(tgles::kGlTexture2d), t);
  tm.ActiveTexture(0x84C0u + 32);  // Past the last unit.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
  tm.BindTexture(0x9999u, t);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

TEST(Step04, ImmutableStorageOnce) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexStorage2D(tgles::kGlTexture2d, 5, tgles::kGlRgba8, 64, 64);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(tm.IsImmutable(t));
  tgles::TextureLevel lvl = tm.LevelState(t, tgles::kGlTexture2d, 0);
  EXPECT_TRUE(lvl.defined);
  EXPECT_EQ(lvl.width, 64);
  EXPECT_EQ(lvl.height, 64);
  tm.TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 8, 8);  // Again?
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidOperation);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 8, 8, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidOperation);  // Immutable.
  tgles::GLint imm = 0;
  tm.GetTexParameteriv(tgles::kGlTexture2d, tgles::kGlTextureImmutableFormat,
                       &imm);
  EXPECT_EQ(imm, 1);
}

TEST(Step04, StorageLevelValidation) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexStorage2D(tgles::kGlTexture2d, 9, tgles::kGlRgba8, 64, 64);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);  // Too many levels.
  tm.TexStorage2D(tgles::kGlTexture2d, 1, 0x1234u, 64, 64);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);  // Bad format.
  tm.TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 0, 64);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
}

TEST(Step04, MutableUploadAndSubBounds) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexSubImage2D(tgles::kGlTexture2d, 0, 4, 4, 8, 8, tgles::kGlRgba,
                   tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexSubImage2D(tgles::kGlTexture2d, 0, 12, 12, 8, 8, tgles::kGlRgba,
                   tgles::kGlUnsignedByte, nullptr);  // Overruns.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  tm.TexSubImage2D(tgles::kGlTexture2d, 3, 0, 0, 4, 4, tgles::kGlRgba,
                   tgles::kGlUnsignedByte, nullptr);  // No level 3.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidOperation);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 8, 8, 1,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);  // Border.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
}

TEST(Step04, CompressedAstcBlockRule) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  const std::uint8_t blob[16] = {};
  tm.CompressedTexImage2D(tgles::kGlTexture2d, 0,
                          tgles::kGlCompressedRgbaAstc4x4, 8, 8, 0,
                          sizeof(blob), blob);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.CompressedTexImage2D(tgles::kGlTexture2d, 0,
                          tgles::kGlCompressedRgbaAstc4x4, 7, 8, 0,
                          sizeof(blob), blob);  // 7 % 4 != 0.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  tm.CompressedTexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 8, 8, 0,
                          sizeof(blob), blob);  // Not compressed.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
}

TEST(Step04, CubeMapFaces) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTextureCubeMap, t);
  tm.TexStorage2D(tgles::kGlTextureCubeMap, 1, tgles::kGlRgba8, 16, 16);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  for (int f = 0; f < 6; ++f) {
    tgles::TextureLevel lvl = tm.LevelState(
        t, tgles::kGlTextureCubeMapPositiveX + f, 0);
    EXPECT_TRUE(lvl.defined);
    EXPECT_EQ(lvl.width, 16);
  }
}

TEST(Step04, TextureParameters) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tgles::GLint v = 0;
  tm.GetTexParameteriv(tgles::kGlTexture2d, tgles::kGlTextureMinFilter, &v);
  EXPECT_EQ(v, static_cast<tgles::GLint>(tgles::kGlNearestMipmapLinear));
  tm.TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMinFilter,
                   tgles::kGlLinear);
  tm.TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureWrapS,
                   tgles::kGlRepeat);
  tm.TexParameterf(tgles::kGlTexture2d, tgles::kGlTextureMaxLod, 4.0f);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMagFilter,
                   tgles::kGlLinearMipmapLinear);  // Illegal for MAG.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
  tm.TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMaxLevel, -1);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  tm.GenerateMipmap(tgles::kGlTexture2d);  // No level 0 defined.
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidOperation);
}

TEST(Step04, SizeLimitsMeetMinimums) {
  EXPECT_TRUE(tgles::kMaxTextureSizeValue >= 2048);
  EXPECT_TRUE(tgles::kMaxCubeMapTextureSizeValue >= 2048);
  EXPECT_TRUE(tgles::kMax3dTextureSizeValue >= 256);
  EXPECT_TRUE(tgles::kMaxArrayTextureLayersValue >= 256);
  EXPECT_TRUE(tgles::kMaxSamplesValue >= 4);
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlRgba8, 16384, 16);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);  // Past our 8192.
}

TEST(Step04, SamplerObjects) {
  tgles::SamplerManager sm;
  tgles::GLuint s = 0;
  sm.GenSamplers(1, &s);
  EXPECT_NE(s, 0u);
  EXPECT_EQ(sm.IsSampler(s), tgles::kGlFalse);
  sm.BindSampler(2, s);
  EXPECT_EQ(sm.IsSampler(s), tgles::kGlTrue);
  EXPECT_EQ(sm.BoundSampler(2), s);
  sm.SamplerParameteri(s, tgles::kGlTextureMinFilter, tgles::kGlLinear);
  tgles::GLint v = 0;
  sm.GetSamplerParameteriv(s, tgles::kGlTextureMinFilter, &v);
  EXPECT_EQ(v, static_cast<tgles::GLint>(tgles::kGlLinear));
  sm.BindSampler(32, s);  // Past unit 31.
  EXPECT_EQ(sm.GetError(), tgles::kGlInvalidValue);
  sm.BindSampler(0, 424242u);  // Unknown sampler.
  EXPECT_EQ(sm.GetError(), tgles::kGlInvalidOperation);
  sm.SamplerParameteri(424242u, tgles::kGlTextureMinFilter, tgles::kGlLinear);
  EXPECT_EQ(sm.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
}
