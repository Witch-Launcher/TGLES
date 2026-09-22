// Texture pixel upload (spec ch.8, TDD red-first).
//
// Why: TexImage2D used to copy only min(size, 64) bytes and ignore
// format/type, while TexSubImage2D/3D validated bounds and copied nothing.
// A translator that drops texels renders the wrong image with NO_ERROR, which
// is invisible to every other test. These cases pin the bytes themselves via
// LevelState().pixels (the RGBA8-unpacked store).

#include "test_framework.h"

#include <cstdint>
#include <vector>

#include "tgles/state/texture.h"
#include "tgles/state/vertex_array.h"  // kGlUnsignedByte pixel type.

TEST(TextureUpload, FullImageCopiesEveryByte) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  // 8x8 RGBA8 ramp: byte i == i mod 251, so a 64-byte prefix copy passes a
  // naive check but fails the tail (256 bytes total).
  std::vector<std::uint8_t> src(8 * 8 * 4);
  for (std::size_t i = 0; i < src.size(); ++i)
    src[i] = (std::uint8_t)(i % 251);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 8, 8, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, src.data());
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tgles::TextureLevel lvl = tm.LevelState(t, tgles::kGlTexture2d, 0);
  EXPECT_TRUE(lvl.defined);
  EXPECT_EQ(lvl.pixels.size(), std::size_t{256});
  for (std::size_t i = 0; i < src.size(); ++i) {
    if (lvl.pixels[i] != src[i]) {
      EXPECT_TRUE(false);  // First mismatch fails the case.
      break;
    }
  }
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

TEST(TextureUpload, NullPixelsZeroFills) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 2, 2, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tgles::TextureLevel lvl = tm.LevelState(t, tgles::kGlTexture2d, 0);
  for (std::uint8_t b : lvl.pixels) EXPECT_EQ(b, (std::uint8_t)0);
}

TEST(TextureUpload, BadFormatTypeIsRejected) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  const std::uint8_t src[16] = {};
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 2, 2, 0,
                0x9999u /*bad format*/, tgles::kGlUnsignedByte, src);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 2, 2, 0,
                tgles::kGlRgba, 0x9999u /*bad type*/, src);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

TEST(TextureUpload, SubImagePatchesRows) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4, 4, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  // 2x2 white patch at (1,1).
  const std::uint8_t white[2 * 2 * 4] = {
      255, 255, 255, 255, 255, 255, 255, 255,
      255, 255, 255, 255, 255, 255, 255, 255};
  tm.TexSubImage2D(tgles::kGlTexture2d, 0, 1, 1, 2, 2, tgles::kGlRgba,
                   tgles::kGlUnsignedByte, white);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tgles::TextureLevel lvl = tm.LevelState(t, tgles::kGlTexture2d, 0);
  auto at = [&](int x, int y) -> std::uint8_t {
    return lvl.pixels[(std::size_t)(y * 4 + x) * 4];
  };
  EXPECT_EQ(at(0, 0), (std::uint8_t)0);
  EXPECT_EQ(at(1, 1), (std::uint8_t)255);
  EXPECT_EQ(at(2, 2), (std::uint8_t)255);
  EXPECT_EQ(at(3, 3), (std::uint8_t)0);
  EXPECT_EQ(at(0, 3), (std::uint8_t)0);
}
