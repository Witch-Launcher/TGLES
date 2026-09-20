// 3D/array upload + copy TDD (red first): TexImage3D/TexSubImage3D with
// w*h*d*4 unpacked storage, then CopyImageSubData across z slices.
// Targets: TEXTURE_3D and TEXTURE_2D_ARRAY (both keyed images[target][level]
// with depth, matching TexStorage3D).

#include "test_framework.h"

#include "tgles/texture.h"
#include "tgles/vertex_array.h"  // kGlUnsignedByte pixel type.

TEST(CopyImage3D, UploadAndSliceCopy) {
  tgles::TextureManager tm;
  tgles::GLuint s = 0, d = 0;
  tm.GenTextures(1, &s);
  tm.GenTextures(1, &d);
  tm.BindTexture(tgles::kGlTexture3d, s);
  tm.TexImage3D(tgles::kGlTexture3d, 0, tgles::kGlRgba8, 4, 4, 4, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.BindTexture(tgles::kGlTexture3d, d);
  tm.TexImage3D(tgles::kGlTexture3d, 0, tgles::kGlRgba8, 4, 4, 4, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.CopyImageSubData(s, tgles::kGlTexture3d, 0, 0, 0, 1, d,
                      tgles::kGlTexture3d, 0, 0, 0, 2, 4, 4, 2);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  // Out-of-range slice.
  tm.CopyImageSubData(s, tgles::kGlTexture3d, 0, 0, 0, 3, d,
                      tgles::kGlTexture3d, 0, 0, 0, 0, 4, 4, 2);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}

TEST(CopyImage3D, SubImageUploadBounds) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2dArray, t);
  tm.TexImage3D(tgles::kGlTexture2dArray, 0, tgles::kGlRgba8, 4, 4, 2, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexSubImage3D(tgles::kGlTexture2dArray, 0, 0, 0, 0, 4, 4, 2,
                   tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.TexSubImage3D(tgles::kGlTexture2dArray, 0, 3, 3, 0, 4, 4, 1,
                   tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
}
